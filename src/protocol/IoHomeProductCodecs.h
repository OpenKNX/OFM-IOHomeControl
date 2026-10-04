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

// OVPd RGB definitions 0x60100, 0x60102, 0x10000060102:
// inverse MP brightness plus FP10=u and FP11=v. No gamma correction.
struct IoHomeRgbRepresentation
{
    uint16_t mp = IOHC_POSITION_MAX;
    uint16_t u = 0;
    uint16_t v = 0;
    bool hasChromaticity = false;
};

inline bool ioHomeEncodeRgb(double iRed, double iGreen, double iBlue,
                            IoHomeRgbRepresentation &oValue)
{
    if (!std::isfinite(iRed) || !std::isfinite(iGreen) || !std::isfinite(iBlue) ||
        iRed < 0 || iRed > 255 || iGreen < 0 || iGreen > 255 || iBlue < 0 || iBlue > 255)
        return false;
    IoHomeRgbRepresentation lValue;
    const double r = iRed / 255.0, g = iGreen / 255.0, b = iBlue / 255.0;
    const double lMaximum = std::fmax(r, std::fmax(g, b));
    // Retained RGBToVector divides 0/0 for black. Explicit safe policy:
    // MP=off, omit FP10/11 instead of casting NaN into a wire word.
    if (lMaximum > 0)
    {
        const double X = 2.7689*r + 1.7517*g + 1.1302*b;
        const double Y = r + 4.5907*g + 0.0601*b;
        const double Z = 0.056508*g + 5.5943*b;
        const double lDenominator = X + 15*Y + 3*Z;
        const double u = 4*X / lDenominator, v = 9*Y / lDenominator;
        // Deliberate positive truncation policy for representation words.
        // Source bit-library fractional coercion is not hardware-qualified.
        lValue.mp = static_cast<uint16_t>((100 - lMaximum*100)*512);
        lValue.u = static_cast<uint16_t>(u*51200);
        lValue.v = static_cast<uint16_t>(v*51200);
        lValue.hasChromaticity = true;
    }
    oValue = lValue;
    return true;
}

inline bool ioHomeDecodeRgb(uint16_t iMp, uint16_t iU, uint16_t iV,
                            uint8_t &oRed, uint8_t &oGreen, uint8_t &oBlue)
{
    if (iMp > IOHC_POSITION_MAX || iU > IOHC_POSITION_MAX || iV > IOHC_POSITION_MAX)
        return false;
    double lRed = 0, lGreen = 0, lBlue = 0;
    if (iMp != IOHC_POSITION_MAX && iU != 0 && iV != 0)
    {
        const double u = iU / 51200.0, v = iV / 51200.0;
        const double X = 2.25*u/v, Z = (-3*u - 20*v + 12)/(4*v);
        auto lClamp = [](double iValue) { return std::fmax(0.0, std::fmin(1.0, iValue)); };
        const double r = lClamp(0.41847*X - 0.15866 - 0.082835*Z);
        const double g = lClamp(-0.091169*X + 0.25243 + 0.015708*Z);
        const double b = lClamp(0.0009209*X - 0.0025498 + 0.1786*Z);
        const double lMaximum = std::fmax(r, std::fmax(g,b));
        if (lMaximum <= 0) return false;
        const double lScale = 255*(1 - iMp/51200.0)/lMaximum;
        lRed = std::round(lScale*r);
        lGreen = std::round(lScale*g);
        lBlue = std::round(lScale*b);
    }
    oRed = static_cast<uint8_t>(lRed);
    oGreen = static_cast<uint8_t>(lGreen);
    oBlue = static_cast<uint8_t>(lBlue);
    return true;
}

// OVPd tunable-white definitions 0x60202 and 0x10000060202, FP14.
// setColorTemperature's second argument is RAW MP, not a percentage.
inline bool ioHomeDecodeWhiteTemperature(uint16_t iRaw, uint16_t &oKelvin)
{
    if (iRaw > IOHC_POSITION_MAX) return false;
    oKelvin = static_cast<uint16_t>(std::round(iRaw*4500.0/51200 + 2000));
    return true;
}

inline bool ioHomeEncodeWhiteTemperature(double iKelvin, uint16_t &oRaw)
{
    if (!std::isfinite(iKelvin) || iKelvin < 2000 || iKelvin > 6500) return false;
    // Same explicit positive truncation policy as RGB.
    oRaw = static_cast<uint16_t>((iKelvin - 2000)*51200/4500);
    return true;
}
