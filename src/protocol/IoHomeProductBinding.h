#pragma once
#include "IoHomeCommands.h"

// Semantic-family binding, not a commercial model or a database typeId.
// The database's generation/variant bits are not inferred from discovery.
// No binding grants write permission or replaces expert configuration.
enum class IoHomeBoundProductFamily : uint8_t
{
    Unknown, RgbLight, TunableWhiteLight,
    AtlanticPassApcHeatPump, AtlanticPassApcHybrid
};

inline IoHomeBoundProductFamily ioHomeBindProductFamily(
    const IoHomeProtocolIdentity &iIdentity, const IoHomeProductIdentityEvidence &iEvidence)
{
    if (!iIdentity.valid || !iIdentity.fullMetadata ||
        iIdentity.nodeClass != IoHomeNodeClass::Actuator ||
        iEvidence.manufacturerSignatureInconsistent ||
        (iEvidence.generalInfo2TypeValid &&
         (iEvidence.generalInfo2Profile != iIdentity.profile ||
          iEvidence.generalInfo2SubProfile != iIdentity.subProfile)))
        return IoHomeBoundProductFamily::Unknown;
    // Retained C0.P6.1 / C0.P6.2 and corresponding SQLite definitions.
    // Scope to the manufacturer IDs actually present in those definitions.
    if (iIdentity.profile == 6)
    {
        if (iIdentity.subProfile == 1 &&
            (iIdentity.manufacturerId == 0 || iIdentity.manufacturerId == 2))
            return IoHomeBoundProductFamily::RgbLight;
        if (iIdentity.subProfile == 2 && iIdentity.manufacturerId == 2)
            return IoHomeBoundProductFamily::TunableWhiteLight;
    }
    // C0.P22.1 explicitly gates PassAPC on Atlantic manufacturer12.
    // This is NOT the generic normalized HeatPump temperature definition.
    if (iIdentity.profile == 22 && iIdentity.subProfile == 1 &&
        iIdentity.manufacturerId == 12 && iEvidence.generalInfo2Len >= 10)
    {
        const uint32_t lCode = (uint32_t(iEvidence.generalInfo2[7]) << 16) |
            (uint32_t(iEvidence.generalInfo2[8]) << 8) | iEvidence.generalInfo2[9];
        if (lCode == 0x620000) return IoHomeBoundProductFamily::AtlanticPassApcHeatPump;
        if (lCode == 0x520001) return IoHomeBoundProductFamily::AtlanticPassApcHybrid;
    }
    return IoHomeBoundProductFamily::Unknown;
}

inline const char *ioHomeBoundProductFamilyName(IoHomeBoundProductFamily iFamily)
{
    switch (iFamily)
    {
    case IoHomeBoundProductFamily::RgbLight: return "RGB light";
    case IoHomeBoundProductFamily::TunableWhiteLight: return "tunable-white light";
    case IoHomeBoundProductFamily::AtlanticPassApcHeatPump: return "Atlantic PassAPC heat pump";
    case IoHomeBoundProductFamily::AtlanticPassApcHybrid: return "Atlantic PassAPC hybrid";
    default: return "unmatched";
    }
}

struct IoHomeProductBindingResult
{
    enum class Reason:uint8_t { MatchedFamily, MissingIdentity, MissingClass, ConflictingEvidence, UnsupportedDefinition };
    IoHomeBoundProductFamily family=IoHomeBoundProductFamily::Unknown;
    Reason reason=Reason::MissingIdentity;
    // No retained RF evidence resolves commercial identity or generation bits.
    bool commercialModelKnown=false;
    bool generationKnown=false;
    bool rfWriteQualified=false;
};

inline IoHomeProductBindingResult ioHomeProductBindingResult(
    const IoHomeProtocolIdentity &identity,const IoHomeProductIdentityEvidence &evidence)
{
    IoHomeProductBindingResult result;
    if(!identity.valid || !identity.fullMetadata) return result;
    if(identity.nodeClass!=IoHomeNodeClass::Actuator)
    { result.reason=IoHomeProductBindingResult::Reason::MissingClass;return result; }
    if(evidence.manufacturerSignatureInconsistent ||
       (evidence.generalInfo2TypeValid && (evidence.generalInfo2Profile!=identity.profile || evidence.generalInfo2SubProfile!=identity.subProfile)))
    { result.reason=IoHomeProductBindingResult::Reason::ConflictingEvidence;return result; }
    result.family=ioHomeBindProductFamily(identity,evidence);
    result.reason=result.family==IoHomeBoundProductFamily::Unknown ?
        IoHomeProductBindingResult::Reason::UnsupportedDefinition:IoHomeProductBindingResult::Reason::MatchedFamily;
    return result;
}
