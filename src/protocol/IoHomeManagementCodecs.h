#pragma once
#include <stdint.h>
#include <cstring>

struct IoHomePriorityState {
    uint8_t rawTime=0,originator=0,priority=0,uninterpretedByte0=0;
    uint16_t seconds=0;
    bool refreshEnabled=false;
};
inline bool ioHomeDecodePriority(const uint8_t *p,uint8_t size,uint8_t priority,IoHomePriorityState &out) {
    if(!p||size<3||priority>7)return false;
    IoHomePriorityState s;s.uninterpretedByte0=p[0];s.rawTime=p[1];s.originator=p[2];s.priority=priority;
    s.seconds=p[1]?uint16_t(p[1])*30:30;s.refreshEnabled=p[1]!=0xFF;out=s;return true;
}
struct IoHomeSensorStatus {
    uint8_t status=0,scaleCode=0,uninterpretedByte0=0,uninterpretedByte2=0;
    uint16_t raw=0;double value=0;
};
inline bool ioHomeDecodeSensorStatus(const uint8_t *p,uint8_t size,IoHomeSensorStatus &out) {
    if(!p||size<6)return false;
    int exponent=p[3]<=7?p[3]:p[3]>=0xF9?int(p[3])-256:99;
    if(exponent==99)return false;
    double divisor=1;
    for(int i=0;i<exponent;i++)divisor*=10;
    for(int i=0;i>exponent;i--)divisor/=10;
    IoHomeSensorStatus s;s.status=p[1]&3;s.scaleCode=p[3];s.raw=uint16_t(p[4])<<8|p[5];
    s.value=s.raw/divisor;s.uninterpretedByte0=p[0];s.uninterpretedByte2=p[2];out=s;return true;
}
struct IoHomeSensorInformation {
    uint8_t manufacturer=0,type=0,parameterType=0,scale=0,resolution=0,resolutionScale=0,mode=0,maxEvents=0;
    uint16_t minimum=0,maximum=0,minRefresh=0;uint32_t backbone=0;
};
inline bool ioHomeDecodeSensorInformation(const uint8_t *p,uint8_t size,IoHomeSensorInformation &out) {
    if(!p||size!=17)return false;
    IoHomeSensorInformation s;s.manufacturer=p[0];s.type=p[1];s.parameterType=p[2];
    s.minimum=uint16_t(p[3])<<8|p[4];s.maximum=uint16_t(p[5])<<8|p[6];s.scale=p[7];s.resolution=p[8];
    s.resolutionScale=p[9];s.mode=p[10];s.maxEvents=p[11];s.minRefresh=uint16_t(p[12])<<8|p[13];
    s.backbone=uint32_t(p[14])<<16|uint32_t(p[15])<<8|p[16];out=s;return true;
}
