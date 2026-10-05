#pragma once
#include "IoHomeProfileRegistry.h"

constexpr uint8_t IOHC_PROFILE_OBJECT_STRIDE = 18;
inline uint8_t ioHomeProfileObjectSlot(uint8_t index) {return index <= 3 ? index : index == 9 ? 4 : 0xFF;}
inline uint8_t ioHomeProfileObjectIndex(uint8_t slot) {return slot < 4 ? slot : slot == 4 ? 9 : 0xFF;}
inline bool ioHomeProfileObjectBinary(const IoHomeProfileDescriptor *profile) {
    return profile && (profile->mp == ParameterSemantic::LockState || profile->mp == ParameterSemantic::SwitchState ||
                       (profile->capabilityFlags & IoHomeCapabilityBinaryOnly));
}
struct IoHomeProfileObjectValue {float percent=0;uint16_t raw=0;bool binary=false;};
inline bool ioHomeDecodeProfileObject(const IoHomeProfileDescriptor *profile,uint8_t index,uint16_t raw,IoHomeProfileObjectValue &value) {
    if (!profile || ioHomeProfileObjectSlot(index)==0xFF || raw==IOHC_NO_FEEDBACK_VALUE) return false;
    const auto semantic=ioHomeResolvedParameterSemantic(profile,index);
    if (semantic==ParameterSemantic::Unknown || semantic==ParameterSemantic::Unsupported) return false;
    value.raw=raw;
    if (semantic==ParameterSemantic::ProjectionAngle) return true; // Opaque word; no degrees conversion claimed.
    if (semantic==ParameterSemantic::WindowSecurityMode) return raw<=2;
    if (index==0 && ioHomeProfileObjectBinary(profile) && raw!=0 && raw!=IOHC_POSITION_MAX) return false;
    const auto polarity=index==0 ? profile->mpPolarity : ioHomeIsOrientationSemantic(semantic) ? ParameterPolarity::Reversed : ParameterPolarity::Normal;
    if (!ioHomeRawToPercent(raw,polarity,value.percent)) return false;
    value.binary=value.percent>=50;
    return true;
}
inline uint16_t ioHomeProfileObjectReadMask(const IoHomeProfileDescriptor *profile) {
    uint16_t mask=0;
    for (uint8_t slot=1;slot<5;++slot) {const uint8_t index=ioHomeProfileObjectIndex(slot);
        const auto semantic=ioHomeResolvedParameterSemantic(profile,index);
        if (semantic!=ParameterSemantic::Unknown && semantic!=ParameterSemantic::Unsupported) mask |= uint16_t(1)<<(index-1);
    }
    return mask;
}
