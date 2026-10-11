#pragma once
#include <cstdint>

// Two complete aligned buckets upper-bound EVERY rolling one-hour interval.
// Retaining the whole previous bucket is intentionally more restrictive than
// an exact rolling ledger. No refunds on abort/lost TX_DONE. Recovery is owned
// by IoHomeRfAccessPolicy; accounting remains active during debug bypass.
class IoHomeDutyBudget {
public:
    static constexpr uint32_t windowMs = 3600000U;
    // Include the maximum admitted packet duration in the bucket period so
    // expiry cannot drop a packet tail spanning the old bucket boundary.
    static constexpr uint32_t bucketMs = windowMs + 36000U;
    static uint8_t band(uint32_t hz) {
        if (hz >= 868000000U && hz <= 868600000U) return 0;
        if (hz >= 868700000U && hz <= 869200000U) return 1;
        if (hz >= 869700000U && hz <= 870000000U) return 2;
        return 3; // unsupported emission is denied
    }
    static uint32_t allowance(uint8_t b) { return b==1 ? 3600U : b<3 ? 36000U : 0U; }
    void advance(uint32_t now) {
        if (!initialized) { start=now; initialized=true; }
        const uint32_t periods = uint32_t(now-start)/bucketMs;
        if (!periods) return;
        for (unsigned b=0;b<3;++b) { previous[b]=periods==1?current[b]:0; current[b]=0; }
        start += periods*bucketMs;
    }
    uint32_t used(uint32_t hz) const {
        const uint8_t b=band(hz);
        return b<3 ? saturatedAdd(current[b],previous[b]) : UINT32_MAX;
    }
    bool admits(uint32_t hz, uint32_t nextMs) const {
        const uint8_t b=band(hz); const uint32_t limit=allowance(b);
        if (b>=3 || nextMs>limit) return false;
        const uint32_t total=used(hz);
        return total<=limit && nextMs<=limit-total; // no addition overflow
    }
    void charge(uint32_t hz, uint32_t ms) {
        const uint8_t b=band(hz);
        if (b<3) current[b]=saturatedAdd(current[b],ms); // every accepted TX, including debug
    }
    uint32_t currentUsed(uint8_t b) const { return b<3?current[b]:UINT32_MAX; }
    uint32_t previousUsed(uint8_t b) const { return b<3?previous[b]:UINT32_MAX; }
    uint32_t retryAfter(uint32_t hz,uint32_t nextMs,uint32_t now) const {
        const uint8_t b=band(hz);const uint32_t limit=allowance(b);
        if(b>=3||nextMs>limit)return UINT32_MAX;
        if(admits(hz,nextMs))return 0;
        const uint32_t remaining=bucketMs-uint32_t(now-start);
        return current[b]<=limit && nextMs<=limit-current[b]?remaining:remaining+bucketMs;
    }
private:
    static uint32_t saturatedAdd(uint32_t a,uint32_t b) {return b>UINT32_MAX-a?UINT32_MAX:a+b;}
    uint32_t current[3]{},previous[3]{},start=0;
    bool initialized=false;
};
