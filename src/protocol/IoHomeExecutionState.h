#pragma once
#include "IoHomeFrame.h"
// Qualified VELUX main-info status only; unrelated models keep their stopped bit.
namespace IoHomeExecutionState {
constexpr uint8_t Unknown=0xFF;
inline uint8_t decode(uint8_t status,const IoHomeProtocolIdentity &id) {
    if(!id.valid||id.manufacturerId!=1||!((id.profile==2&&id.subProfile==0)||(id.profile==4&&id.subProfile==1)))return Unknown;
    const uint8_t state=status&7;return state<=5?state:Unknown;
}
inline bool waiting(uint8_t state){return state==2||state==3;}
inline bool stopped(uint8_t status,const IoHomeProtocolIdentity &id) {
    const auto state=decode(status,id);
    return state==Unknown?(status&1)!=0:!(waiting(state)||state==4);
}
}
