#pragma once
#include "IoHomeObjectTransfer.h"

namespace IoHomeAlfeaRead {
constexpr uint16_t HeatingEnergy = 0x0023;
constexpr uint16_t HotWaterEnergy = 0x0025;
constexpr uint16_t OutdoorTemperature = 0x019B;

enum class Unit : uint8_t { KWh, TenthsCelsius };
struct Reading {
    int32_t value;
    Unit unit;
};

// Read-only class 0x0C recipe. The caller owns scheduling and peer correlation.
inline bool open(IoHomeObjectTransfer &transfer, uint32_t peer, uint32_t token,
                 uint16_t offset, bool prepare, uint32_t now, uint32_t budget)
{
    if (offset != HeatingEnergy && offset != HotWaterEnergy && offset != OutdoorTemperature)
        return false;
    return transfer.begin(IoHomeObjectTransfer::Direction::Read, peer, token,
                          0x0C, prepare ? 0x1001 : 0x1002, offset, prepare ? 0 : 3,
                          now, budget, 0x08, 0, true);
}

inline bool decode(uint16_t offset, const uint8_t *data, uint16_t length, Reading &out)
{
    if (!data || length != 3 || data[0] != 0x80)
        return false;
    const uint16_t raw = uint16_t(data[1]) << 8 | data[2];
    if (offset == HeatingEnergy || offset == HotWaterEnergy)
        out = {raw, Unit::KWh};
    else if (offset == OutdoorTemperature)
        out = {raw < 0x8000 ? int32_t(raw) : int32_t(raw) - 0x10000, Unit::TenthsCelsius};
    else
        return false;
    return true;
}
}
