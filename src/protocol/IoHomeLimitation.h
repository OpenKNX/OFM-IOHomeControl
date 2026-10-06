#pragma once
#include "IoHomeFrame.h"
#include <cstring>

// Captures: laberning/home_io_control 215457eb51730429b75a93ef5af29fb45a018b5e.
// Five-byte response fields and timer remain provisional pending active-limit RF.
enum class IoHomeLimitationType : uint8_t { Minimum=0x80, Maximum=0xC0 };
constexpr uint8_t IOHC_LIMITATION_PARAM_MP=0, IOHC_LIMITATION_PARAM_FP_FIRST=1,
    IOHC_LIMITATION_PARAM_FP_LAST=16, IOHC_LIMITATION_REQUEST_SIZE=3, IOHC_LIMITATION_RESPONSE_SIZE=5;
constexpr uint16_t IOHC_LIMITATION_UNRESTRICTED_MAX=0xC800;
constexpr uint32_t IOHC_LIMITATION_PAIR_MS=5000, IOHC_LIMITATION_FRESH_MS=300000;
inline bool ioHomeBuildLimitationStatusRequest(IoHomeFrame &frame,uint32_t own,uint32_t target,
    IoHomeLimitationType type,uint8_t parameter,bool lowPower) {
    if(!own||own>0xFFFFFF||target<=0x3F||target>0xFFFFFF||parameter!=0||
       (type!=IoHomeLimitationType::Minimum&&type!=IoHomeLimitationType::Maximum))return false;
    frame.init();frame.setStart2W();frame.setSrcNode(own);frame.setDestNode(target);
    frame.setLowPower(lowPower);frame.commandId=IoHomeCommand::LimitationStatusRequest;
    frame.data[0]=uint8_t(type);frame.data[1]=parameter;frame.data[2]=0;frame.dataLen=3;
    return true;
}
struct IoHomeLimitationStatus {
    bool valid=false;uint8_t parameterId=0;uint16_t valueRaw=0;uint8_t originator=0,timeRaw=0;
    uint8_t raw[5]{};
    bool normalValue() const {return valid&&valueRaw<=IOHC_LIMITATION_UNRESTRICTED_MAX;}
    float percent() const {return normalValue()?float(valueRaw)*100/IOHC_LIMITATION_UNRESTRICTED_MAX:-1;}
};
inline bool ioHomeDecodeLimitationStatus(const IoHomeFrame &frame,IoHomeLimitationStatus &out) {
    out={};if(frame.commandId!=IoHomeCommand::LimitationStatusResponse||frame.dataLen!=5)return false;
    out.valid=true;out.parameterId=frame.data[0];out.valueRaw=uint16_t(frame.data[1])<<8|frame.data[2];
    out.originator=frame.data[3];out.timeRaw=frame.data[4];std::memcpy(out.raw,frame.data,5);return true;
}
inline const char *ioHomeOriginatorName(uint8_t v) {
    switch(v) {
    case 0:return "local-user";case 1:return "user";case 2:return "rain";case 3:return "timer";
    case 4:return "security";case 5:return "ups";case 6:return "sfc";case 7:return "lsc";
    case 8:return "saac";case 9:return "wind";case 0x0B:return "external-access";
    case 0x0C:return "local-light";case 0x0D:return "environment-sensor";case 0x10:return "myself";
    case 0xC8:return "unknown";case 0xFE:return "automatic-cycle";case 0xFF:return "emergency";
    default:return "unmapped";
    }
}
struct IoHomeLimitationTimer {
    enum class Kind:uint8_t { Seconds,Unlimited,Special };
    Kind kind;uint16_t seconds;
};
// KLF/API-derived; never used for KNX state, cache expiry or RF scheduling.
inline IoHomeLimitationTimer ioHomeDecodeLimitationTimer(uint8_t raw) {
    return raw<=252?IoHomeLimitationTimer{IoHomeLimitationTimer::Kind::Seconds,uint16_t((raw+1)*30)}:
      IoHomeLimitationTimer{raw==253?IoHomeLimitationTimer::Kind::Unlimited:IoHomeLimitationTimer::Kind::Special,0};
}
struct IoHomeLimitationSnapshot {
    bool minimumValid=false,maximumValid=false,coherent=false,limitationActive=false;
    IoHomeLimitationStatus minimum{},maximum{};
    uint32_t minimumTimestampMs=0,maximumTimestampMs=0,node=0,revision=0,refreshToken=0;
    void begin(uint32_t n,uint32_t r,uint32_t token) {*this={};node=n;revision=r;refreshToken=token;}
    void accept(IoHomeLimitationType type,const IoHomeLimitationStatus &sample,uint32_t now) {
        if(type==IoHomeLimitationType::Minimum){minimum=sample;minimumValid=sample.valid;minimumTimestampMs=now;}
        else {maximum=sample;maximumValid=sample.valid;maximumTimestampMs=now;}
        coherent=minimumValid&&maximumValid&&minimum.parameterId==0&&maximum.parameterId==0&&
            minimum.normalValue()&&maximum.normalValue()&&minimum.valueRaw<=maximum.valueRaw&&
            uint32_t(maximumTimestampMs-minimumTimestampMs)<=IOHC_LIMITATION_PAIR_MS;
        limitationActive=coherent&&(minimum.valueRaw>0||maximum.valueRaw<IOHC_LIMITATION_UNRESTRICTED_MAX);
    }
    bool fresh(uint32_t now,uint32_t n,uint32_t r) const {
        return coherent&&n==node&&r==revision&&refreshToken&&
          uint32_t(now-minimumTimestampMs)<IOHC_LIMITATION_FRESH_MS&&
          uint32_t(now-maximumTimestampMs)<IOHC_LIMITATION_FRESH_MS;
    }
};
struct IoHomeLimitationPublication {
    enum class Action:uint8_t { Invalid,Cache,Transmit };
    bool published=false,last=false;uint32_t node=0,revision=0;
    Action update(const IoHomeLimitationSnapshot &s,bool valid) {
        if(node!=s.node||revision!=s.revision){published=false;node=s.node;revision=s.revision;}
        if(!valid)return Action::Invalid;
        const bool send=!published||last!=s.limitationActive;
        published=true;last=s.limitationActive;return send?Action::Transmit:Action::Cache;
    }
};
