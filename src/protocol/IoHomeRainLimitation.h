#pragma once
#include "IoHomeLimitation.h"
#include <cmath>

// Evidence and request: laberning/home_io_control
// 38f03e651b8418e21685a79f674a8407ed961c8a. The hold is software policy, not RF timing.
constexpr uint32_t IOHC_RAIN_EVIDENCE_HOLD_MS=2UL*60*60*1000;
constexpr float IOHC_RAIN_TARGET_TOLERANCE=100.0f*100/0xC800;
enum class RainLimitationRule:uint8_t { None,RainOriginator,ClampedCommand };
struct RainLimitationInput {
    bool hasLastCommand=false;uint8_t originator=0;bool rainEvidenceRecent=false;
    bool predictedTargetValid=false;float predictedTarget=0;
    bool observedTargetValid=false;float observedTarget=0;bool stopped=false;
};
inline bool ioHomeRainTargetsMatch(const RainLimitationInput &in) {
    return in.predictedTargetValid&&in.observedTargetValid&&
        std::isfinite(in.predictedTarget)&&std::isfinite(in.observedTarget)&&
        std::fabs(in.predictedTarget-in.observedTarget)<=IOHC_RAIN_TARGET_TOLERANCE;
}
inline RainLimitationRule ioHomeRainLimitationRule(const RainLimitationInput &in) {
    if(in.hasLastCommand&&in.originator==2)return RainLimitationRule::RainOriginator;
    if(in.rainEvidenceRecent&&in.stopped&&in.predictedTargetValid&&in.observedTargetValid&&
       std::isfinite(in.predictedTarget)&&std::isfinite(in.observedTarget)&&!ioHomeRainTargetsMatch(in))
        return RainLimitationRule::ClampedCommand;
    return RainLimitationRule::None;
}
inline const char *ioHomeRainRuleName(RainLimitationRule rule) {
    return rule==RainLimitationRule::RainOriginator?"rain-originator":
        rule==RainLimitationRule::ClampedCommand?"rain-clamp":"none";
}
struct IoHomeLastCommand {
    bool valid=false;uint32_t node=0;uint8_t originator=0;
};
inline IoHomeLastCommand ioHomeDecodeLastCommand(const IoHomeFrame &frame) {
    IoHomeLastCommand out;
    // Only ordinary position/status replies. Request-derived replies must be excluded by caller.
    const uint8_t offset=frame.commandId==IoHomeCommand::PrivateResponse?8:
        frame.commandId==IoHomeCommand::StatusUpdate?11:0;
    if(!offset||frame.dataLen<offset+4)return out;
    out.node=uint32_t(frame.data[offset])<<16|uint32_t(frame.data[offset+1])<<8|frame.data[offset+2];
    out.valid=out.node!=0;out.originator=frame.data[offset+3];return out;
}
inline bool ioHomeBuildStatusMpFpProbe(IoHomeFrame &frame,uint32_t own,uint32_t target,bool lowPower) {
    if(!own||own>0xFFFFFF||target<=0x3F||target>0xFFFFFF)return false;
    frame.init();frame.setStart2W();frame.setSrcNode(own);frame.setDestNode(target);frame.setLowPower(lowPower);
    frame.commandId=IoHomeCommand::Private;
    constexpr uint8_t payload[]={1,0xFE,1,1,1,1,1,1,1,0};
    std::memcpy(frame.data,payload,sizeof(payload));frame.dataLen=sizeof(payload);return true;
}
struct IoHomeRainEvidence {
    IoHomeLastCommand lastCommand{};uint32_t lastCommandMs=0;
    bool hasRainEvidence=false,limitedByRain=false;uint32_t lastRainEvidenceMs=0,statusMs=0;
    RainLimitationRule rule=RainLimitationRule::None;RainLimitationInput input{};
    bool recent(uint32_t now) const {
        return hasRainEvidence&&uint32_t(now-lastRainEvidenceMs)<=IOHC_RAIN_EVIDENCE_HOLD_MS;
    }
    bool active(uint32_t now) const {
        return limitedByRain&&recent(now)&&uint32_t(now-statusMs)<=IOHC_RAIN_EVIDENCE_HOLD_MS;
    }
    void observe(const IoHomeLastCommand &record,RainLimitationInput observed,uint32_t now) {
        if(record.valid){lastCommand=record;lastCommandMs=now;}
        // An absent record never reuses an old rain originator as new evidence.
        observed.hasLastCommand=record.valid;observed.originator=record.originator;
        if(record.valid&&record.originator==2){hasRainEvidence=true;lastRainEvidenceMs=now;}
        else if(record.valid&&observed.stopped&&ioHomeRainTargetsMatch(observed))hasRainEvidence=false;
        if(!recent(now))hasRainEvidence=false;
        observed.rainEvidenceRecent=recent(now);input=observed;statusMs=now;
        rule=ioHomeRainLimitationRule(observed);limitedByRain=rule!=RainLimitationRule::None;
    }
};
enum class IoHomeLimitationSource:uint8_t { Unknown,ExplicitRange,ExplicitError,RainOriginator,RainClamp };
inline const char *ioHomeLimitationSourceName(IoHomeLimitationSource source) {
    switch(source){case IoHomeLimitationSource::ExplicitRange:return "explicit-range";
    case IoHomeLimitationSource::ExplicitError:return "explicit-error";
    case IoHomeLimitationSource::RainOriginator:return "rain-originator";
    case IoHomeLimitationSource::RainClamp:return "rain-clamp";default:return "unknown";}
}
struct IoHomeLimitationDecision {bool valid=false,active=false;IoHomeLimitationSource source=IoHomeLimitationSource::Unknown;};
inline IoHomeLimitationDecision ioHomeMergeLimitation(bool rangeValid,bool rangeActive,bool errorValid,
    const IoHomeRainEvidence &rain,uint32_t now) {
    if(rangeValid)return {true,rangeActive,IoHomeLimitationSource::ExplicitRange};
    if(errorValid)return {true,true,IoHomeLimitationSource::ExplicitError};
    if(rain.active(now))return {true,true,rain.rule==RainLimitationRule::RainOriginator?
        IoHomeLimitationSource::RainOriginator:IoHomeLimitationSource::RainClamp};
    return {};
}
inline bool ioHomeIsExplicitLimitationError(uint8_t code) {
    return (code>=0xE0&&code<=0xE7)||(code>=0xEA&&code<=0xEE);
}
