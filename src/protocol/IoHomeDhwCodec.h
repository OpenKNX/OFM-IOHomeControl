#pragma once
#include "IoHomeFrame.h"
#include "IoHomeMpFpRead.h"
#include <cmath>
#include <cstdint>

// https://github.com/laberning/home_io_control/commit/7a21ae215dd75c6f8c5ae1bfe3fd5304ea1a021f
// Family 00 uses BE values. Never use the shutter's byte-sized main-only target.
namespace IoHomeDhwCodec {
inline bool identity(const IoHomeProtocolIdentity &id,uint32_t node){return id.valid&&id.ioAddress==node&&id.manufacturerId==0x0C&&id.profile==51&&id.subProfile==0;}
inline bool readable(uint16_t reg){return reg==0||reg==4||reg==6||reg==7||reg==8||reg==0x17||reg==0x21;}
inline bool mode(const uint8_t *p,uint8_t len,uint8_t &out){if(!p||len!=1||p[0]>2)return false;out=p[0];return true;}
inline bool waterTemperature(const uint8_t *p,uint8_t len,int16_t &tenths){
    if(!p||len!=6)return false;const uint16_t raw=uint16_t(p[0])<<8|p[1];
    if(raw==0x7FFF||raw==0x8000||raw==0xFFFF)return false;
    const auto value=static_cast<int16_t>(raw);if(value<-400||value>1200)return false;tenths=value;return true;
}
inline bool energyWh(const uint8_t *p,uint8_t len,uint32_t &out){if(!p||len!=4)return false;out=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];return true;}
struct Heating {bool heating=false,element=false,heatPump=false;};
inline bool heating(const uint8_t *p,uint8_t len,Heating &out){
    if(!p||len!=5||p[0]>1||p[2]>1||p[3]>1||p[4]>1)return false;
    out={p[0]!=0,p[2]!=0,p[3]!=0||p[4]!=0};return true;
}
inline bool statusTarget(const IoHomeFrame &frame,uint16_t &out){
    if(frame.commandId!=IoHomeCommand::PrivateResponse||(frame.ctrlByte0&IOHC_CTRL0_MODE_1W)||frame.dataLen!=6)return false;
    const uint16_t raw=uint16_t(frame.data[2])<<8|frame.data[3];if(raw>0xC800)return false;out=raw;return true;
}
inline bool decodeTarget(uint16_t raw,float &celsius){if(raw>0xC800)return false;celsius=std::round((20.f+raw*42.f/51200.f)*10.f)/10.f;return true;}
struct ExplorerStatus {
    bool valid=false, targetKnown=false, boostKnown=false, boost=false;
    uint16_t targetTenths=0;
};
inline constexpr uint8_t ExplorerBitmap = 0x43; // FP10, FP15 and FP16
inline ExplorerStatus explorerStatus(const IoHomeFrame &frame) {
    if (frame.commandId != IoHomeCommand::PrivateResponse || (frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) ||
        !ioHomeHighFpReplyMatches(ExplorerBitmap, frame.data, frame.dataLen))
        return {};
    const uint16_t main = uint16_t(frame.data[2]) << 8 | frame.data[3];
    const uint16_t fp16 = uint16_t(frame.data[10]) << 8 | frame.data[11];
    // 0000 also encodes the minimum temperature; keep it ambiguous.
    const bool targetKnown = main != 0 && main <= 0xC800;
    const uint16_t target = targetKnown ? uint16_t(200U + (uint32_t(main) * 10U + 512U) / 1024U) : 0;
    const uint16_t boostWord = fp16 & 0x0300;
    const bool boostKnown = fp16 != 0xFFFF && (boostWord == 0x0100 || boostWord == 0x0200);
    return {true, targetKnown, boostKnown, boostKnown && boostWord == 0x0200, target};
}
inline bool encodeConfirmedTarget(float celsius,uint16_t volumeLitres,bool manual,uint16_t &raw){
    // Only these steps on the captured 200-l device have display confirmation.
    if(!manual||volumeLitres!=200||!std::isfinite(celsius)||(celsius!=50.f&&celsius!=54.f&&celsius!=62.f))return false;
    raw=uint16_t(std::lround((celsius-20.f)*51200.f/42.f));return true;
}
inline bool executePayload(uint16_t raw,uint8_t acei,uint8_t (&data)[6]){
    if(raw>0xC800)return false;data[0]=1;data[1]=acei;data[2]=raw>>8;data[3]=raw;data[4]=data[5]=0;return true;
}
inline uint8_t modeWrite(uint8_t mode,uint8_t (&data)[6]){if(mode>2)return 0;data[0]=0x0C;data[1]=0x61;data[2]=0;data[3]=6;data[4]=mode;return 5;}
// Boost is read-only until a separately qualified display/readback test exists.
struct State {
    uint32_t energy=0;uint16_t targetRaw=0,volumeLitres=0;uint8_t mode=0;
    bool modeValid=false,targetValid=false,energyValid=false,energyReset=false;
    bool visibleTarget()const{return modeValid&&mode!=0&&targetValid;}
    void observeMode(uint8_t value){mode=value;modeValid=value<=2;if(!modeValid||!mode)targetValid=false;}
    void observeEnergy(uint32_t value){energyReset=energyValid&&value<energy;energy=value;energyValid=true;}
    double energyKWh()const{return double(energy)/1000.0;}
};
}
