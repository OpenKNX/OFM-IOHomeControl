#pragma once
#include "../protocol/IoHomeFrame.h"

// Protocol classification only. Duty admission and radio readiness always apply.
// The caller supplies a single-use grant from a validated receive handler;
// START=0 by itself never gives a raw sender a carrier-sense exemption.
enum class IoHomeTransmitIntent { Malformed, OneWay, Start, ImmediateReply, Unqualified };
inline IoHomeTransmitIntent ioHomeTransmitIntent(const IoHomeFrame &frame, bool parsed,
                                                bool qualifiedReply) {
    if (!parsed) return IoHomeTransmitIntent::Malformed;
    if (frame.ctrlByte0 & IOHC_CTRL0_MODE_1W) return IoHomeTransmitIntent::OneWay;
    if (frame.ctrlByte0 & IOHC_CTRL0_START) return IoHomeTransmitIntent::Start;
    return qualifiedReply ? IoHomeTransmitIntent::ImmediateReply : IoHomeTransmitIntent::Unqualified;
}

// A local freshness bound, not an on-air timestamp or a regulatory exemption.
struct IoHomeReplyGrant {
    bool granted;
    uint32_t owner, peer, receivedMs;
    uint8_t frequency;
    uint64_t session;
};
inline bool ioHomeReplyQualified(const IoHomeFrame &frame, const IoHomeReplyGrant &grant,
                                 uint32_t now, uint32_t owner, uint8_t frequency,
                                 uint64_t session, bool exchangeValid) {
    return grant.granted && exchangeValid && grant.owner==owner &&
        grant.session==session && uint32_t(now-grant.receivedMs)<=25U &&
        grant.frequency==frequency && frame.getSrcNodeId()==owner &&
        frame.getDestNodeId()==grant.peer;
}
