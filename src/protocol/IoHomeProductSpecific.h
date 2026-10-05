#pragma once
#include "IoHomeProductCodecs.h"
// OVPd 0x1D0000: MP slat orientation, FP1 commanded movement speed.
inline bool ioHomeDecodePergola(uint8_t index,uint16_t raw,double &percent){if(index>1||raw>IOHC_POSITION_MAX)return false;percent=std::round(raw/512.0);return true;}
inline bool ioHomeBuildPergola(double orientation,double speed,uint8_t *out,uint8_t capacity,uint8_t &length){
 if(!std::isfinite(orientation)||!std::isfinite(speed)||orientation<0||orientation>100||speed<0||speed>100)return false;
 const IoHomeFpValue value{1,uint16_t(std::round(speed*512))};return ioHomeBuildActivationRepresentation(uint16_t(std::round(orientation*512)),&value,1,out,capacity,length);
}
