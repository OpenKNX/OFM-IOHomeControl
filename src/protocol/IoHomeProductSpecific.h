#pragma once
#include "IoHomeProductCodecs.h"
// OVPd 0x1D0000: MP slat orientation, FP1 commanded movement speed.
inline bool ioHomeDecodePergola(uint8_t index,uint16_t raw,double &percent){if(index>1||raw>IOHC_POSITION_MAX)return false;percent=std::round(raw/512.0);return true;}
inline bool ioHomeBuildPergola(double orientation,double speed,uint8_t *out,uint8_t capacity,uint8_t &length){
 if(!std::isfinite(orientation)||!std::isfinite(speed)||orientation<0||orientation>100||speed<0||speed>100)return false;
 const IoHomeFpValue value{1,uint16_t(std::round(speed*512))};return ioHomeBuildActivationRepresentation(uint16_t(std::round(orientation*512)),&value,1,out,capacity,length);
}

// Alarm 0x170000 only: three independent zone pairs, not percentage scaling.
inline bool ioHomeDecodeAlarmZones(uint16_t raw,uint8_t &zones){
 if(raw==0){zones=0;return true;}if(raw==0xC800){zones=7;return true;}if(raw<=0xF800)return false;
 switch(raw&0x3F){case 0:zones=0;break;case 3:zones=1;break;case 12:zones=2;break;case 15:zones=3;break;case 48:zones=4;break;case 51:zones=5;break;case 60:zones=6;break;case 63:zones=7;break;default:return false;}return true;
}
struct IoHomeSlidingLock {uint16_t raw=0;bool sourceSaysLocked=false,known=false;};
inline IoHomeSlidingLock ioHomeDecodeSlidingLock(uint16_t raw){
 // Retained 0x1B0102 converter treats every non-C800 word as unlocked.
 // Preserve that literal result, but qualify only the ordinary endpoints.
 return {raw,raw==0xC800,raw==0||raw==0xC800};
}
