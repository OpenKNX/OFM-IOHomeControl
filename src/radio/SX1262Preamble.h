#pragma once
#include <stdint.h>
// SX1262 SetPacketParams stores a uint16_t bit count. Reject before narrowing
// and leave output bytes untouched on error (including the zero-length case).
inline bool sx1262EncodePreamble(uint16_t iBytes, uint8_t &oMsb, uint8_t &oLsb)
{
    const uint32_t lBits = static_cast<uint32_t>(iBytes) * 8UL;
    if (iBytes == 0 || lBits > UINT16_MAX)
        return false;
    oMsb = static_cast<uint8_t>(lBits >> 8);
    oLsb = static_cast<uint8_t>(lBits);
    return true;
}
