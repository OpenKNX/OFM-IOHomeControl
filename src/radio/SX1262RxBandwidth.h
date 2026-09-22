#pragma once

#include <stdint.h>

// SX1262 GFSK RX bandwidth register values from the Semtech command table.
// 58.6 kHz remains the validated OFM default; the alternatives are exposed
// only for runtime hardware diagnostics.
enum class RadioSX1262RxBandwidth : uint8_t
{
    Khz39_0 = 0x1C,
    Khz46_9 = 0x14,
    Khz58_6 = 0x0C,
    Khz78_2 = 0x1B,
    Khz117_3 = 0x0B,
    Khz156_2 = 0x1A,
    Khz187_2 = 0x12,
};

inline bool isValidRadioSX1262RxBandwidth(RadioSX1262RxBandwidth iBandwidth)
{
    switch (iBandwidth)
    {
    case RadioSX1262RxBandwidth::Khz39_0:
    case RadioSX1262RxBandwidth::Khz46_9:
    case RadioSX1262RxBandwidth::Khz58_6:
    case RadioSX1262RxBandwidth::Khz78_2:
    case RadioSX1262RxBandwidth::Khz117_3:
    case RadioSX1262RxBandwidth::Khz156_2:
    case RadioSX1262RxBandwidth::Khz187_2:
        return true;
    default:
        return false;
    }
}

inline const char *radioSX1262RxBandwidthName(RadioSX1262RxBandwidth iBandwidth)
{
    switch (iBandwidth)
    {
    case RadioSX1262RxBandwidth::Khz39_0: return "39.0";
    case RadioSX1262RxBandwidth::Khz46_9: return "46.9";
    case RadioSX1262RxBandwidth::Khz58_6: return "58.6";
    case RadioSX1262RxBandwidth::Khz78_2: return "78.2";
    case RadioSX1262RxBandwidth::Khz117_3: return "117.3";
    case RadioSX1262RxBandwidth::Khz156_2: return "156.2";
    case RadioSX1262RxBandwidth::Khz187_2: return "187.2";
    default: return "invalid";
    }
}
