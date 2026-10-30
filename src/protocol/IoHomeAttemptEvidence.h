#pragma once
#include "IoHomeCommands.h"
#include "../radio/IoHomeRadioTiming.h"
#include <cstring>

// Classification describes observations, never physical refusal or actuation.
inline const char *ioHomeAttemptOutcomeName(IoHomeTimingEvent event)
{
    switch(event) {
    case IoHomeTimingEvent::NoChallenge:return "no_3c_after_00";
    case IoHomeTimingEvent::PostAuthNoReply:return "3c_seen_3d_sent_no_04";
    case IoHomeTimingEvent::Response04:return "04_received";
    case IoHomeTimingEvent::CrcError:return "rx_crc_error";
    case IoHomeTimingEvent::Unmatched:return "rx_unmatched";
    case IoHomeTimingEvent::AdmissionDeferred:return "rf_admission_deferred";
    case IoHomeTimingEvent::DriverError:return "tx_or_rx_reentry_error";
    case IoHomeTimingEvent::NoResponse:return "response_timeout";
    case IoHomeTimingEvent::PeerRejected:return "peer_rejected";
    default:return "observation";
    }
}

inline IoHomeTimingEvent ioHomeAttemptEvent(IoHomeCommand request,const char *event,
        bool challenge,bool answer,IoHomeCommand reply,const char *failure="none")
{
    if (!std::strcmp(event,"tx_timeout")) return IoHomeTimingEvent::DriverError;
    if (!std::strcmp(event,"tx_rejected") || !std::strcmp(failure,"duty_budget_exceeded"))
        return IoHomeTimingEvent::AdmissionDeferred;
    if (!std::strcmp(event,"rx_wrong_peer") || !std::strcmp(event,"rx_unexpected") || !std::strcmp(event,"rx_invalid_status"))
        return IoHomeTimingEvent::Unmatched;
    if (!std::strcmp(event,"rx_correlated")) {
        if(reply==IoHomeCommand::PrivateResponse)return IoHomeTimingEvent::Response04;
        if(reply==IoHomeCommand::ErrorResponse)return IoHomeTimingEvent::PeerRejected;
    }
    if (!std::strcmp(event,"exchange_failed") &&
        (!std::strcmp(failure,"local_radio_failure") || !std::strcmp(failure,"media_access_failed")))
        return IoHomeTimingEvent::DriverError;
    if (!std::strcmp(event,"response_timeout") || !std::strcmp(event,"exchange_failed")) {
        if(request==IoHomeCommand::Execute && challenge && answer)return IoHomeTimingEvent::PostAuthNoReply;
        if(request==IoHomeCommand::Execute && !challenge)return IoHomeTimingEvent::NoChallenge;
        return IoHomeTimingEvent::NoResponse;
    }
    if (!std::strcmp(event,"exchange_completed") || !std::strcmp(event,"status_poll_completed"))
        return IoHomeTimingEvent::AttemptEnd;
    return IoHomeTimingEvent::PacketParsed; // ordinary observation, not terminal
}
