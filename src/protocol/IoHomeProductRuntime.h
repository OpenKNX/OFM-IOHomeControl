#pragma once
#include "IoHomeProductPolicy.h"
#include "IoHomeProductCodecs.h"
#include "IoHomeMpFpRead.h"
#include <cstring>

// Volatile observations are identity/key-bound; reboot never restores freshness.
class IoHomeProductRuntime {
public:
    enum class Trust:uint8_t { None,Passive,Correlated,Authenticated };
    struct Sample {uint16_t raw=0;uint32_t generation=0,receivedMs=0;Trust trust=Trust::None;bool present=false;};
    void bind(uint32_t node,const uint8_t *key) {
        if(!key)return;
        if(node!=mNode||std::memcmp(key,mKey,16)){mNode=node;std::memcpy(mKey,key,16);for(auto &s:mSamples)s={};}
    }
    bool observe(uint32_t peer,uint8_t index,uint16_t raw,uint32_t generation,uint32_t now,Trust trust) {
        if(!mNode||peer!=mNode||index>19||!generation||trust==Trust::None)return false;
        auto &s=mSamples[index];
        if(s.present&&generation<s.generation)return false; // generations never wrap in controller
        s={raw,generation,now,trust,true};return true;
    }
    // One standard reply is one observation generation. Never combine independently
    // queried FPs into a manufactured authenticated RGB snapshot.
    bool observeReply(uint32_t peer,const IoHomeMpFpReply &reply,uint32_t generation,uint32_t now,Trust trust) {
        IoHomeProductRuntime next=*this;
        if(!next.observe(peer,0,reply.current,generation,now,trust))return false;
        for(uint8_t index=1;index<=16;index++)if(reply.present&(uint16_t(1)<<(index-1)))
            if(!next.observe(peer,index,reply.values[index-1],generation,now,trust))return false;
        *this=next;return true;
    }
    const Sample *sample(uint8_t index) const {return index<=19?&mSamples[index]:nullptr;}
    static bool fresh(const Sample &s,uint32_t now,uint32_t maxAge){return s.present&&maxAge&&uint32_t(now-s.receivedMs)<=maxAge;}
    bool rgb(IoHomeBoundProductFamily family,uint32_t now,uint32_t maxAge,uint8_t &r,uint8_t &g,uint8_t &b) const {
        if(family!=IoHomeBoundProductFamily::RgbLight)return false;
        const auto observation=[&](uint8_t index){const auto &s=mSamples[index];return IoHomeProductObservation{s.raw,s.generation,s.present,s.trust==Trust::Authenticated,fresh(s,now,maxAge)};};
        if(!ioHomeCoherentRgbObservations(observation(0),observation(10),observation(11)))return false;
        return ioHomeDecodeRgb(mSamples[0].raw,mSamples[10].raw,mSamples[11].raw,r,g,b);
    }
    bool white(IoHomeBoundProductFamily family,uint32_t now,uint32_t maxAge,uint16_t &kelvin) const {
        const auto &s=mSamples[14];
        return family==IoHomeBoundProductFamily::TunableWhiteLight&&s.trust==Trust::Authenticated&&fresh(s,now,maxAge)&&ioHomeDecodeWhiteTemperature(s.raw,kelvin);
    }
private:
    uint32_t mNode=0;uint8_t mKey[16]{};Sample mSamples[20]{};
};
