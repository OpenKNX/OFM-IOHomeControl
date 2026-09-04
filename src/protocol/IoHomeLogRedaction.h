#pragma once

#include "IoHomeFrame.h"

#include <cstdio>
#include <string>

inline bool ioHomeCommandCarriesLogSensitiveKeyMaterial(IoHomeCommand iCommand)
{
    return iCommand == IoHomeCommand::SendKey1W ||
           iCommand == IoHomeCommand::KeyTransfer;
}

inline std::string ioHomeBytesToHexForLog(const uint8_t *iData, uint8_t iLen)
{
    if (iData == nullptr || iLen == 0)
        return {};

    static const char kHex[] = "0123456789ABCDEF";
    std::string lOut;
    lOut.reserve(static_cast<size_t>(iLen) * 2U);
    for (uint8_t i = 0; i < iLen; ++i)
    {
        lOut.push_back(kHex[(iData[i] >> 4) & 0x0F]);
        lOut.push_back(kHex[iData[i] & 0x0F]);
    }
    return lOut;
}

inline std::string ioHomeRedactionMarker(uint8_t iLen)
{
    char lMarker[32] = {};
    std::snprintf(lMarker, sizeof(lMarker), "[%u bytes redacted]", static_cast<unsigned>(iLen));
    return lMarker;
}

// Retain the routing header and command for diagnostics, but never render a
// recoverable wrapped/encrypted key from 0x30 or 0x32.
inline std::string ioHomeFrameHexForLog(const uint8_t *iData, uint8_t iLen)
{
    if (iData == nullptr || iLen < IOHC_FRAME_MIN_SIZE)
        return ioHomeBytesToHexForLog(iData, iLen);

    const IoHomeCommand lCommand = static_cast<IoHomeCommand>(iData[IOHC_FRAME_MIN_SIZE - 1U]);
    if (!ioHomeCommandCarriesLogSensitiveKeyMaterial(lCommand))
        return ioHomeBytesToHexForLog(iData, iLen);

    std::string lOut = ioHomeBytesToHexForLog(iData, IOHC_FRAME_MIN_SIZE);
    lOut += ioHomeRedactionMarker(static_cast<uint8_t>(iLen - IOHC_FRAME_MIN_SIZE));
    return lOut;
}

inline std::string ioHomePayloadHexForLog(IoHomeCommand iCommand,
                                          const uint8_t *iData, uint8_t iLen)
{
    if (ioHomeCommandCarriesLogSensitiveKeyMaterial(iCommand))
        return ioHomeRedactionMarker(iLen);
    return ioHomeBytesToHexForLog(iData, iLen);
}
