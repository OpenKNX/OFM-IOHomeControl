#pragma once

#include <stdint.h>
#include <string.h>

#include "IoHomeCommands.h"
#include "IoHomeCrypto.h"
#include "IoHomeFrame.h"

enum class IoHomePassiveAuthResult : uint8_t
{
    None,
    Incomplete,
    Verified,
    HmacMismatch,
    Expired,
    CorrelationFailed,
    WrongPayload,
};

inline const char *ioHomePassiveAuthResultName(IoHomePassiveAuthResult iResult)
{
    switch (iResult)
    {
    case IoHomePassiveAuthResult::None: return "none";
    case IoHomePassiveAuthResult::Incomplete: return "incomplete";
    case IoHomePassiveAuthResult::Verified: return "verified";
    case IoHomePassiveAuthResult::HmacMismatch: return "wrong_hmac";
    case IoHomePassiveAuthResult::Expired: return "expired_transaction";
    case IoHomePassiveAuthResult::CorrelationFailed: return "correlation_failed";
    case IoHomePassiveAuthResult::WrongPayload: return "wrong_payload";
    }
    return "unknown";
}

inline bool ioHomeIsEligiblePassiveCandidate(uint32_t iNodeId,
                                             uint32_t iHubNodeId,
                                             uint32_t iExtractionNodeId,
                                             uint32_t iOwnNodeId)
{
    return iNodeId != 0 && iNodeId != iHubNodeId &&
           iNodeId != iExtractionNodeId && iNodeId != iOwnNodeId &&
           getAddressClass(iNodeId) == IoHomeAddressClass::Unicast;
}

inline uint8_t ioHomePreferredDirectedFrequencyIndex(uint8_t iRequestFreqIdx,
                                                       uint8_t iLastRxFreqIdx)
{
    if (iRequestFreqIdx < IOHC_NUM_FREQUENCIES)
        return iRequestFreqIdx;
    return iLastRxFreqIdx < IOHC_NUM_FREQUENCIES ? iLastRxFreqIdx : 0xFF;
}

// One ordered KLR/device authentication exchange. The caller keeps one of
// these per candidate node, so interleaved exchanges cannot share a challenge.
struct IoHomePassiveAuthEvidence
{
    static constexpr uint32_t kStageTimeoutMs = 10000UL;

    uint32_t requestAtMs = 0;
    uint32_t challengeAtMs = 0;
    uint32_t authAtMs = 0;
    uint32_t finalAtMs = 0;
    uint8_t requestFreqIdx = 0xFF;
    uint8_t challengeFreqIdx = 0xFF;
    uint8_t authFreqIdx = 0xFF;
    uint8_t finalFreqIdx = 0xFF;
    uint8_t requestPayload = 0;
    uint8_t challenge[6] = {};
    uint8_t responseHmac[6] = {};
    uint8_t computedHmac[6] = {};
    bool requestSeen = false;
    bool challengeSeen = false;
    bool authSeen = false;
    bool finalSeen = false;
    bool authVerified = false;
    bool hmacComputed = false;
    IoHomePassiveAuthResult result = IoHomePassiveAuthResult::None;

    const char *diagnosticReason() const
    {
        if (result == IoHomePassiveAuthResult::Expired ||
            result == IoHomePassiveAuthResult::HmacMismatch ||
            result == IoHomePassiveAuthResult::CorrelationFailed ||
            result == IoHomePassiveAuthResult::WrongPayload)
            return ioHomePassiveAuthResultName(result);
        if (authVerified)
            return "verified";
        if (!requestSeen)
            return "missing_request";
        if (!challengeSeen)
            return "missing_challenge";
        if (!authSeen)
            return "missing_auth";
        if (!finalSeen)
            return "missing_final";
        return "awaiting_key";
    }

    void observe(const IoHomeFrame &iFrame, uint32_t iHubNodeId,
                 uint32_t iDeviceNodeId, uint8_t iFrequencyIndex,
                 uint32_t iTimestampMs)
    {
        const bool lFromHub = iFrame.getSrcNodeId() == iHubNodeId &&
                              iFrame.getDestNodeId() == iDeviceNodeId;
        const bool lToHub = iFrame.getSrcNodeId() == iDeviceNodeId &&
                            iFrame.getDestNodeId() == iHubNodeId;
        if (!lFromHub && !lToHub)
        {
            result = IoHomePassiveAuthResult::CorrelationFailed;
            return;
        }

        switch (iFrame.commandId)
        {
        case IoHomeCommand::Discover2ERequest:
            if (!lFromHub)
            {
                result = IoHomePassiveAuthResult::CorrelationFailed;
                return;
            }
            if (iFrame.dataLen != 1 || iFrame.data[0] != 0x02)
            {
                result = IoHomePassiveAuthResult::WrongPayload;
                return;
            }
            *this = IoHomePassiveAuthEvidence{};
            requestSeen = true;
            requestPayload = iFrame.data[0];
            requestAtMs = iTimestampMs;
            requestFreqIdx = iFrequencyIndex;
            result = IoHomePassiveAuthResult::Incomplete;
            return;

        case IoHomeCommand::ChallengeRequest:
            if (!lToHub || !requestSeen || challengeSeen)
            {
                result = IoHomePassiveAuthResult::CorrelationFailed;
                return;
            }
            if (iTimestampMs - requestAtMs > kStageTimeoutMs)
            {
                result = IoHomePassiveAuthResult::Expired;
                return;
            }
            if (iFrame.dataLen != sizeof(challenge))
            {
                result = IoHomePassiveAuthResult::WrongPayload;
                return;
            }
            memcpy(challenge, iFrame.data, sizeof(challenge));
            challengeSeen = true;
            challengeAtMs = iTimestampMs;
            challengeFreqIdx = iFrequencyIndex;
            return;

        case IoHomeCommand::ChallengeResponse:
            if (!lFromHub || !challengeSeen || authSeen)
            {
                result = IoHomePassiveAuthResult::CorrelationFailed;
                return;
            }
            if (iTimestampMs - challengeAtMs > kStageTimeoutMs)
            {
                result = IoHomePassiveAuthResult::Expired;
                return;
            }
            if (iFrame.dataLen != sizeof(responseHmac))
            {
                result = IoHomePassiveAuthResult::WrongPayload;
                return;
            }
            memcpy(responseHmac, iFrame.data, sizeof(responseHmac));
            authSeen = true;
            authAtMs = iTimestampMs;
            authFreqIdx = iFrequencyIndex;
            return;

        case IoHomeCommand::Discover2EResponse:
            if (!lToHub || !authSeen || finalSeen)
            {
                result = IoHomePassiveAuthResult::CorrelationFailed;
                return;
            }
            if (iTimestampMs - authAtMs > kStageTimeoutMs)
            {
                result = IoHomePassiveAuthResult::Expired;
                return;
            }
            if (iFrame.dataLen != 1 || iFrame.data[0] != requestPayload ||
                (iFrame.ctrlByte0 & IOHC_CTRL0_END) == 0 ||
                (iFrame.ctrlByte0 & IOHC_CTRL0_START) != 0)
            {
                result = IoHomePassiveAuthResult::WrongPayload;
                return;
            }
            finalSeen = true;
            finalAtMs = iTimestampMs;
            finalFreqIdx = iFrequencyIndex;
            return;

        default:
            return;
        }
    }

    bool verify(const uint8_t iSystemKey[16])
    {
        if (!requestSeen || !challengeSeen || !authSeen || !finalSeen ||
            result == IoHomePassiveAuthResult::Expired)
            return false;
        const uint8_t lTranscript[] = {
            static_cast<uint8_t>(IoHomeCommand::Discover2ERequest), requestPayload};
        hmacComputed = IoHomeCrypto::createHmac2W(
            lTranscript, sizeof(lTranscript), challenge, iSystemKey, computedHmac);
        authVerified = hmacComputed && IoHomeCrypto::verifyHmac(
            lTranscript, sizeof(lTranscript), responseHmac, challenge, iSystemKey);
        result = authVerified ? IoHomePassiveAuthResult::Verified
                              : IoHomePassiveAuthResult::HmacMismatch;
        return authVerified;
    }
};
