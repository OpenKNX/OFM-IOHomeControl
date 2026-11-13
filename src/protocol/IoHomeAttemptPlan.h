#pragma once
#include <cstdint>

// Reserve the complete authenticated attempt before RF admission. Elapsed time
// includes earlier waits, TX and gaps. CCA still admits every actual TX separately.
struct IoHomeAttemptPlan {
    static constexpr uint8_t hardCap=9;
    static bool fits(uint32_t elapsed,uint32_t budget,uint32_t requestAir,
                     uint32_t firstWindow,uint32_t authAir,uint32_t finalWindow,
                     uint32_t overhead=10) {
        const uint64_t needed=uint64_t(requestAir)+firstWindow+authAir+finalWindow+overhead;
        return elapsed<=budget && needed<=uint64_t(budget-elapsed);
    }
};
