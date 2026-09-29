#include "IoHomeProfileRegistry.h"

namespace
{
    constexpr uint32_t capabilityFor(ParameterSemantic iSemantic)
    {
        switch (iSemantic)
        {
        case ParameterSemantic::Position:
        case ParameterSemantic::CurtainPosition:
        case ParameterSemantic::ShutterClosure:
            return IoHomeCapabilityPosition;
        case ParameterSemantic::UpperCurtainPosition:
        case ParameterSemantic::LowerCurtainPosition:
            return IoHomeCapabilityPosition | IoHomeCapabilityDualCurtain;
        case ParameterSemantic::LinearSpeed:
        case ParameterSemantic::AngularSpeed:
            return IoHomeCapabilitySpeed;
        case ParameterSemantic::SlatOrientation:
        case ParameterSemantic::HangerOrientation:
            return IoHomeCapabilityOrientation;
        case ParameterSemantic::SlatOrientationSpeed:
        case ParameterSemantic::HangerOrientationSpeed:
            return IoHomeCapabilityOrientationSpeed;
        case ParameterSemantic::LightIntensity:
        case ParameterSemantic::LightIntensityGradient:
            return IoHomeCapabilityLight;
        case ParameterSemantic::LockState:
            return IoHomeCapabilityLock;
        case ParameterSemantic::SwitchState:
            return IoHomeCapabilitySwitch;
        case ParameterSemantic::AirDemand:
            return IoHomeCapabilityVentilation;
        case ParameterSemantic::EnergyDemand:
        case ParameterSemantic::EnergyGradient:
            return IoHomeCapabilityHeating;
        default:
            return 0;
        }
    }

    constexpr IoHomeProfileDescriptor profile(
        uint16_t iPackedType, const char *iLabel, ParameterSemantic iMp,
        ParameterSemantic iFp1 = ParameterSemantic::Unsupported,
        ParameterSemantic iFp2 = ParameterSemantic::Unsupported,
        ParameterSemantic iFp3 = ParameterSemantic::Unsupported,
        ParameterPolarity iPolarity = ParameterPolarity::Normal,
        bool iSecuredVentilation = false)
    {
        return {
            static_cast<uint16_t>(iPackedType >> 6),
            static_cast<uint8_t>(iPackedType & 0x3F),
            IoHomeNodeClass::Actuator, iLabel, iMp,
            {iFp1, iFp2, iFp3},
            capabilityFor(iMp) | capabilityFor(iFp1) |
                capabilityFor(iFp2) | capabilityFor(iFp3),
            iPolarity, iSecuredVentilation};
    }

    using S = ParameterSemantic;
    // Packed IDs are (10-bit Profile << 6) | 6-bit SubProfile. An absent
    // entry is not a wildcard match for another subprofile.
    constexpr IoHomeProfileDescriptor kProfiles[] = {
        profile(0x0040, "Interior Venetian Blind", S::Position,
                S::SlatOrientation, S::SlatOrientationSpeed, S::LinearSpeed),
        profile(0x0080, "Roller Shutter", S::Position, S::LinearSpeed),
        profile(0x0081, "Adjustable Slats Roller Shutter", S::Position,
                S::LinearSpeed, S::SlatOrientationSpeed, S::SlatOrientation),
        profile(0x0082, "Roller Shutter with Projection", S::Position, S::LinearSpeed),
        profile(0x00C0, "Vertical Exterior Awning", S::Position, S::LinearSpeed),
        profile(0x0100, "Window Opener", S::Position, S::LinearSpeed,
                S::Unsupported, S::Unsupported, ParameterPolarity::Reversed, true),
        profile(0x0101, "Window Opener with Rain Sensor", S::Position, S::LinearSpeed,
                S::Unsupported, S::Unsupported, ParameterPolarity::Reversed, true),
        // The KLF table calls this speed linear or angular; the generic
        // speed semantic is retained until the physical mode is identified.
        profile(0x0140, "Garage Door Opener", S::Position, S::LinearSpeed),
        profile(0x017A, "Garage Door On/Off", S::Position),
        profile(0x0180, "Light", S::LightIntensity, S::LightIntensityGradient,
                S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x01BA, "Light On/Off", S::LightIntensity,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x01C0, "Gate Opener", S::Position, S::LinearSpeed),
        profile(0x01FA, "Gate On/Off", S::Position),
        profile(0x0240, "Door Lock", S::LockState),
        profile(0x0241, "Window Lock", S::LockState),
        profile(0x0280, "Vertical Interior Blind", S::Position, S::LinearSpeed),
        profile(0x0340, "Dual Roller Shutter", S::Position,
                S::UpperCurtainPosition, S::LowerCurtainPosition, S::LinearSpeed),
        profile(0x03C0, "On/Off Switch", S::SwitchState,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0400, "Horizontal Awning", S::Position, S::LinearSpeed),
        profile(0x0440, "Exterior Venetian Blind", S::Position,
                S::LinearSpeed, S::SlatOrientationSpeed, S::SlatOrientation),
        profile(0x0480, "Louvre Blind", S::CurtainPosition,
                S::LinearSpeed, S::HangerOrientationSpeed, S::HangerOrientation),
        profile(0x04C0, "Curtain Track", S::CurtainPosition, S::LinearSpeed),
        profile(0x0500, "Ventilation Point", S::AirDemand,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0501, "Air Inlet", S::AirDemand,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0502, "Air Transfer", S::AirDemand,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0503, "Air Outlet", S::AirDemand,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0540, "Exterior Heating", S::EnergyDemand, S::EnergyGradient,
                S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x057A, "Exterior Heating On/Off", S::EnergyDemand,
                S::Unsupported, S::Unsupported, S::Unsupported, ParameterPolarity::Reversed),
        profile(0x0600, "Swinging Shutter", S::ShutterClosure, S::LinearSpeed),
        profile(0x0601, "Independent Leaf Swinging Shutter", S::ShutterClosure,
                S::LinearSpeed),
    };
}

const IoHomeProfileDescriptor *ioHomeProfileDescriptor(uint16_t iProfile,
                                                        uint8_t iSubProfile)
{
    if (iProfile > 0x03FF || iSubProfile > 0x3F)
        return nullptr;
    for (const IoHomeProfileDescriptor &lProfile : kProfiles)
    {
        if (lProfile.profile == iProfile && lProfile.subProfile == iSubProfile)
            return &lProfile;
    }
    return nullptr;
}

const IoHomeProfileDescriptor *ioHomeProfileDescriptor(const IoHomeProtocolIdentity &iIdentity)
{
    return iIdentity.valid
               ? ioHomeProfileDescriptor(iIdentity.profile, iIdentity.subProfile)
               : nullptr;
}

ParameterSemantic ioHomeParameterSemantic(const IoHomeProfileDescriptor *iDescriptor,
                                          uint8_t iParameterIndex)
{
    if (!iDescriptor || iParameterIndex > 16)
        return ParameterSemantic::Unknown;
    return iParameterIndex == 0
               ? iDescriptor->mp
               : iDescriptor->fp[iParameterIndex - 1];
}

IoHomeParameterDescriptor ioHomeParameterDescriptor(
    const IoHomeProfileDescriptor *iDescriptor, uint8_t iParameterIndex)
{
    IoHomeParameterDescriptor lResult;
    lResult.index = iParameterIndex;
    lResult.semantic = ioHomeParameterSemantic(iDescriptor, iParameterIndex);
    if (!iDescriptor || iParameterIndex > 16)
        return lResult;
    lResult.polarity = iParameterIndex == 0 ? iDescriptor->mpPolarity
                                              : ParameterPolarity::Normal;
    switch (lResult.semantic)
    {
    case ParameterSemantic::Unsupported:
    case ParameterSemantic::Unknown:
        return lResult;
    case ParameterSemantic::LockState:
    case ParameterSemantic::SwitchState:
        lResult.valueKind = ParameterValueKind::Discrete;
        return lResult;
    default:
        lResult.valueKind = ParameterValueKind::Relative;
        // Only the existing FP1..FP3 percentage command path is validated.
        lResult.writable = iParameterIndex >= 1 && iParameterIndex <= 3;
        return lResult;
    }
}

uint8_t ioHomeParameterIndex(const IoHomeProfileDescriptor *iDescriptor,
                             ParameterSemantic iSemantic)
{
    if (!iDescriptor || iSemantic == ParameterSemantic::Unsupported ||
        iSemantic == ParameterSemantic::Unknown)
        return 0xFF;
    if (iDescriptor->mp == iSemantic)
        return 0;
    for (uint8_t i = 0; i < iDescriptor->fp.size(); i++)
    {
        if (iDescriptor->fp[i] == iSemantic)
            return i + 1;
    }
    return 0xFF;
}

IoHomeGenericCapabilities ioHomeProfileCapabilities(
    const IoHomeProfileDescriptor *iDescriptor)
{
    IoHomeGenericCapabilities lCapabilities;
    if (!iDescriptor)
        return lCapabilities;
    const uint32_t lFlags = iDescriptor->capabilityFlags;
    lCapabilities.position = (lFlags & IoHomeCapabilityPosition) != 0;
    lCapabilities.velocity = (lFlags & IoHomeCapabilitySpeed) != 0;
    lCapabilities.tilt = (lFlags & IoHomeCapabilityOrientation) != 0;
    lCapabilities.tiltVelocity = (lFlags & IoHomeCapabilityOrientationSpeed) != 0;
    lCapabilities.light = (lFlags & IoHomeCapabilityLight) != 0;
    lCapabilities.lock = (lFlags & IoHomeCapabilityLock) != 0;
    lCapabilities.onOff = (lFlags & IoHomeCapabilitySwitch) != 0;
    lCapabilities.ventilation = (lFlags & IoHomeCapabilityVentilation) != 0;
    lCapabilities.heating = (lFlags & IoHomeCapabilityHeating) != 0;
    lCapabilities.dualCurtain = (lFlags & IoHomeCapabilityDualCurtain) != 0;
    return lCapabilities;
}

const char *ioHomeParameterSemanticName(ParameterSemantic iSemantic)
{
    switch (iSemantic)
    {
    case ParameterSemantic::Unsupported: return "unsupported";
    case ParameterSemantic::Position: return "position";
    case ParameterSemantic::LinearSpeed: return "linear-speed";
    case ParameterSemantic::AngularSpeed: return "angular-speed";
    case ParameterSemantic::SlatOrientation: return "slat-orientation";
    case ParameterSemantic::SlatOrientationSpeed: return "slat-orientation-speed";
    case ParameterSemantic::CurtainPosition: return "curtain-position";
    case ParameterSemantic::UpperCurtainPosition: return "upper-curtain-position";
    case ParameterSemantic::LowerCurtainPosition: return "lower-curtain-position";
    case ParameterSemantic::HangerOrientation: return "hanger-orientation";
    case ParameterSemantic::HangerOrientationSpeed: return "hanger-orientation-speed";
    case ParameterSemantic::LightIntensity: return "light-intensity";
    case ParameterSemantic::LightIntensityGradient: return "light-intensity-gradient";
    case ParameterSemantic::LockState: return "lock-state";
    case ParameterSemantic::SwitchState: return "switch-state";
    case ParameterSemantic::AirDemand: return "air-demand";
    case ParameterSemantic::EnergyDemand: return "energy-demand";
    case ParameterSemantic::EnergyGradient: return "energy-gradient";
    case ParameterSemantic::ShutterClosure: return "shutter-closure";
    default: return "unknown";
    }
}

uint16_t ioHomePercentToRaw(float iPercent, ParameterPolarity iPolarity)
{
    if (iPercent < 0.0f) iPercent = 0.0f;
    if (iPercent > 100.0f) iPercent = 100.0f;
    if (iPolarity == ParameterPolarity::Reversed)
        iPercent = 100.0f - iPercent;
    return static_cast<uint16_t>((iPercent * IOHC_POSITION_MAX / 100.0f) + 0.5f);
}

bool ioHomeRawToPercent(uint16_t iRaw, ParameterPolarity iPolarity,
                        float &oPercent)
{
    if (iRaw > IOHC_POSITION_MAX)
        return false;
    oPercent = static_cast<float>(iRaw) * 100.0f / IOHC_POSITION_MAX;
    if (iPolarity == ParameterPolarity::Reversed)
        oPercent = 100.0f - oPercent;
    return true;
}
