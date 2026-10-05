#pragma once
#include "IoHomeCommands.h"

struct IoHomeDirectedTimeout {
    uint16_t milliseconds;
    uint8_t group, responseClass;
    bool fallback;
};
// Recovered STM directed transaction deadlines, not KLF turnaround hints.
inline IoHomeDirectedTimeout ioHomeDirectedTimeout(uint8_t ctrl1, const IoHomeProtocolIdentity *identity) {
    const uint8_t group = ((ctrl1 >> 4) & 1) | (((ctrl1 >> 3) & 1) << 1) |
                          (((ctrl1 >> 7) & 1) << 2) | (((ctrl1 >> 5) & 1) << 3);
    static constexpr uint16_t rows[16][4] = {
        {116,121,131,151},{161,211,311,511},{161,166,176,196},{100,100,100,100},
        {116,121,131,151},{161,211,311,511},{161,166,176,196},{100,100,100,100},
        {534,711,811,1011},{534,711,811,1011},{534,711,811,1011},{534,711,811,1011},
        {534,711,811,1011},{534,711,811,1011},{534,711,811,1011},{534,711,811,1011}
    };
    const bool known = identity && identity->valid && identity->fullMetadata && identity->hasMib && identity->responseTimeClass < 4;
    // Unknown/partial MIB: allow the slowest recovered peer. Never interpret an
    // absent class as class zero or borrow a KLF turnaround number as a deadline.
    const uint8_t cls = known ? identity->responseTimeClass : 0xFF;
    return {known ? rows[group][cls] : uint16_t(1011), group, cls, !known};
}
