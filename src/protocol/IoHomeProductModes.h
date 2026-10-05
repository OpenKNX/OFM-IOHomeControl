#pragma once
#include <stdint.h>
#include <cmath>
#include "IoHomeCommands.h"

// Explicit product semantics only; these codecs never enqueue RF or grant
// publication/write permission. FP numbers are reused by unrelated products.
enum class IoHomeHeatingLevel : uint16_t {
    Off=0xFC00,FrostProtection=0xFC01,Eco=0xFC02,ComfortMinus2=0xFC03,
    ComfortMinus1=0xFC04,Comfort=0xFC05,Boost=0xFC07,Secured=0xFC3F
};
inline bool ioHomeDecodeHeatingLevel(uint16_t raw,IoHomeHeatingLevel &out) {
    switch(raw) {
    case 0xFC00:case 0xFC01:case 0xFC02:case 0xFC03:case 0xFC04:case 0xFC05:case 0xFC07:case 0xFC3F:
        out=static_cast<IoHomeHeatingLevel>(raw);return true;
    default:return false;
    }
}
inline const char *ioHomeHeatingLevelName(IoHomeHeatingLevel value) {
    switch(value) {
    case IoHomeHeatingLevel::Off:return "off";
    case IoHomeHeatingLevel::FrostProtection:return "frost protection";
    case IoHomeHeatingLevel::Eco:return "eco";
    case IoHomeHeatingLevel::ComfortMinus2:return "comfort minus 2";
    case IoHomeHeatingLevel::ComfortMinus1:return "comfort minus 1";
    case IoHomeHeatingLevel::Comfort:return "comfort";
    case IoHomeHeatingLevel::Boost:return "boost";
    case IoHomeHeatingLevel::Secured:return "secured";
    default:return "unknown";
    }
}
inline bool ioHomeEncodeHeatingLevel(IoHomeHeatingLevel value,uint16_t &out) {
    IoHomeHeatingLevel checked;if(!ioHomeDecodeHeatingLevel(static_cast<uint16_t>(value),checked))return false;
    out=static_cast<uint16_t>(checked);return true;
}

// Heat-pump 00/11 are unhandled. Atlantic's 00/11 have DIFFERENT meanings.
enum class IoHomeHeatPumpPair : uint8_t { Unhandled00,On,Off,Unhandled11 };
enum class IoHomeAtlanticPair : uint8_t { KeepCurrent,On,Off,NotUsed };
struct IoHomeHeatPumpModes {
    uint16_t rawCapabilities=0,rawModes=0,uninterpretedCapabilities=0,uninterpretedModes=0;
    bool heating=false,cooling=false,away=false,pool=false,dhw=false;
    IoHomeHeatPumpPair global=IoHomeHeatPumpPair::Unhandled00;
    IoHomeHeatPumpPair modes[5]{}; // heating, cooling, away, pool, DHW
};
inline IoHomeHeatPumpModes ioHomeDecodeHeatPumpModes(uint16_t capabilities,uint16_t modes) {
    IoHomeHeatPumpModes out;out.rawCapabilities=capabilities;out.rawModes=modes;
    out.uninterpretedCapabilities=capabilities&~uint16_t(0x001F);out.uninterpretedModes=modes&~uint16_t(0xC3FF);
    out.heating=capabilities&1;out.cooling=capabilities&2;out.away=capabilities&4;out.pool=capabilities&8;out.dhw=capabilities&16;
    out.global=static_cast<IoHomeHeatPumpPair>((modes>>14)&3);
    for(uint8_t i=0;i<5;i++)out.modes[i]=static_cast<IoHomeHeatPumpPair>((modes>>(i*2))&3);
    return out;
}
struct IoHomeAtlanticDhwModes {
    uint16_t rawCapabilities=0,rawModes=0,uninterpretedCapabilities=0,uninterpretedModes=0;
    bool rateManagement=false,absence=false,relaunch=false,energyDemand=false;
    IoHomeAtlanticPair relaunchMode=IoHomeAtlanticPair::KeepCurrent,absenceMode=IoHomeAtlanticPair::KeepCurrent;
};
inline IoHomeAtlanticDhwModes ioHomeDecodeAtlanticDhwModes(uint16_t capabilities,uint16_t modes) {
    IoHomeAtlanticDhwModes out;out.rawCapabilities=capabilities;out.rawModes=modes;
    out.uninterpretedCapabilities=capabilities&~uint16_t(0x800D);out.uninterpretedModes=modes&~uint16_t(0x0F00);
    out.rateManagement=capabilities&1;out.absence=capabilities&4;out.relaunch=capabilities&8;out.energyDemand=capabilities&0x8000;
    out.relaunchMode=static_cast<IoHomeAtlanticPair>((modes>>8)&3);out.absenceMode=static_cast<IoHomeAtlanticPair>((modes>>10)&3);
    return out; // common 0x4000 stays uninterpreted
}
enum class IoHomeAtlanticRate : uint8_t { No,Wanted,Recommended,Unsuitable,Forbidden };
inline bool ioHomeDecodeAtlanticRate(uint16_t raw,IoHomeAtlanticRate &out) {
    switch(raw) {
    case 0xFC00:out=IoHomeAtlanticRate::No;return true;
    case 0xFC01:out=IoHomeAtlanticRate::Wanted;return true;
    case 0xFC02:out=IoHomeAtlanticRate::Recommended;return true;
    case 0xFC04:out=IoHomeAtlanticRate::Unsuitable;return true;
    case 0xFC05:out=IoHomeAtlanticRate::Forbidden;return true;
    default:return false;
    }
}

struct IoHomeSirenSequence {
    uint16_t durationUnits=0,repetitions=0; // 100 ms; 0 pattern, 0x7FF default; 0x3FF unlimited repetitions
    uint8_t dutyCycleRaw=0,volume=0,visual=0; // duty 0..31, names only for recovered option codes
    bool defaultDuration=false,defaultPattern=false,unlimited=false,knownVolume=false,knownVisual=false;
};
inline bool ioHomeKnownSirenVolume(uint8_t v){return v==0||v==3||v==5||v==6||v==7;}
inline bool ioHomeKnownSirenVisual(uint8_t v){return v<=3||v==7;}
inline IoHomeSirenSequence ioHomeDecodeSirenSequence(uint16_t sound,uint16_t options) {
    IoHomeSirenSequence out;out.durationUnits=sound&0x7FF;out.dutyCycleRaw=sound>>11;
    out.repetitions=options&0x3FF;out.volume=(options>>10)&7;out.visual=options>>13;
    out.defaultDuration=out.durationUnits==0x7FF;out.defaultPattern=out.durationUnits==0;
    out.unlimited=out.repetitions==0x3FF;out.knownVolume=ioHomeKnownSirenVolume(out.volume);out.knownVisual=ioHomeKnownSirenVisual(out.visual);
    return out;
}
inline bool ioHomeEncodeSirenSequence(const IoHomeSirenSequence &value,uint16_t &sound,uint16_t &options) {
    if(value.durationUnits>0x7FF||value.dutyCycleRaw>31||value.repetitions>0x3FF||
        !ioHomeKnownSirenVolume(value.volume)||!ioHomeKnownSirenVisual(value.visual)||
        (!value.durationUnits&&value.dutyCycleRaw))return false;
    sound=value.durationUnits|(uint16_t(value.dutyCycleRaw)<<11);
    options=value.repetitions|(uint16_t(value.volume)<<10)|(uint16_t(value.visual)<<13);return true;
}
inline bool ioHomeSirenDurationFromMs(double ms,uint16_t &out) {
    if(!std::isfinite(ms)||ms<100||ms>204600)return false;
    out=static_cast<uint16_t>(std::round(ms/100));return true;
}
// Retained scaleChange rounds to one decimal before bit.lshift. This explicit
// positive truncation policy models fractional coercion, not measured RF.
inline bool ioHomeSirenDutyFromPercent(double percent,uint8_t &out) {
    if(!std::isfinite(percent)||percent<0||percent>100)return false;
    out=static_cast<uint8_t>(std::round(percent*3.1)/10);return true;
}
inline bool ioHomeBuildSirenRepresentation(const IoHomeSirenSequence *sequences,uint8_t count,
    uint8_t *out,uint8_t capacity,uint8_t &length) {
    if(!sequences||count<1||count>3||!out)return false;
    IoHomeFpValue values[6]{};
    for(uint8_t i=0;i<3;i++) {
        uint16_t sound=0,options=0;
        if(i<count&&!ioHomeEncodeSirenSequence(sequences[i],sound,options))return false;
        values[2*i]={uint8_t(9+2*i),sound};values[2*i+1]={uint8_t(10+2*i),options};
    }
    return ioHomeBuildActivationRepresentation(0,values,6,out,capacity,length);
}

// Retained HeatPump mode activation only; not RF permission.
enum class IoHomeHeatPumpTarget:uint8_t {Comfort,Setback,Eco,Halted,Off};
inline bool ioHomeBuildHeatPumpMode(IoHomeHeatPumpTarget target,uint8_t *out,uint8_t capacity,uint8_t &length){
 uint16_t mp=0,mode=0x4000;
 switch(target){case IoHomeHeatPumpTarget::Comfort:mp=0xD80F;break;case IoHomeHeatPumpTarget::Setback:mp=0xD80E;break;case IoHomeHeatPumpTarget::Eco:mp=0xD812;break;case IoHomeHeatPumpTarget::Halted:mp=0xD813;break;case IoHomeHeatPumpTarget::Off:mp=0xD400;mode=0x8000;break;default:return false;}
 const IoHomeFpValue value{16,mode};return ioHomeBuildActivationRepresentation(mp,&value,1,out,capacity,length);
}

inline bool ioHomeBuildAtlanticDhwMode(bool absence,bool relaunch,uint8_t *out,uint8_t capacity,uint8_t &length){
 if(absence&&relaunch)return false; // no recovered producer for both on
 const uint16_t mode=absence?0x4600:relaunch?0x4900:0x4A00;
 const IoHomeFpValue value{16,mode}; // common 4000 remains opaque, preserved exactly
 return ioHomeBuildActivationRepresentation(0xD400,&value,1,out,capacity,length);
}

enum class IoHomeAtlanticVentilation:uint16_t {Standard=0xFC00,Comfort=0xFC01,Eco=0xFC02};
inline bool ioHomeDecodeAtlanticVentilation(uint16_t raw,IoHomeAtlanticVentilation &out){if(raw<0xFC00||raw>0xFC02)return false;out=IoHomeAtlanticVentilation(raw);return true;}
inline bool ioHomeBuildAtlanticVentilation(IoHomeAtlanticVentilation mode,uint8_t *out,uint8_t capacity,uint8_t &length){
 IoHomeAtlanticVentilation checked;if(!ioHomeDecodeAtlanticVentilation(uint16_t(mode),checked))return false;
 const IoHomeFpValue value{16,uint16_t(mode)};return ioHomeBuildActivationRepresentation(0xD400,&value,1,out,capacity,length);
}
