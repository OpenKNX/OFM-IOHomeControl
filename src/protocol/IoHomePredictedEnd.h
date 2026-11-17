#pragma once
#include <cstdint>
#include "IoHomeRemainingTime.h"
#include "IoHomeExecutionState.h"

// Owned by a channel's existing bounded tracking epoch; never creates an epoch.
struct IoHomePredictedEnd {
    uint32_t at=0,awakeUntil=0;
    uint16_t last=0;
    uint8_t nondecreasing=0,execution=IoHomeExecutionState::Unknown;
    bool pending=false,positive=false;
    void clear(){*this={};}
    bool awake(uint32_t now)const{return positive&&int32_t(awakeUntil-now)>0;}
    bool due(uint32_t now)const{return pending&&int32_t(now-at)>=0;}
    void consume(){pending=false;}
    bool suppresses(uint32_t progress)const {
        return pending&&progress&&int32_t(at-progress)>=0&&uint32_t(at-progress)<=1000U;
    }
    void observe(uint32_t now,uint16_t raw,uint32_t deadline,uint8_t state=IoHomeExecutionState::Unknown) {
        if(state!=execution){clear();execution=state;}
        // A queued motor's frozen count describes its future journey. It is not
        // an executing countdown and must not train the stuck-count penalty.
        if(IoHomeExecutionState::waiting(state)){pending=false;positive=false;nondecreasing=0;last=raw;return;}
        const auto value=IoHomeRemainingTime::fromRaw(raw);
        if(value.kind!=IoHomeRemainingTime::Kind::Positive&&value.kind!=IoHomeRemainingTime::Kind::Zero){clear();return;}
        if(!raw&&!positive)return;
        if(raw&&positive&&raw>=last){if(nondecreasing<3)++nondecreasing;}
        else nondecreasing=0;
        if(nondecreasing>=3){pending=false;awakeUntil=now;last=raw;return;}
        const uint32_t duration=uint32_t(raw)*1000U;
        if(int32_t(deadline-now)<=0||uint64_t(duration)+1000U>=uint32_t(deadline-now)){
            pending=false;awakeUntil=now;return;
        }
        positive=positive||raw!=0;last=raw;awakeUntil=now+duration;
        at=awakeUntil+1000U;pending=true;
    }
};
