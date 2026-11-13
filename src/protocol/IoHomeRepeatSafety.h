#pragma once
#include <cstdint>
// Only explicitly repeat-safe movement forms. Unknown selectors stay single try.
inline bool ioHomeRepeatSafePercentExecute(uint8_t parameter){return parameter<=100||parameter==0xD2;}
inline bool ioHomeRepeatSafeRawExecute(const uint8_t *body,uint8_t length){
    if(!body||(length!=6&&length!=8)||body[0]!=1)return false;
    const uint16_t main=(uint16_t(body[2])<<8)|body[3];
    return main<=0xC800||main==0xD200;
}
