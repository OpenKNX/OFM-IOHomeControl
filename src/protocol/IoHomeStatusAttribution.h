#pragma once
#include <cstdint>
struct IoHomeStatusAttribution {
    enum class Source:uint8_t {Unknown,ForeignPassive,LocalCorrelated,Inbound71Authenticated};
    uint64_t localTransaction=0;
    uint32_t timeMs=0,transportSrc=0,transportDst=0,lastController=0;
    uint16_t target=0xFFFF,current=0xFFFF,remaining=0xFFFF;
    uint8_t originator=0,stateFlags=0;
    Source source=Source::Unknown;
    bool valid=false,layoutKnown=false,lastControllerKnown=false,remainingKnown=false,authenticatedLocalCommand=false;
    const char *sourceName()const{return source==Source::ForeignPassive?"foreign_passive":source==Source::LocalCorrelated?"local_correlated":source==Source::Inbound71Authenticated?"inbound_71_authenticated":"unknown";}
};
static_assert(sizeof(IoHomeStatusAttribution)<=40,"Bound status attribution RAM");
