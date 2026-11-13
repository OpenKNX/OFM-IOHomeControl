#pragma once
#include "IoHomeCommands.h"
// Thermor PASS_Actuator, issue 228. Observation codec only; no execute builder.
namespace IoHomePilotWire {
inline bool identity(const IoHomeProtocolIdentity &id) {
    return id.valid&&id.manufacturerId==0x0C&&id.profile==0x34&&id.subProfile==0;
}
inline bool observedExecuteMode(const uint8_t *body,uint8_t length,uint8_t &mode) {
    if(!body||length!=8||body[0]!=1||body[1]!=0xE7||body[2]!=0xFC||body[3]>5||
       body[4]!=0||body[5]!=0x20||body[6]!=0xFF||body[7]!=0xFF)return false;
    mode=body[3];return true;
}
inline const char *name(uint8_t mode) {
    static constexpr const char *names[]={"off","frost","eco","comfort_minus_2","comfort_minus_1","comfort"};
    return mode<6?names[mode]:"unknown";
}
}
