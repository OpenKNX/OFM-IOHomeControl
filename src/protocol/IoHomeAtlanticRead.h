#pragma once
#include <cstdint>
#include "IoHomeAdelis.h"
#include "IoHomeDhwCodec.h"
#include <cstring>

// The pure build() codec remains read-only. Typed action stages are admitted only by the bound controller job.
namespace IoHomeAtlanticRead {
enum class Model:uint8_t {Raw,Calypso,AdelisClassic,HeaterWithLight,AdelisMixed,Tahual,Guelma};
enum class Result:uint8_t {Idle,Queued,Value,Empty,Unconfirmed,NoReply,Rejected,Cancelled,IdentityChanged,Failed};
struct Sample {uint16_t reg=0;Result result=Result::Idle;uint8_t length=0,data[19]{};uint32_t receivedMs=0;};
enum class Action:uint8_t {Read,AdelisWrite,DhwMode,DhwTarget};
struct State {Action action=Action::Read;uint8_t writeIndex=0xFF,desired=0,modelSelection=0;uint16_t targetRaw=0;bool readbackVerified=false,targetReadbackDiffers=false;IoHomeDhwCodec::State dhw;bool active=false;uint8_t channel=0xFF,count=0,completed=0;Model model=Model::Raw;uint32_t token=0;Sample samples[16]{};};
inline bool build(uint8_t family,uint8_t index,uint8_t data[4]) {
    if(family>1||!data)return false;
    data[0]=0x0C;data[1]=0x60;data[2]=family;data[3]=index;return true;
}
inline bool reply(uint16_t reg,const uint8_t *data,uint8_t length,Sample &out) {
    if(!data||length<4||length>23||data[0]!=0x0C||data[1]!=0x60||
       data[2]!=uint8_t(reg>>8)||data[3]!=uint8_t(reg))return false;
    Sample sample;sample.reg=reg;sample.length=length-4;
    sample.result=sample.length?Result::Value:Result::Empty;
    std::memcpy(sample.data,data+4,sample.length);out=sample;return true;
}
inline bool isAdelisClassicName(const Sample &s) {
    constexpr char model[]="ADELIS 2 DIGIT";
    if(s.reg!=0x119||s.result!=Result::Value||s.length<sizeof(model)-1||
       std::memcmp(s.data,model,sizeof(model)-1))return false;
    for(unsigned i=sizeof(model)-1;i<s.length;++i)if(s.data[i])return false;
    return true;
}
inline bool exactModelName(const Sample &s, uint16_t reg, const char *expected) {
    const size_t length = std::strlen(expected);
    if (s.reg != reg || s.result != Result::Value || s.length < length ||
        std::memcmp(s.data, expected, length) != 0)
        return false;
    for (size_t i = length; i < s.length; ++i)
        if (s.data[i] != 0 && s.data[i] != ' ') return false;
    return true;
}
inline bool guelmaReadable(uint16_t reg) {
    switch (reg) {
    case 0x0004: case 0x0006: case 0x0032: case 0x0046: case 0x0048:
    case 0x0049: case 0x004A: case 0x0052: case 0x0053:
        return true;
    default:
        return false;
    }
}
struct Value {bool known=false,candidate=false;int64_t scaled=0;const char *unit="raw";};
inline Value decode(Model model,const Sample &s) {
    if(s.result!=Result::Value)return {};
    if(model==Model::Calypso) {
        if(s.reg==6&&s.length==1&&s.data[0]<=2)return {true,false,s.data[0],"mode"};
        if(s.reg==0x17&&s.length==6) {
            int16_t value=0;if(IoHomeDhwCodec::waterTemperature(s.data,s.length,value))return {true,false,value,"tenths_C"};
        }
        if(s.reg==4&&s.length==4)return {true,false,int64_t(uint32_t(s.data[0])<<24|uint32_t(s.data[1])<<16|uint32_t(s.data[2])<<8|s.data[3]),"Wh"};
        if(s.reg==8&&s.length==2)return {true,true,int64_t(uint16_t(s.data[0])<<8|s.data[1]),"litres_candidate"};
    }
    // Explicit diagnostic model only; discovery 52/1 never selects this and
    // these candidate observations grant no heating/light writer or KNX status.
    if(model==Model::HeaterWithLight) {
        // The detection switch and the raw occupancy are independent readings.
        if (s.length == 1 && s.data[0] <= 1) {
            if (s.reg == 0x0101) return {true, false, s.data[0], "occupancy_detection_enabled"};
            if (s.reg == 0x0110) return {true, false, s.data[0], "occupancy_detected"};
        }
        if(s.reg==0x144&&s.length==1&&s.data[0]<=1)return {true,true,s.data[0],"night_light_on_candidate"};
        if(s.reg==0x142&&s.length==2) {
            const uint16_t raw=uint16_t(s.data[1])<<8|s.data[0];
            if(raw<=10000)return {true,true,raw,"brightness_raw_candidate"};
        }
    }
    if (model == Model::Guelma) {
        const uint16_t raw = uint16_t(s.data[0]) << 8 | s.data[1];
        uint32_t wh = 0;
        if (s.reg == 0x0004 && IoHomeDhwCodec::energyWh(s.data, s.length, wh))
            return {true, false, wh, "Wh"};
        if (s.reg == 0x0032 && s.length == 2)
            return {true, false, raw, "W"};
        if (s.reg == 0x0049 && s.length == 2 && raw >= 70 && raw <= 700)
            return {true, false, raw, "tenths_C"};
        if (s.length == 1) {
            if (s.reg == 0x0006 && (s.data[0] == 0 || s.data[0] == 2)) return {true, false, s.data[0] == 2, "manual"};
            if (s.reg == 0x0052 && s.data[0] <= 1) return {true, false, s.data[0], "boost"};
            if (s.reg == 0x0053 && s.data[0] <= 1) return {true, false, s.data[0], "absence"};
            if (s.reg == 0x0046 && s.data[0] <= 4) return {true, false, s.data[0], "showers_available"};
            if (s.reg == 0x0048 && s.data[0] >= 2 && s.data[0] <= 4) return {true, false, s.data[0], "manual_showers"};
            if (s.reg == 0x004A && s.data[0] <= 4) return {true, false, s.data[0], "showers_target"};
        }
    }
    if (model == Model::Tahual && s.reg == 0x0116 && s.length == 2)
        return {true, false, uint16_t(s.data[0]) * 256 + s.data[1], "kWh_raw_counter"};
    if(model==Model::AdelisMixed&&s.reg==0x153&&s.length==1&&s.data[0]<=1)
        return {true,false,s.data[0],"central_heating_selected"};
    if(model==Model::AdelisClassic){const auto value=IoHomeAdelis::decode(s.reg,s.data,s.length);return {value.valid,value.candidate,value.scaled,value.unit};}

    return {};
}
}
