#pragma once
#include <cstdint>
// First-trigger metadata only; no RF payload, key, challenge or MAC storage.
struct IoHomeCollisionEvidence {
    uint64_t localTransaction=0;
    uint32_t boot=0,session=0,timeMs=0,own=0,src=0,dst=0,frequency=0,rxReadUs=0;
    int16_t rssi=0;uint16_t irq=0;
    uint8_t length=0,command=0,ctrl0=0,ctrl1=0,radioState=0,controllerState=0;
    bool valid=false,phyRx=false,crcChecked=false,crcValid=false,fifoOverrun=false,truncated=false,recentOwnTx=false,softwarePhy=false;
};
static_assert(sizeof(IoHomeCollisionEvidence)<=64,"Keep immutable collision evidence compact");
