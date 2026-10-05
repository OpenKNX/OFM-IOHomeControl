#pragma once
#include <cstdint>
#include <cstddef>

// OVPd getMpFp (0x0016D0F0): individual GET, not the refresh-info
// representation or a PUT endpoint. Index zero selects MP.
inline bool ioHomeBuildMpFpRead(uint8_t index,uint8_t *data,uint8_t &length) {
    length=0;
    if(!data||index>16)return false;
    data[0]=3;data[1]=index>=1&&index<=8?uint8_t(0x80>>(index-1)):0;
    data[2]=index>=9?uint8_t(0x80>>(index-9)):0;length=3;return true;
}
struct IoHomeMpFpReply {
    uint8_t basicInfo=0,detailedStatus=0,mainInfo=0;
    uint16_t target=0,current=0,remainingSeconds=0,values[16]{};
    uint32_t lastMaster=0;
    uint16_t present=0; // bit zero = FP1; independent of wire mask order
};
// Standard 04 reply: fixed twelve bytes followed by two sparse groups.
// Parse transactionally: truncated/extra tails never leak partial state.
inline bool ioHomeDecodeMpFpReply(const uint8_t *data,size_t length,IoHomeMpFpReply &out) {
    if(!data||length<14)return false;
    IoHomeMpFpReply result;
    result.basicInfo=data[0];result.detailedStatus=data[1];result.mainInfo=data[11];
    result.target=uint16_t(data[2])<<8|data[3];result.current=uint16_t(data[4])<<8|data[5];
    result.remainingSeconds=uint16_t(data[6])<<8|data[7];
    result.lastMaster=uint32_t(data[8])<<16|uint32_t(data[9])<<8|data[10];
    size_t cursor=12;
    for(uint8_t group=0;group<2;group++) {
        if(cursor>=length)return false;
        const uint8_t mask=data[cursor++];
        for(uint8_t bit=0;bit<8;bit++)if(mask&(0x80>>bit)) {
            if(length-cursor<2)return false;
            const uint8_t index=group*8+bit;
            result.values[index]=uint16_t(data[cursor])<<8|data[cursor+1];cursor+=2;
            result.present|=uint16_t(1)<<index;
        }
    }
    if(cursor!=length)return false;
    out=result;return true;
}
