#pragma once
#include <cstdint>
struct IoHomeVerificationEvidence {
    enum class Result:uint8_t {Idle,Queued,TxNoReply,ResponseUnverified,Authenticated,DutyDenied,CcaDenied,Collision,DriverError,QueueBusy};
    enum Flag:uint8_t {TxStarted=1,TxDone=2,RxReady=4,ChallengeSeen=8,AuthTx=16,ResponseSeen=32};
    uint64_t transaction=0;
    uint32_t node=0,startMs=0,frequency=0,chargedMs=0,usedMs=0,limitMs=0;
    uint8_t flags=0,mode=0;
    Result result=Result::Idle;
    uint32_t txIdLow=0;
    bool peerNoReply()const{return result==Result::TxNoReply&&(flags&(TxStarted|TxDone|RxReady))==(TxStarted|TxDone|RxReady);}
    const char *name()const {
        switch(result){case Result::Queued:return "queued";case Result::TxNoReply:return "tx_started_no_reply";
        case Result::ResponseUnverified:return "2f_seen_proof_incomplete";case Result::Authenticated:return "authenticated_success";
        case Result::DutyDenied:return "tx_denied_duty_budget";case Result::CcaDenied:return "tx_denied_cca";
        case Result::Collision:return "identity_collision";case Result::DriverError:return "driver_error";
        case Result::QueueBusy:return "queue_busy";default:return "idle";}
    }
};
static_assert(sizeof(IoHomeVerificationEvidence)<=40,"Bound verification diagnostic RAM");
