#pragma once
#include <cstdint>
#include <cstring>
struct IoHomeBatteryDiagnostic {
    enum class State:uint8_t {NotRequested,Queued,RfStarted,RfCompleted,ReplyReceived,Complete,Unsupported,Timeout,DutyBlocked,Failed,IdentityChanged,NotApplicable};
    static const char*name(State s) {
        static const char*names[]={"not_requested","queued_not_transmitted","rf_tx_started","rf_tx_completed","reply_received","complete","unsupported","timeout","duty_blocked","failed","identity_changed","not_in_manufacturer_sequence"};
        return names[static_cast<unsigned>(s)];
    }
    uint32_t node=0,revision=0,queuedMs=0,lastRfAttemptMs=0,resultMs=0,requiredMs=0,remainingMs=0,retryAfterMs=0,token=0;
    uint8_t key[16]{},action=0,objectSlot=0;
    uint32_t rfAcceptedCount=0;
    State result=State::NotRequested,objects[5]{};
    bool rfStarted=false,rfCompleted=false,replyReceived=false;
    bool matches(uint32_t n,const uint8_t*k,uint32_t rev)const {return node==n&&revision==rev&&k&&!std::memcmp(key,k,16);}
    const char *resultName()const {return result==State::DutyBlocked?(rfAcceptedCount?"exchange_duty_budget":"not_transmitted_duty_budget"):name(result);}
    void begin(uint32_t n,const uint8_t*k,uint32_t rev,uint8_t a,uint32_t now) {
        *this={};node=n;revision=rev;std::memcpy(key,k,16);action=a;queuedMs=now;result=State::Queued;
    }
    void observe(State s,uint32_t now) {
        result=s;resultMs=now;
        if(s==State::RfStarted){rfStarted=true;lastRfAttemptMs=now;++rfAcceptedCount;}
        if(s==State::RfCompleted)rfCompleted=true;
        if(s==State::ReplyReceived)replyReceived=true;
        if(!action)objects[objectSlot]=s;
    }
};
