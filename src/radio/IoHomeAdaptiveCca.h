#pragma once
#include <algorithm>
#include <cstdint>

// Sample while idle, away from preambles, after retune settling.
class IoHomeAdaptiveCca {
public:
    bool due(uint8_t channel, uint32_t now) const {
        return channel < 3 && (!mChannels[channel].count || now - mChannels[channel].lastMs >= 250);
    }
    void sample(uint8_t channel, uint32_t now, int16_t rssi) {
        if (!due(channel, now) || rssi < -127 || rssi > -40)
            return;
        auto &row = mChannels[channel];
        if (now - row.lastMs >= 10000) row = {};
        row.samples[row.next] = static_cast<int8_t>(rssi);
        row.next = (row.next + 1) % 9;
        if (row.count < 9) ++row.count;
        row.lastMs = now;
    }
    int16_t threshold(uint32_t now, int16_t fixed) const {
        int16_t quiet = 0;
        bool varied = false;
        int8_t first = 0;
        for (const auto &row : mChannels) {
            if (row.count != 9 || now - row.lastMs >= 10000)
                return -85;
            int8_t sorted[9];
            std::copy_n(row.samples, 9, sorted);
            std::sort(sorted, sorted + 9);
            if (!first) first = sorted[0];
            varied |= sorted[0] != first || sorted[8] != first;
            quiet = std::min<int16_t>(quiet, sorted[4]);
        }
        // A receiver stuck at the same RSSI on every channel cannot teach CCA.
        return varied ? std::clamp<int16_t>(quiet + 19, -90, -75) : fixed;
    }
private:
    struct Channel {
        uint32_t lastMs = 0;
        uint8_t next = 0, count = 0;
        int8_t samples[9]{};
    };
    Channel mChannels[3];
};
