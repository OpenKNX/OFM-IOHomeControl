#pragma once
#include <stdint.h>

// Offline preparation only. No TX API or receiver trust is granted by this plan.
// Source: laberning/home_io_control components/home_io_control/proto_commands.cpp
// https://github.com/laberning/home_io_control/issues/156#issuecomment-6640070627
inline bool ioHomeBuildLightSubscriptionPlan(uint16_t event,uint8_t action,uint8_t (&data)[3])
{
    // Battery 2001 and experimental unsubscribe deliberately remain unavailable.
    if(event!=0x2005||action!=1)return false;
    data[0]=1;data[1]=0x20;data[2]=5;return true;
}
