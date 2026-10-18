#pragma once
#include <stdint.h>

// Roll back only the latest position intent; use received position feedback
// when available, otherwise the pre-command estimate. Neither is proof of
// current physical position after an unanswered command: request status next.
struct IoHomeOptimisticPosition
{
    bool pending=false, observed=false;
    uint8_t parameter=0;
    float lastObserved=0, before=0;
    void observe(float position) { observed=true; lastObserved=position; }
    void begin(uint8_t target, float position) { pending=true; parameter=target; before=position; }
    bool reject(uint8_t target, float &position) {
        if (!pending || target!=parameter) return false;
        pending=false; position=observed?lastObserved:before; return true;
    }
};
