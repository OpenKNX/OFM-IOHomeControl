#pragma once
#include "IoHomeProductBinding.h"
#include "IoHomeProductPolicy.h"
#include "IoHomeProductCodecs.h"

// Source-qualified activation REPRESENTATIONS for bench preparation.
// Excludes originator/ACEI, command/header, authentication and CRC.
// No RF enqueue here: physical high-FP write acceptance remains unqualified.
// Commit output only after all validation succeeds.
inline bool ioHomeBuildBoundRgbRepresentation(const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence, double iRed, double iGreen,
    double iBlue, uint8_t *oData, uint8_t iCapacity, uint8_t &oLength)
{
    if (ioHomeBindProductFamily(iIdentity, iEvidence) != IoHomeBoundProductFamily::RgbLight)
        return false;
    if (!ioHomeProductAccess(IoHomeBoundProductFamily::RgbLight,10).encodeRepresentation) return false;
    IoHomeRgbRepresentation lRgb;
    if (!ioHomeEncodeRgb(iRed, iGreen, iBlue, lRgb)) return false;
    const IoHomeFpValue lValues[] = {{10, lRgb.u}, {11, lRgb.v}};
    uint8_t lData[8], lLength;
    if (!ioHomeBuildActivationRepresentation(lRgb.mp, lValues,
            lRgb.hasChromaticity ? 2 : 0, lData, sizeof(lData), lLength) ||
        !oData || iCapacity < lLength) return false;
    std::memcpy(oData, lData, lLength); oLength = lLength;
    return true;
}

inline bool ioHomeBuildBoundWhiteRepresentation(const IoHomeProtocolIdentity &iIdentity,
    const IoHomeProductIdentityEvidence &iEvidence, double iKelvin, uint16_t iRawMp,
    uint8_t *oData, uint8_t iCapacity, uint8_t &oLength)
{
    if (ioHomeBindProductFamily(iIdentity, iEvidence) != IoHomeBoundProductFamily::TunableWhiteLight ||
        (iRawMp > IOHC_POSITION_MAX && iRawMp != IOHC_PARAMETER_IGNORE)) return false;
    if (!ioHomeProductAccess(IoHomeBoundProductFamily::TunableWhiteLight,14).encodeRepresentation) return false;
    uint16_t lRaw;
    if (!ioHomeEncodeWhiteTemperature(iKelvin, lRaw)) return false;
    const IoHomeFpValue lValue{14, lRaw};
    uint8_t lData[6], lLength;
    if (!ioHomeBuildActivationRepresentation(iRawMp, &lValue, 1, lData, sizeof(lData), lLength) ||
        !oData || iCapacity < lLength) return false;
    std::memcpy(oData, lData, lLength); oLength = lLength;
    return true;
}

// Generic heater 0x340100 only; caller supplies independently validated bounds.
// Canonical ascending FP11 precedes FP12/13 despite retained Lua caller order.
inline bool ioHomeBuildGenericHeaterTemperature(bool setback,double celsius,
 const IoHomeTemperatureContext &context,uint8_t *out,uint8_t capacity,uint8_t &length) {
 uint16_t absolute=0;if(!ioHomeEncodeProductTemperature(IoHomeTemperatureProduct::GenericAdjustableHeater,12,celsius,context,absolute))return false;
 uint16_t raw=absolute;
 if(setback){if(!context.hasComfort||context.comfortRaw>IOHC_POSITION_MAX||absolute>context.comfortRaw)return false;raw=context.comfortRaw-absolute;}
 const IoHomeFpValue values[]={{11,0xFFFF},{uint8_t(setback?13:12),raw}};
 return ioHomeBuildActivationRepresentation(0xD400,values,2,out,capacity,length);
}
