#pragma once
#include <stdint.h>

#define IOHC_1W_SEQUENCE_RESERVE_WINDOW 16

// Allocation policy shared by production channels and native controller tests.
// The caller must persist a changed watermark before transmitting this value.
inline uint16_t ioHomeAllocateSequence1W(uint16_t &ioSequence, uint16_t &ioReserved,
                                        bool iForceReserve, bool &oSaveRequired)
{
    ioSequence = static_cast<uint16_t>(ioSequence + 1U);
    const int16_t lRemaining = static_cast<int16_t>(ioReserved - ioSequence);
    oSaveRequired = iForceReserve || ioReserved == 0 || lRemaining <= 0;
    if (oSaveRequired)
        ioReserved = static_cast<uint16_t>(ioSequence + IOHC_1W_SEQUENCE_RESERVE_WINDOW);
    return ioSequence;
}
