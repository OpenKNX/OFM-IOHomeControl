#pragma once
#include <stdint.h>
struct IoHomeCommissioningJob
{
    enum class Owner:uint8_t { None, Pairing, Import };
    enum class Stage:uint8_t { Idle, Preparing, Discovering, Verifying, CandidateReady, Saving, Done, Cancelled, Failed, Unconfirmed };
    uint32_t generation=0,startedMs=0,budgetMs=0,snapshotRevision=0;
    enum class Error:uint8_t { None,Deadline,PeerOrTransport,Persistence,StaleSnapshot };
    Error error=Error::None;
    Owner owner=Owner::None;
    Stage stage=Stage::Idle;
    uint8_t channel=0xFF;
    uint32_t node=0;
    bool active() const { return stage>=Stage::Preparing && stage<=Stage::Saving; }
    bool begin(Owner who,uint8_t target=0xFF,uint32_t now=0,uint32_t budget=0)
    {
        if (active() || who==Owner::None || generation==0xFFFFFFFF) return false;
        ++generation; startedMs=now;budgetMs=budget;snapshotRevision=0;error=Error::None;owner=who; channel=target; node=0; stage=Stage::Preparing; return true;
    }
    bool expired(uint32_t now) const {return active()&&stage!=Stage::CandidateReady&&budgetMs&&uint32_t(now-startedMs)>=budgetMs;}
    uint32_t remaining(uint32_t now) const {const auto elapsed=uint32_t(now-startedMs);return budgetMs>elapsed?budgetMs-elapsed:0;}
    void freeze() {if(!snapshotRevision) snapshotRevision=1;}
    bool matches(uint32_t token,uint32_t revision) const {return generation==token&&snapshotRevision&&snapshotRevision==revision;}
    bool cancel(uint32_t token)
    {
        if (!active() || token!=generation) return false;
        stage=Stage::Cancelled; return true;
    }
};
