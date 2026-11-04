#pragma once
#include <cstdint>
#include <cstring>

// Read observations only. No request accepted here can encode 0C 61.
namespace IoHomeAtlanticRead {
enum class Model:uint8_t {Raw,Calypso,AdelisClassic};
enum class Result:uint8_t {Idle,Queued,Value,Empty,Unconfirmed,NoReply,Rejected,Cancelled,IdentityChanged,Failed};
struct Sample {uint16_t reg=0;Result result=Result::Idle;uint8_t length=0,data[19]{};uint32_t receivedMs=0;};
struct State {bool active=false;uint8_t channel=0xFF,count=0,completed=0;Model model=Model::Raw;uint32_t token=0;Sample samples[16]{};};
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
struct Value {bool known=false,candidate=false;int64_t scaled=0;const char *unit="raw";};
inline Value decode(Model model,const Sample &s) {
    if(s.result!=Result::Value)return {};
    if(model==Model::Calypso) {
        if(s.reg==6&&s.length==1&&s.data[0]<=2)return {true,false,s.data[0],"mode"};
        if(s.reg==0x17&&s.length==6) {
            const uint16_t raw=uint16_t(s.data[0])<<8|s.data[1];
            if(raw<0x7FFF)return {true,false,raw,"tenths_C"};
        }
        if(s.reg==4&&s.length==4)return {true,false,int64_t(uint32_t(s.data[0])<<24|uint32_t(s.data[1])<<16|uint32_t(s.data[2])<<8|s.data[3]),"Wh"};
        if(s.reg==8&&s.length==2)return {true,true,int64_t(uint16_t(s.data[0])<<8|s.data[1]),"litres_candidate"};
    }
    // Adelis physical values/profile still await independent display/identity proof.
    if(model==Model::AdelisClassic) {
        if((s.reg==0x115||s.reg==0x137)&&s.length==2)
            return {true,true,int64_t(uint16_t(s.data[1])<<8|s.data[0]),s.reg==0x115?"hundredths_C_candidate":"tenths_C_candidate"};
        if(s.reg==0x136&&s.length==1)return {true,true,s.data[0],"mode_candidate"};
    }
    return {};
}
}
