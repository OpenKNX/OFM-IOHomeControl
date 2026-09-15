#pragma once

#include <stdint.h>

#include "protocol/IoHomeCommands.h"

// Golden RF fixtures are deliberately self-contained C++ data instead of files
// loaded at test time. This keeps native and CI validation deterministic while
// retaining enough metadata to trace each fixture back to its provenance.
//
// All node IDs, encrypted keys and MACs below are public test-vector or
// re-keyed values. Never add an observed controller key, system key, or an
// unredacted pairing exchange here.
namespace IoHomeGoldenRfCorpus
{
enum class RadioPath : uint8_t
{
    SX1276,
    SX1262,
    LR1121,
    ProtocolOnly,
};

enum class CryptoExpectation : uint8_t
{
    NoCrypto,
    HmacPresentKeyRedacted,
    VerifyPublicOneWayTrailer,
    PairingCorrelationMustReject,
};

struct Frame
{
    const char *id;
    const char *scenario;
    const char *provenance;
    RadioPath radio;
    const uint8_t *bytes;
    uint8_t wireLen;
    IoHomeCommand command;
    uint32_t source;
    uint32_t destination;
    bool oneWay;
    bool hasHmac;
    bool hasTrailerMac;
    CryptoExpectation crypto;
};

struct PairingWaitInjection
{
    const char *waitState;
    const char *unrelatedFrameId;
    const char *expectedOutcome;
};

struct Scenario
{
    const char *id;
    const char *fixtureId;
    RadioPath radio;
    const char *expectedOutcome;
};

struct MaskedFrameReference
{
    const char *id;
    const char *provenance;
    const uint8_t *bytes;
    const uint8_t *compareMask;
    uint8_t wireLen;
};

struct RawFrameReference
{
    const char *id;
    const char *scenario;
    const char *provenance;
    const uint8_t *bytes;
    uint8_t wireLen;
    IoHomeCommand command;
    uint32_t source;
    uint32_t destination;
    uint8_t declaredLen;
    uint8_t protocolLen;
    bool hasHmac;
    bool hasTrailerMac;
    uint16_t crc;
};

struct OneWayEnrollmentReference
{
    const char *id;
    const char *provenance;
    uint8_t manufacturer;
    uint8_t acei;
    uint32_t removeDestination;
    uint8_t addDestinationCount;
    uint32_t addDestinations[4];
    uint32_t finalizerDestination;
    uint16_t stopMain;
    uint16_t downMain;
    uint16_t stopDownDeadlineMs;
    uint8_t repeatCountAfterFirst;
    bool pairingLowPower;
};

// Re-keyed, non-secret equivalent of the observed Smoove Remove -> SendKey
// sequence. Its 0x30 payload is from the long-standing public crypto vector.
static const uint8_t kSmooveRemoveController[] = {
    0x71, 0x00, 0x00, 0x00, 0x3F, 0xB6, 0x0D, 0x1A, 0x39,
    0x02, 0x00, 0x01, 0x61, 0x7A, 0x20, 0x4C, 0x91, 0xD3,
};

static const uint8_t kSmooveSendKeyNoMac[] = {
    0x7C, 0x00, 0x00, 0x00, 0x3F, 0xB6, 0x0D, 0x1A, 0x30,
    0x2D, 0x36, 0xBD, 0x8B, 0x4D, 0x4F, 0xB1, 0xE1,
    0xA1, 0xB3, 0x09, 0x9B, 0x39, 0x4D, 0x3A, 0x9E,
    0x01, 0x01, 0x12, 0x34,
};

// Public 1W crypto KAT: the trailer validates with kPublicTrailerVectorKey.
static const uint8_t kSendKeyWithMac[] = {
    0x7C, 0x00, 0x00, 0x00, 0xBF, 0x48, 0x5B, 0x37, 0x30,
    0x82, 0x60, 0x89, 0xF3, 0x44, 0xCB, 0xCC, 0xAB,
    0x84, 0x26, 0xCE, 0x7C, 0x12, 0x51, 0xB8, 0xE0,
    0x01, 0x01, 0x1A, 0x2B,
    0x2D, 0x49, 0x8F, 0xBF, 0x1F, 0x7C,
};

// Velocet/iown-homecontrol Issue #36 documentation/reference vectors. The
// displayed source NID ABCDEF is a placeholder, so neither entry is classified
// as a raw OTA capture. They pin the corrected 29-byte MAC-less form and the
// legacy out-of-declared-length trailer form respectively.
static const uint8_t kIssue36SendKeyNoTrailerReference[] = {
    0xFC, 0x00, 0x00, 0x00, 0x3F, 0xAB, 0xCD, 0xEF, 0x30,
    0x7E, 0x60, 0x49, 0x1F, 0x97, 0x6A, 0xDF, 0x65,
    0x3D, 0xB0, 0xED, 0x78, 0x5E, 0x49, 0xA2, 0x01,
    0x02, 0x01, 0x12, 0x34,
    0x39, 0x11,
};

static const uint8_t kIssue36SendKeyTrailerReference[] = {
    0xFC, 0x00, 0x00, 0x00, 0x3F, 0xAB, 0xCD, 0xEF, 0x30,
    0x7E, 0x60, 0x49, 0x1F, 0x97, 0x6A, 0xDF, 0x65,
    0x3D, 0xB0, 0xED, 0x78, 0x5E, 0x49, 0xA2, 0x01,
    0x02, 0x01, 0x12, 0x34,
    0x19, 0xE8, 0x1E, 0xC4, 0x3D, 0x5E,
    0x9B, 0xF2,
};

static const RawFrameReference kIssue36SendKeyReferences[] = {
    {"issue36_sendkey_no_trailer_reference", "issue36_sendkey_documentation",
     "Velocet/iown-homecontrol Issue #36 documentation/reference vector; corrected MAC-less 0x30 example; ABCDEF is a placeholder source NID, not a raw OTA capture",
     kIssue36SendKeyNoTrailerReference, sizeof(kIssue36SendKeyNoTrailerReference),
     IoHomeCommand::SendKey1W, 0xABCDEF, 0x00003F, 29, 29, false, false, 0x1139},
    {"issue36_sendkey_trailer_reference", "issue36_sendkey_documentation",
     "Velocet/iown-homecontrol Issue #36 legacy documentation/reference example; validates out-of-declared-length trailer framing only, not that the trailer is mandatory",
     kIssue36SendKeyTrailerReference, sizeof(kIssue36SendKeyTrailerReference),
     IoHomeCommand::SendKey1W, 0xABCDEF, 0x00003F, 29, 35, false, true, 0xF29B},
};

// Public KLI-compatible 0x30 wire shape. This is a source-derived regression
// reference, not a claimed RF capture: source, wrapped key and sequence are
// deliberately masked. The stable fields pin CTRL flags, the roller-shutter
// destination, command, VELUX manufacturer and the 0x01 enrollment marker.
// A real KLI 310 sends CTRL1=0x00 (no LOW_POWER) on its enrollment frames.
static const uint8_t kKli310AddShape[] = {
    0xFC, 0x00, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00,
};

static const uint8_t kKli310AddShapeMask[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00,
};

static const MaskedFrameReference kKli310AddReference = {
    "kli310_add_controller_shape",
    "public KLI behavior from samr037/iohc-flipper tx_runner.c; variable fields masked",
    kKli310AddShape, kKli310AddShapeMask, sizeof(kKli310AddShape),
};

static const OneWayEnrollmentReference kKli310EnrollmentReference = {
    "kli310_remove_multicast_add_stop_down",
    "public KLI behavior from samr037/iohc-flipper tx_runner.c and VELUX registration instructions",
    0x01,
    0x61,
    0x00003F,
    3,
    {0x0000BF, 0x0000FF, 0x00037F, 0},
    0x00003F,
    0xD200,
    0xC800,
    3000,
    4,
    false,
};

static const uint8_t kPublicTrailerVectorKey[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
};

// Sanitized shapes from the 2W retry/extraction paths. They contain no key
// material and are useful both for parser regression coverage and as foreign
// frames injected while the controller waits for a bound peer.
static const uint8_t kRs100ForeignChallenge[] = {
    0x0E, 0x00, 0x12, 0x34, 0x56, 0x65, 0x43, 0x21, 0x3C,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
};

static const uint8_t kRs100KeyTransferConfirmation[] = {
    0x08, 0x00, 0xA1, 0xB2, 0xC3, 0x65, 0x43, 0x21, 0x33,
};

static const uint8_t kExtractionAddressResponse[] = {
    0x0E, 0x00, 0xA1, 0xB2, 0xC3, 0x65, 0x43, 0x21, 0x37,
    0xA1, 0xB2, 0xC3, 0x00, 0x00, 0x01,
};

// Sanitized KLR300 controller-role pairing/search sequence captured on
// 2026-09-12.  Node IDs are the published capture shape; challenge/HMAC/key
// bytes below are synthetic public placeholders.  The stable regression
// targets are command, endpoints, CTRL1 and declared payload length.
static const uint8_t kKlr300Discover28Ack[] = {
    0xC8, 0x10, 0x00, 0x00, 0x3B, 0xE2, 0xD1, 0xFF, 0x28,
};

static const uint8_t kKlr300Discover2EBroadcast[] = {
    0xC9, 0x20, 0x00, 0x00, 0x3F, 0xE2, 0xD1, 0xFF, 0x2E, 0x00,
};

static const uint8_t kKlr300DiscoveryConfirmation[] = {
    0x48, 0x20, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x2C,
};

static const uint8_t kKlr300KeyInit[] = {
    0x48, 0x20, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x31,
};

static const uint8_t kKlr300KeyTransfer[] = {
    0x18, 0x00, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x32,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

static const uint8_t kKlr300DiscoverSpe[] = {
    0xD4, 0x30, 0x00, 0x00, 0x3B, 0xE2, 0xD1, 0xFF, 0x2A,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
};

static const uint8_t kKlr300Discover2EDirected[] = {
    0x49, 0x20, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x2E, 0x02,
};

static const uint8_t kKlr300ChallengeResponse[] = {
    0x0E, 0x00, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x3D,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
};

static const uint8_t kKlr300AddressRequest[] = {
    0x48, 0x24, 0x7E, 0x9E, 0x6E, 0xE2, 0xD1, 0xFF, 0x36,
};

static const Frame kFrames[] = {
    {"klr300_discover_28_ack", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; ACK bit and header preserved", RadioPath::ProtocolOnly,
     kKlr300Discover28Ack, sizeof(kKlr300Discover28Ack), IoHomeCommand::DiscoverRequest,
     0xE2D1FF, 0x00003B, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_discover_2e_broadcast", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture", RadioPath::ProtocolOnly,
     kKlr300Discover2EBroadcast, sizeof(kKlr300Discover2EBroadcast), IoHomeCommand::Discover2ERequest,
     0xE2D1FF, 0x00003F, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_discovery_confirmation", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; peer address substituted", RadioPath::ProtocolOnly,
     kKlr300DiscoveryConfirmation, sizeof(kKlr300DiscoveryConfirmation), IoHomeCommand::Confirmation,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_key_init", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; peer address substituted", RadioPath::ProtocolOnly,
     kKlr300KeyInit, sizeof(kKlr300KeyInit), IoHomeCommand::KeyInitTransfer,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_key_transfer", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; encrypted key replaced", RadioPath::ProtocolOnly,
     kKlr300KeyTransfer, sizeof(kKlr300KeyTransfer), IoHomeCommand::KeyTransfer,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_discover_spe", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; challenge and HMAC replaced", RadioPath::ProtocolOnly,
     kKlr300DiscoverSpe, sizeof(kKlr300DiscoverSpe), IoHomeCommand::DiscoverSPERequest,
     0xE2D1FF, 0x00003B, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_discover_2e_directed", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; peer address substituted", RadioPath::ProtocolOnly,
     kKlr300Discover2EDirected, sizeof(kKlr300Discover2EDirected), IoHomeCommand::Discover2ERequest,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_challenge_response", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; HMAC replaced", RadioPath::ProtocolOnly,
     kKlr300ChallengeResponse, sizeof(kKlr300ChallengeResponse), IoHomeCommand::ChallengeResponse,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"klr300_address_request", "klr300_pairing_search_2026_09_12",
     "sanitized KLR300 capture; unknown CTRL1 bit 0x04 preserved without semantics", RadioPath::ProtocolOnly,
     kKlr300AddressRequest, sizeof(kKlr300AddressRequest), IoHomeCommand::AddressRequest,
     0xE2D1FF, 0x7E9E6E, false, false, false, CryptoExpectation::NoCrypto},
    {"smoove_remove_controller", "smoove_remove_add_sx1276",
     "sanitized/re-keyed August Smoove remove-add shape", RadioPath::SX1276,
     kSmooveRemoveController, sizeof(kSmooveRemoveController), IoHomeCommand::RemoveController,
     0xB60D1A, 0x00003F, true, true, false, CryptoExpectation::HmacPresentKeyRedacted},
    {"smoove_sendkey_no_mac", "smoove_remove_add_sx1276",
     "sanitized public 1W SendKey vector; no trailer", RadioPath::SX1276,
     kSmooveSendKeyNoMac, sizeof(kSmooveSendKeyNoMac), IoHomeCommand::SendKey1W,
     0xB60D1A, 0x00003F, true, false, false, CryptoExpectation::NoCrypto},
    {"dimmer_sendkey_with_mac", "dimmer_add_controller_sx1276",
     "sanitized public 1W trailer-MAC crypto vector", RadioPath::SX1276,
     kSendKeyWithMac, sizeof(kSendKeyWithMac), IoHomeCommand::SendKey1W,
     0x485B37, 0x0000BF, true, false, true, CryptoExpectation::VerifyPublicOneWayTrailer},
    {"rs100_foreign_challenge", "rs100_key_transfer_timeout_and_retry_sx1262",
     "sanitized August RS100 foreign-frame injection shape", RadioPath::SX1262,
     kRs100ForeignChallenge, sizeof(kRs100ForeignChallenge), IoHomeCommand::ChallengeRequest,
     0x654321, 0x123456, false, false, false, CryptoExpectation::PairingCorrelationMustReject},
    {"rs100_key_transfer_confirmation", "rs100_key_transfer_timeout_and_retry_sx1262",
     "sanitized August RS100 retry-confirmation shape", RadioPath::SX1262,
     kRs100KeyTransferConfirmation, sizeof(kRs100KeyTransferConfirmation), IoHomeCommand::KeyTransferConfirmation,
     0x654321, 0xA1B2C3, false, false, false, CryptoExpectation::NoCrypto},
    {"kig300_klr200_address_response", "key_extraction_address_verification",
     "sanitized KIG300/KLR200 responder address-verification shape", RadioPath::ProtocolOnly,
     kExtractionAddressResponse, sizeof(kExtractionAddressResponse), IoHomeCommand::AddressResponse,
     0x654321, 0xA1B2C3, false, false, false, CryptoExpectation::NoCrypto},
};

static const PairingWaitInjection kPairingWaitInjections[] = {
    {"PairWaitDiscoveryResponse", "rs100_foreign_challenge", "ignore and keep scanning"},
    {"PairWaitDeviceChallenge", "rs100_foreign_challenge", "ignore and keep waiting"},
    {"PairWaitKeyTransferConfirmation", "rs100_foreign_challenge", "ignore and keep waiting"},
    {"PairWaitSetConfig1Response", "rs100_foreign_challenge", "ignore and keep waiting"},
    {"PairWaitSetConfig1FinalResponse", "rs100_foreign_challenge", "ignore and keep waiting"},
};

// A scenario may share an RF frame with another radio path when the frame is
// protocol-identical. The radio field preserves the capture context without
// implying that OFM has a driver for that radio (notably LR1121).
static const Scenario kScenarios[] = {
    {"smoove_remove_add_sx1276", "smoove_remove_controller", RadioPath::SX1276,
     "0x39 then 29-byte 0x30"},
    {"sendkey_no_mac", "smoove_sendkey_no_mac", RadioPath::SX1276,
     "declared length is 29 and no trailer is present"},
    {"sendkey_with_mac", "dimmer_sendkey_with_mac", RadioPath::SX1276,
     "declared length remains 29 and trailer MAC verifies"},
    {"rs100_key_transfer_timeout_sx1262", "rs100_foreign_challenge", RadioPath::SX1262,
     "foreign frame is ignored while key transfer times out"},
    {"rs100_key_transfer_retry_success_sx1262", "rs100_key_transfer_confirmation", RadioPath::SX1262,
     "bound-peer confirmation completes the retry"},
    {"dimmer_pairing_sx1276", "dimmer_sendkey_with_mac", RadioPath::SX1276,
     "protocol frame is decoded with the recorded radio attribution"},
    {"dimmer_pairing_sx1262", "dimmer_sendkey_with_mac", RadioPath::SX1262,
     "protocol frame is decoded with the recorded radio attribution"},
    {"dimmer_pairing_lr1121", "dimmer_sendkey_with_mac", RadioPath::LR1121,
     "protocol fixture retained; hardware replay awaits an OFM LR1121 driver"},
    {"kig300_key_extraction_address_verification", "kig300_klr200_address_response", RadioPath::ProtocolOnly,
     "address response is available for responder address verification"},
    {"klr200_key_extraction_address_verification", "kig300_klr200_address_response", RadioPath::ProtocolOnly,
     "address response is available for responder address verification"},
};

static const char *const kKlr300PairingSearchSequence[] = {
    "klr300_discover_2e_broadcast",
    "klr300_discover_28_ack",
    "klr300_discovery_confirmation",
    "klr300_key_init",
    "klr300_key_transfer",
    "klr300_discover_spe",
    "klr300_discover_2e_directed",
    "klr300_challenge_response",
    "klr300_address_request",
};

static constexpr uint8_t frameCount = sizeof(kFrames) / sizeof(kFrames[0]);
static constexpr uint8_t issue36SendKeyReferenceCount =
    sizeof(kIssue36SendKeyReferences) / sizeof(kIssue36SendKeyReferences[0]);
static constexpr uint8_t pairingWaitInjectionCount = sizeof(kPairingWaitInjections) / sizeof(kPairingWaitInjections[0]);
static constexpr uint8_t scenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);
static constexpr uint8_t klr300PairingSearchSequenceCount =
    sizeof(kKlr300PairingSearchSequence) / sizeof(kKlr300PairingSearchSequence[0]);

inline const Frame *findFrame(const char *iId)
{
    if (!iId)
        return nullptr;
    for (uint8_t i = 0; i < frameCount; i++)
    {
        const char *lCandidate = kFrames[i].id;
        uint8_t lPos = 0;
        while (lCandidate[lPos] == iId[lPos] && lCandidate[lPos] != '\0')
            lPos++;
        if (lCandidate[lPos] == iId[lPos])
            return &kFrames[i];
    }
    return nullptr;
}

inline bool matchesMaskedReference(const uint8_t *iActual, uint8_t iActualLen,
                                   const MaskedFrameReference &iReference)
{
    if (!iActual || iActualLen != iReference.wireLen)
        return false;
    for (uint8_t i = 0; i < iReference.wireLen; ++i)
    {
        if ((iActual[i] & iReference.compareMask[i]) !=
            (iReference.bytes[i] & iReference.compareMask[i]))
            return false;
    }
    return true;
}
} // namespace IoHomeGoldenRfCorpus
