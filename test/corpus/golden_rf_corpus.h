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

struct OneWayEnrollmentReference
{
    const char *id;
    const char *provenance;
    uint8_t manufacturer;
    uint8_t acei;
    uint8_t addDestinationCount;
    uint32_t addDestinations[4];
    uint32_t finalizerDestination;
    uint16_t stopMain;
    uint16_t downMain;
    uint16_t stopDownDeadlineMs;
    uint8_t repeatCountAfterFirst;
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

// Public KLI-compatible 0x30 wire shape. This is a source-derived regression
// reference, not a claimed RF capture: source, wrapped key and sequence are
// deliberately masked. The stable fields pin CTRL flags, ALL destination,
// command, VELUX manufacturer and the 0x01 enrollment marker.
static const uint8_t kKli310AddShape[] = {
    0xFC, 0x20, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x30,
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
    4,
    {0x00003F, 0x0000BF, 0x0000FF, 0x00037F},
    0x00003F,
    0xD200,
    0xC800,
    3000,
    4,
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

static const Frame kFrames[] = {
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

static constexpr uint8_t frameCount = sizeof(kFrames) / sizeof(kFrames[0]);
static constexpr uint8_t pairingWaitInjectionCount = sizeof(kPairingWaitInjections) / sizeof(kPairingWaitInjections[0]);
static constexpr uint8_t scenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);

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
