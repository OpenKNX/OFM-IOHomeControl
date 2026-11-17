#pragma once
#include <cstring>
#include "IoHomeCommands.h"
#include "IoHomeFrame.h"
#include <cmath>

// https://github.com/laberning/home_io_control/commit/3923fd5f9a4f54e785186f28f0c43ada7b4dd7c7
namespace IoHomeAdelis {
enum class HeatingModel:uint8_t {Unknown=0,Agilia=1,Adelis=2,AdelisMixed=3};
inline bool identity(const IoHomeProtocolIdentity &id,uint32_t node){return id.valid&&id.ioAddress==node&&id.manufacturerId==0x0C&&id.profile==52&&id.subProfile==1;}
inline bool modelName(HeatingModel model,const uint8_t *data,uint8_t length){
    const char *expected=model==HeatingModel::AdelisMixed?"ADELIS 2 M":model==HeatingModel::Adelis?"ADELIS 2 DIGIT":model==HeatingModel::Agilia?"AGILIA IO":nullptr;
    if(!expected||!data)return false;const auto size=std::strlen(expected);if(length<size||std::memcmp(data,expected,size))return false;
    for(unsigned i=size;i<length;++i)if(data[i])return false;return true;
}
struct Value {bool valid=false,candidate=false;int32_t scaled=0;const char *unit="raw";};
inline Value decode(uint16_t reg,const uint8_t *p,uint8_t len){
    if(!p)return {};
    if(reg==0x115&&len==2){const uint16_t raw=uint16_t(p[1])<<8|p[0];if(raw==0xFFFF||raw==0x7FFF||raw==0x8000)return {};
        const int16_t value=static_cast<int16_t>(raw);return value>=-4000&&value<=8000?Value{true,false,value,"hundredths_C"}:Value{};}
    if((reg==0x103||reg==0x138)&&len==2){const uint16_t raw=uint16_t(p[1])<<8|p[0];if(raw>=70&&raw<=280)return {true,false,raw,"tenths_C"};}
    if(reg==0x136&&len==1&&(p[0]==0||p[0]==2||p[0]==3))return {true,false,p[0],"adelis_mode"};
    if(reg==0x13E&&len==1&&p[0]<=2)return {true,false,p[0],"temporary_state"};
    if(reg==0x126&&len==1&&p[0]<=100)return {true,false,p[0],"working_rate_percent"};
    if(reg==0x13A&&len==1&&p[0]<=60)return {true,true,p[0],"remaining_minutes_candidate"};
    return {};
}
enum class ObservedPreset:uint8_t {Unknown,Comfort,Eco};
inline ObservedPreset observedPreset(const IoHomeFrame &f,bool qualifiedModel,bool qualifiedSession) {
    if(!qualifiedModel||!qualifiedSession||f.commandId!=IoHomeCommand::PrivateResponse||
       (f.ctrlByte0&IOHC_CTRL0_MODE_1W)||f.dataLen!=6||f.data[0]!=0x2D||f.data[1]!=0||
       f.data[2]!=0xFC||f.data[4]!=0||f.data[5]!=0)return ObservedPreset::Unknown;
    return f.data[3]==5?ObservedPreset::Comfort:f.data[3]==2?ObservedPreset::Eco:ObservedPreset::Unknown;
}
inline const char *presetName(ObservedPreset p){return p==ObservedPreset::Comfort?"comfort":p==ObservedPreset::Eco?"eco":"unknown";}
// Qualified input values only; never infer either input from candidate register 0137.
inline bool effectiveProgramTarget(ObservedPreset preset,int16_t comfortTenths,int16_t ecoOffsetTenths,
                                  bool matchingMeasuredContext,int16_t &out) {
    if(!matchingMeasuredContext||preset==ObservedPreset::Unknown||comfortTenths<70||comfortTenths>280||
       ecoOffsetTenths<0||ecoOffsetTenths>70)return false;
    const int16_t target=comfortTenths-(preset==ObservedPreset::Eco?ecoOffsetTenths:0);
    if(target<70||target>280)return false;out=target;return true;
}
inline bool readable(uint16_t reg){return reg==0x119||reg==0x115||reg==0x103||reg==0x138||reg==0x136||reg==0x13E||reg==0x126||reg==0x13A||(reg>=0x131&&reg<=0x134);}
inline uint8_t write(uint16_t reg,uint8_t value,uint8_t (&data)[6]){
    if(!((reg==0x136&&(value==0||value==2||value==3))||(reg==0x13E&&value<=2)))return 0;
    data[0]=0x0C;data[1]=0x61;data[2]=1;data[3]=reg;data[4]=value;return 5;
}
}
