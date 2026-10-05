#pragma once
#include "IoHomeProductCodecs.h"

// Validated physical context is not an MP/FP default-limit observation.
class IoHomePhysicalBounds {
public:
    enum class Source:uint8_t {None,ProductDefinition,OriginalPeerMeasurement,ExpertValidated};
    struct Provenance {Source source=Source::None;uint32_t evidence=0,node=0,revision=0;};
    bool set(uint32_t node,uint32_t revision,uint16_t minimum,uint16_t maximum,Source source,uint32_t evidence){
        if(!node||node>0xFFFFFF||!revision||minimum>=maximum||source==Source::None||uint8_t(source)>3||!evidence)return false;
        mContext={};mContext.hasBounds=true;mContext.minimumCentikelvin=minimum;mContext.maximumCentikelvin=maximum;
        mProvenance={source,evidence,node,revision};return true;
    }
    const IoHomeTemperatureContext *context(uint32_t node,uint32_t revision)const{
        return mContext.hasBounds&&node==mProvenance.node&&revision==mProvenance.revision?&mContext:nullptr;
    }
    const Provenance &provenance()const{return mProvenance;}
    void invalidate(){mContext={};mProvenance={};}
private:
    IoHomeTemperatureContext mContext{};Provenance mProvenance{};
};
