#pragma once
#include <stdint.h>
struct IoHomeCommissioningJob
{
    enum class Owner:uint8_t { None, Pairing, Import };
    enum class Stage:uint8_t { Idle, Preparing, Discovering, Verifying, CandidateReady, Saving, Done, Cancelled, Failed, Unconfirmed };
    uint32_t generation=0;
    Owner owner=Owner::None;
    Stage stage=Stage::Idle;
    uint8_t channel=0xFF;
    uint32_t node=0;
    bool active() const { return stage>=Stage::Preparing && stage<=Stage::Saving; }
    bool begin(Owner who,uint8_t target=0xFF)
    {
        if (active() || who==Owner::None || generation==0xFFFFFFFF) return false;
        ++generation; owner=who; channel=target; node=0; stage=Stage::Preparing; return true;
    }
    bool cancel(uint32_t token)
    {
        if (!active() || token!=generation) return false;
        stage=Stage::Cancelled; return true;
    }
};
