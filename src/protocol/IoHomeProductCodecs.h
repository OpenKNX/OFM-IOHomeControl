#pragma once
#include "IoHomeCommands.h"
#include <cmath>
#include <stdint.h>
#include <cstring>

// Explicit OVPd product-definition selection, not a discovery-profile match.
// Conversion helpers do not grant RF write permission or KNX capabilities.
enum class IoHomeTemperatureProduct : uint8_t
{
    HeatPump,                 // 0x160000
    HeatingInterface,         // 0x0E0000
    GenericAdjustableHeater,  // 0x340100
    AtlanticAdjustableHeater, // 0x34010C
    AtlanticDhwV2,            // 0x33000C
    AtlanticDhwCentikelvin    // 0x1000033000C
};

inline bool ioHomeTemperatureProductByName(const char *iName, IoHomeTemperatureProduct &oProduct)
{
    if (!iName) return false;
    struct Name { const char *name; IoHomeTemperatureProduct product; };
    static constexpr Name kNames[] = {
        {"heatpump", IoHomeTemperatureProduct::HeatPump},
        {"heating-interface", IoHomeTemperatureProduct::HeatingInterface},
        {"generic-heater", IoHomeTemperatureProduct::GenericAdjustableHeater},
        {"atlantic-heater", IoHomeTemperatureProduct::AtlanticAdjustableHeater},
        {"atlantic-dhw-v2", IoHomeTemperatureProduct::AtlanticDhwV2},
        {"atlantic-dhw-ck", IoHomeTemperatureProduct::AtlanticDhwCentikelvin}
    };
    for (const auto &lName : kNames)
        if (std::strcmp(iName, lName.name) == 0) { oProduct = lName.product; return true; }
    return false;
}

struct IoHomeTemperatureContext
{
    bool hasBounds = false;
    uint16_t minimumCentikelvin = 0;
    uint16_t maximumCentikelvin = 0;
    bool hasComfort = false;
    uint16_t comfortRaw = 0; // FP12 required to decode generic heater FP13.
};

inline bool ioHomeTemperatureBounds(IoHomeTemperatureProduct iProduct, uint8_t iIndex,
                                    const IoHomeTemperatureContext &iContext,
                                    uint16_t &oMinimum, uint16_t &oMaximum)
{
    if (iProduct == IoHomeTemperatureProduct::AtlanticAdjustableHeater && (iIndex == 12 || iIndex == 13))
    {
        oMinimum = iIndex == 12 ? 28015 : 27515;
        oMaximum = iIndex == 12 ? 30115 : 28215;
        return true;
    }
    if (((iProduct == IoHomeTemperatureProduct::HeatingInterface ||
          iProduct == IoHomeTemperatureProduct::AtlanticDhwV2) && iIndex == 0) ||
        (iProduct == IoHomeTemperatureProduct::GenericAdjustableHeater && (iIndex == 12 || iIndex == 13)))
    {
        if (!iContext.hasBounds || iContext.minimumCentikelvin >= iContext.maximumCentikelvin)
            return false;
        oMinimum = iContext.minimumCentikelvin;
        oMaximum = iContext.maximumCentikelvin;
        return true;
    }
    return false;
}

inline bool ioHomeDecodeProductTemperature(IoHomeTemperatureProduct iProduct, uint8_t iIndex,
                                           uint16_t iRaw, const IoHomeTemperatureContext &iContext,
                                           double &oCelsius)
{
    if (iRaw > IOHC_POSITION_MAX)
        return false; // Special/unknown words must not become temperatures.
    double lCelsius;
    if (iProduct == IoHomeTemperatureProduct::HeatPump && (iIndex == 0 || iIndex == 8))
    {
        const double lScale = iIndex == 8 ? 1.0 : 10.0;
        lCelsius = std::round((iRaw * 120.0 / 51200.0 - 40.0) * lScale) / lScale;
    }
    else if (iProduct == IoHomeTemperatureProduct::AtlanticDhwCentikelvin && iIndex == 0)
        lCelsius = iRaw / 100.0 - 273.15;
    else
    {
        uint16_t lMinimum, lMaximum;
        if (!ioHomeTemperatureBounds(iProduct, iIndex, iContext, lMinimum, lMaximum))
            return false;
        uint16_t lRaw = iRaw;
        if (iProduct == IoHomeTemperatureProduct::GenericAdjustableHeater && iIndex == 13)
        {
            if (!iContext.hasComfort || iContext.comfortRaw > IOHC_POSITION_MAX || iRaw > iContext.comfortRaw)
                return false;
            lRaw = iContext.comfortRaw - iRaw;
        }
        lCelsius = std::round(((lRaw * (lMaximum - lMinimum) / 51200.0 + lMinimum) / 100.0 - 273.15) * 10.0) / 10.0;
    }
    oCelsius = lCelsius;
    return true;
}

// Checked inverse representation only. Generic FP13 needs its recovered
// coupled producer and is intentionally not encoded as an absolute setpoint.
// Heat-pump FP8 and the centikelvin DHW variant have no enabled write producer.
inline bool ioHomeEncodeProductTemperature(IoHomeTemperatureProduct iProduct, uint8_t iIndex,
                                           double iCelsius, const IoHomeTemperatureContext &iContext,
                                           uint16_t &oRaw)
{
    if (!std::isfinite(iCelsius)) return false;
    double lRaw;
    if (iProduct == IoHomeTemperatureProduct::HeatPump && iIndex == 0)
    {
        if (iCelsius < -40.0 || iCelsius > 80.0) return false;
        lRaw = std::round(51200.0 * (iCelsius + 40.0) / 120.0);
    }
    else
    {
        if (iProduct == IoHomeTemperatureProduct::GenericAdjustableHeater && iIndex == 13)
            return false;
        uint16_t lMinimum, lMaximum;
        if (!ioHomeTemperatureBounds(iProduct, iIndex, iContext, lMinimum, lMaximum)) return false;
        if (iCelsius < lMinimum / 100.0 - 273.15 - 1e-9 ||
            iCelsius > lMaximum / 100.0 - 273.15 + 1e-9) return false;
        lRaw = std::round(((iCelsius + 273.15) * 100.0 - lMinimum) * 51200.0 / (lMaximum - lMinimum));
    }
    if (!std::isfinite(lRaw) || lRaw < 0 || lRaw > IOHC_POSITION_MAX) return false;
    oRaw = static_cast<uint16_t>(lRaw);
    return true;
}
