#pragma once
#include "IoHomeProductBinding.h"

// Conversion knowledge and transport acceptance are separate evidence layers.
struct IoHomeProductAccess
{
    bool decode=false,encodeRepresentation=false;
    bool rfRead=false,rfWrite=false,knxPublish=false;
    bool requiresCoherentTuple=false;
};
inline IoHomeProductAccess ioHomeProductAccess(IoHomeBoundProductFamily family,uint8_t index)
{
    IoHomeProductAccess access;
    if(family==IoHomeBoundProductFamily::RgbLight && (index==0 || index==10 || index==11))
    { access.decode=true;access.encodeRepresentation=true;access.requiresCoherentTuple=true; }
    if(family==IoHomeBoundProductFamily::TunableWhiteLight && (index==0 || index==14))
    { access.decode=true;access.encodeRepresentation=true; }
    // No measured product high-FP transport is in the current qualification manifest.
    // Expert visibility cannot grant rfWrite or publication permission.
    return access;
}

struct IoHomeProductObservation
{
    uint16_t raw=0;
    uint32_t generation=0;
    bool present=false,authenticated=false,fresh=false;
};
inline bool ioHomeCoherentRgbObservations(const IoHomeProductObservation &mp,
    const IoHomeProductObservation &u,const IoHomeProductObservation &v)
{
    return mp.present && u.present && v.present && mp.authenticated && u.authenticated && v.authenticated &&
        mp.fresh && u.fresh && v.fresh && mp.generation!=0 && mp.generation==u.generation && mp.generation==v.generation;
}
