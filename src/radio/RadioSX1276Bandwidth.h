#pragma once
#include <stdint.h>
// Fosc / (mantissa * 2^(exponent+2)), Fosc=32 MHz. Values in Hz
// are rounded; the register encoding is the authoritative setting.
inline bool radioSX1276Bandwidth(uint32_t hz, uint8_t &reg) {
  switch(hz) {
    case 41667: reg=0x13; return true;
    case 50000: reg=0x0B; return true;
    case 62500: reg=0x03; return true;
    case 83333: reg=0x12; return true;
    case 100000: reg=0x0A; return true;
    default: return false;
  }
}
