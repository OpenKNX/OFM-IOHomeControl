#pragma once
#include <cstdint>
#include "IoHomeAirtime.h"

// First observation per receiver epoch. Sticky IRQs cannot renew a deadline.
// Unsigned subtraction covers micros wrap; a sync edge gets its own grace.
struct IoHomeRxHold {
    enum class Disposition:uint8_t {Idle,Preamble,Sync,PreambleExpired,SyncExpired};
    Disposition disposition=Disposition::Idle;
    uint32_t preambleAt=0,syncAt=0;
    bool preambleSeen=false,syncSeen=false;
    void reset(){*this={};}
    static const char *name(Disposition d){return d==Disposition::Preamble?"preamble_hold":d==Disposition::Sync?"sync_grace":d==Disposition::PreambleExpired?"preamble_no_sync_expired":d==Disposition::SyncExpired?"sync_no_packet_expired":"idle";}
    bool result(Disposition d,bool held){disposition=d;return held;}
    static uint32_t preambleUs(uint16_t bytes) {
        return ioHomeAirtime(0,bytes>8191?8191:bytes).preambleUs+40000U;
    }
    bool hold(uint32_t now,bool preamble,bool sync,uint32_t maxPreambleUs,uint32_t syncGraceUs=20000) {
        if(sync) {
            if(!syncSeen){syncSeen=true;syncAt=now;}
            const bool held=uint32_t(now-syncAt)<syncGraceUs;return result(held?Disposition::Sync:Disposition::SyncExpired,held);
        }
        // A sticky preamble after a completed/expired sync is never fresh.
        if(syncSeen)return result(Disposition::SyncExpired,false);
        if(!preamble){preambleSeen=false;return result(Disposition::Idle,false);}
        if(!preambleSeen){preambleSeen=true;preambleAt=now;}
        const bool held=uint32_t(now-preambleAt)<maxPreambleUs;return result(held?Disposition::Preamble:Disposition::PreambleExpired,held);
    }
};

