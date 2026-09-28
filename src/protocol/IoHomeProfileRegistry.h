#pragma once

#include <array>
#include <stdint.h>

#include "IoHomeCommands.h"

// KLF200 API v3.18, Appendix 2, Table 276. These are parameter meanings,
// independent of manufacturer, product family, and the still-unconfirmed RF
// source of NodeClass. FP array index 0 represents FP1.
enum class ParameterSemantic : uint8_t
{
    Unsupported = 0,
    Position,
    LinearSpeed,
    AngularSpeed,
    SlatOrientation,
    SlatOrientationSpeed,
    CurtainPosition,
    UpperCurtainPosition,
    LowerCurtainPosition,
    HangerOrientation,
    HangerOrientationSpeed,
    LightIntensity,
    LightIntensityGradient,
    LockState,
    SwitchState,
    AirDemand,
    EnergyDemand,
    EnergyGradient,
    ShutterClosure,
    Unknown
};

enum class ParameterPolarity : uint8_t
{
    Normal,  // 0x0000 = 0%, 0xC800 = 100%
    Reversed // 0x0000 = 100%, 0xC800 = 0%
};

enum IoHomeProfileCapability : uint32_t
{
    IoHomeCapabilityPosition = 1UL << 0,
    IoHomeCapabilitySpeed = 1UL << 1,
    IoHomeCapabilityOrientation = 1UL << 2,
    IoHomeCapabilityOrientationSpeed = 1UL << 3,
    IoHomeCapabilityLight = 1UL << 4,
    IoHomeCapabilityLock = 1UL << 5,
    IoHomeCapabilitySwitch = 1UL << 6,
    IoHomeCapabilityVentilation = 1UL << 7,
    IoHomeCapabilityHeating = 1UL << 8,
    IoHomeCapabilityDualCurtain = 1UL << 9
};

struct IoHomeProfileDescriptor
{
    uint16_t profile;
    uint8_t subProfile;
    // A documentation hint only; lookup never changes an observed NodeClass.
    IoHomeNodeClass expectedClass;
    const char *label;
    ParameterSemantic mp;
    std::array<ParameterSemantic, 16> fp;
    uint32_t capabilityFlags;
    ParameterPolarity mpPolarity;
    bool securedVentilation;
};

// Returns nullptr for a profile absent from Appendix 2. Callers should keep
// the captured identity and treat its MP/FP meanings as Unknown.
const IoHomeProfileDescriptor *ioHomeProfileDescriptor(uint16_t iProfile,
                                                        uint8_t iSubProfile);
const IoHomeProfileDescriptor *ioHomeProfileDescriptor(const IoHomeProtocolIdentity &iIdentity);

// Index 0 is MP, 1..16 are FP1..FP16. A known but undefined FP is Unsupported;
// an unknown profile or invalid index is Unknown.
ParameterSemantic ioHomeParameterSemantic(const IoHomeProfileDescriptor *iDescriptor,
                                          uint8_t iParameterIndex);
uint8_t ioHomeParameterIndex(const IoHomeProfileDescriptor *iDescriptor,
                             ParameterSemantic iSemantic);
const char *ioHomeParameterSemanticName(ParameterSemantic iSemantic);
IoHomeGenericCapabilities ioHomeProfileCapabilities(
    const IoHomeProfileDescriptor *iDescriptor);
uint16_t ioHomePercentToRaw(float iPercent, ParameterPolarity iPolarity);
bool ioHomeRawToPercent(uint16_t iRaw, ParameterPolarity iPolarity,
                        float &oPercent);

inline bool ioHomeIsOrientationSemantic(ParameterSemantic iSemantic)
{
    return iSemantic == ParameterSemantic::SlatOrientation ||
           iSemantic == ParameterSemantic::HangerOrientation;
}

inline bool ioHomeIsPositionSemantic(ParameterSemantic iSemantic)
{
    return iSemantic == ParameterSemantic::Position ||
           iSemantic == ParameterSemantic::CurtainPosition ||
           iSemantic == ParameterSemantic::UpperCurtainPosition ||
           iSemantic == ParameterSemantic::LowerCurtainPosition ||
           iSemantic == ParameterSemantic::ShutterClosure;
}

inline bool ioHomeIsSpeedSemantic(ParameterSemantic iSemantic)
{
    return iSemantic == ParameterSemantic::LinearSpeed ||
           iSemantic == ParameterSemantic::AngularSpeed ||
           iSemantic == ParameterSemantic::SlatOrientationSpeed ||
           iSemantic == ParameterSemantic::HangerOrientationSpeed;
}

// The captured status selector addresses FP3; use the general profile index
// for commands that target another orientation slot.
inline bool ioHomeSupportsCapturedFp3Orientation(
    const IoHomeProfileDescriptor *iDescriptor)
{
    return iDescriptor &&
           ioHomeIsOrientationSemantic(ioHomeParameterSemantic(iDescriptor, 3));
}

inline bool ioHomeSupportsCapturedFp3Orientation(
    const IoHomeProtocolIdentity &iIdentity)
{
    // Unenriched legacy/1W channels retain their configured behavior. A valid
    // but unlisted discovery identity has no inferred FP semantics.
    return !iIdentity.valid ||
           ioHomeSupportsCapturedFp3Orientation(
               ioHomeProfileDescriptor(iIdentity));
}
