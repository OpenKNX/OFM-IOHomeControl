#pragma once
#include <cstdint>

// First observation per receiver epoch. Sticky IRQs cannot renew a deadline.
// Unsigned subtraction covers micros wrap; a sync edge gets its own grace.
struct IoHomeRxHold {
    uint32_t preambleAt=0,syncAt=0;
    bool preambleSeen=false,syncSeen=false;
    void reset(){*this={};}
    static constexpr uint32_t preambleUs(uint16_t bytes) {
        return (uint64_t(bytes>1960?1960:bytes)*8000000U+38399U)/38400U+40000U;
    }
    bool hold(uint32_t now,bool preamble,bool sync,uint32_t maxPreambleUs,uint32_t syncGraceUs=20000) {
        if(sync) {
            if(!syncSeen){syncSeen=true;syncAt=now;}
            return uint32_t(now-syncAt)<syncGraceUs;
        }
        // A sticky preamble after a completed/expired sync is never fresh.
        if(syncSeen)return false;
        if(!preamble){preambleSeen=false;return false;}
        if(!preambleSeen){preambleSeen=true;preambleAt=now;}
        return uint32_t(now-preambleAt)<maxPreambleUs;
    }
};
static_assert(IoHomeRxHold::preambleUs(1960)>408333,"1960-byte wake preamble must fit");
