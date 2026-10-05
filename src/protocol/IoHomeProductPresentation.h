#pragma once
#include "IoHomeProductCodecs.h"
#include "IoHomeProductPolicy.h"

// Presentation metadata does not create KOs or establish accepted RF transport.
enum class IoHomeQuantity:uint8_t { Unknown, Celsius, Kelvin, Rgb };
struct IoHomeQuantityPresentation
{
    IoHomeQuantity quantity=IoHomeQuantity::Unknown;
    uint16_t dptMain=0,dptSub=0;
    bool publicationQualified=false;
};
inline IoHomeQuantityPresentation ioHomeTemperaturePresentation(IoHomeTemperatureProduct product,uint8_t index)
{
    // The 2..9 numeric Atlantic FP13 quantity is not proven an absolute target.
    if(product==IoHomeTemperatureProduct::AtlanticAdjustableHeater && index==13) return {};
    const bool supported=(product==IoHomeTemperatureProduct::HeatPump && (index==0 || index==8)) ||
        ((product==IoHomeTemperatureProduct::HeatingInterface || product==IoHomeTemperatureProduct::AtlanticDhwV2 ||
          product==IoHomeTemperatureProduct::AtlanticDhwCentikelvin) && index==0) ||
        (product==IoHomeTemperatureProduct::GenericAdjustableHeater && (index==12 || index==13)) ||
        (product==IoHomeTemperatureProduct::AtlanticAdjustableHeater && index==12);
    return supported ? IoHomeQuantityPresentation{IoHomeQuantity::Celsius,9,1,false}:IoHomeQuantityPresentation{};
}
inline IoHomeQuantityPresentation ioHomeLightingPresentation(IoHomeBoundProductFamily family,uint8_t index)
{
    if(family==IoHomeBoundProductFamily::TunableWhiteLight && index==14)
        return {IoHomeQuantity::Kelvin,7,600,false}; // additive Kelvin KO bank
    if(family==IoHomeBoundProductFamily::RgbLight && (index==10 || index==11))
        return {IoHomeQuantity::Rgb,232,600,false}; // coherent RGB, not independent chromaticity percentages
    return {};
}
