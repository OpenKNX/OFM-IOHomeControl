#pragma once
#include <cstdint>

// Application recovery policy, in milliseconds; independent of exchange timing.
// Keep the counters in the channel and use no heap or per-channel delay table.
struct IoHomeStatusPollRetry {
    static uint32_t failed(bool challenged, uint8_t &auth, uint8_t &silent,
                           uint32_t minimumMs = 0) {
        uint8_t &streak = challenged ? auth : silent;
        (challenged ? silent : auth) = 0;
        if (streak != UINT8_MAX) ++streak;
        static constexpr uint32_t authMs[] = {5000,30000,120000,300000};
        static constexpr uint32_t silentMs[] = {5000,15000,30000,60000,300000};
        const uint8_t last = challenged ? 3 : 4;
        const uint8_t index = streak - 1 < last ? streak - 1 : last;
        const uint32_t delay = challenged ? authMs[index] : silentMs[index];
        return delay < minimumMs ? minimumMs : delay;
    }
};
