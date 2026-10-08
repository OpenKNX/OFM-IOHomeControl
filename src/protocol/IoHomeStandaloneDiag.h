#pragma once
#include <stdint.h>
#if defined(IOHC_STANDALONE_DIAG) && IOHC_STANDALONE_DIAG
constexpr bool kIoHomeStandaloneDiag=true;
#else
constexpr bool kIoHomeStandaloneDiag=false;
#endif
// Only bypass the ETS configuration gate, never startup/storage/RF ownership.
constexpr bool ioHomeServiceEnabled(bool configured){return configured||kIoHomeStandaloneDiag;}
constexpr bool ioHomeStandaloneChannel(uint8_t index){return kIoHomeStandaloneDiag&&index==0;}
constexpr uint32_t IOHC_STANDALONE_STARTUP_DELAY_MS=1000;
