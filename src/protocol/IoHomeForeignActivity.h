#pragma once
#include "IoHomeFrame.h"

// Observations may request a later read. They never authorize state publication.
enum class IoHomeForeignKind : uint8_t { Read, Write, Continuation, Reply, Unknown };
enum class IoHomeFollowUpDecision : uint8_t { Ignored, Scheduled, Coalesced };

inline IoHomeForeignKind ioHomeForeignKind(const IoHomeFrame &frame)
{
    switch (frame.commandId) {
    case IoHomeCommand::Private: return IoHomeForeignKind::Read;
    case IoHomeCommand::ChallengeRequest:
    case IoHomeCommand::ChallengeResponse: return IoHomeForeignKind::Continuation;
    case IoHomeCommand::PrivateResponse:
    case IoHomeCommand::StatusUpdate:
    case IoHomeCommand::ErrorResponse: return IoHomeForeignKind::Reply;
    case IoHomeCommand::Execute:
        // Only the supported main-parameter Execute shape is actionable here.
        // Empty/truncated Execute and 2W continuations must not start tracking.
        {
            const bool oneWay=(frame.ctrlByte0 & IOHC_CTRL0_MODE_1W)!=0;
            const uint8_t size=oneWay && frame.dataLen>=2 ? frame.dataLen-2 : frame.dataLen;
            const bool supportedPayload=(size==6 && frame.data[4]==0 && frame.data[5]==0) ||
                (size==8 && frame.data[4]==0x80 && frame.data[7]==0);
            if (supportedPayload && (oneWay ? frame.hasHmac : (frame.ctrlByte0 & IOHC_CTRL0_START)!=0))
                return IoHomeForeignKind::Write;
        }
        return IoHomeForeignKind::Unknown;
    default: return IoHomeForeignKind::Unknown;
    }
}

struct IoHomeFollowUpState {
    uint32_t deadline, nextPoll;
    bool pending, statusExpected;
    uint8_t silentFailures, authFailures, movingMisses;
};

// Shared by the real channel and native tests: an active epoch owns its pending
// work and backoff, even after its first poll has left the queue.
inline IoHomeFollowUpDecision ioHomeScheduleFollowUp(IoHomeFollowUpState &s,
                                                    uint32_t now, uint32_t delay)
{
    if (s.deadline && int32_t(now - s.deadline) < 0)
        return IoHomeFollowUpDecision::Coalesced;
    s = {now + 600000U, now + delay, true, false, 0, 0, 0};
    // Zero is reserved for "no epoch/no scheduled poll".
    if (!s.deadline) s.deadline = 1;
    if (!s.nextPoll) s.nextPoll = 1;
    return IoHomeFollowUpDecision::Scheduled;
}
