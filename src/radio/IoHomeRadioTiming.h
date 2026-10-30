#pragma once
#include <stdint.h>
#ifdef ESP32
#include <esp_attr.h>
#define IOHC_TIMING_IRAM IRAM_ATTR
#else
#define IOHC_TIMING_IRAM
#endif

#ifndef IOHC_SX1276_TIMING
#define IOHC_SX1276_TIMING 0
#endif

enum class IoHomeTimingEvent : uint8_t {
    TxStart, FirstTxPoll, TxDoneIsr, TxDonePolled, CompletionPollEntry, IrqRead,
    StandbyBegin, StandbyEnd, CompletionReturn, ControllerCheck, ControllerDone,
    ControllerRxBegin, RxSetupBegin, RxRegistersDone, RxModeWritten, RxReturn,
    ControllerRxReturn, PreambleIsr, SyncIsr, PayloadIsr, PacketRead, PacketParsed,
    NoChallenge, PostAuthNoReply, Response04, CrcError, Unmatched, AdmissionDeferred,
    DriverError, AttemptEnd, CaptureExpired, PeerRejected, NoResponse, CaptureStopped
};

// Caller serializes access (ESP32 uses a portMUX in both task and ISR context).
// No payloads, keys, allocation, SPI or formatting are retained here.
struct IoHomeRadioTiming {
    struct Entry {
        uint64_t transaction=0;
        uint32_t us=0, txId=0, txHz=0, rxHz=0;
        uint8_t attempt=0, event=0, state=0;
        uint16_t cachedIrq=0;
    };
    static constexpr uint8_t capacity=64;
    Entry entries[capacity]{};
    enum class Capture : uint8_t {Off, Armed, Active, Frozen};
    volatile Capture capture=Capture::Off;
    uint64_t pendingTransaction=0, transaction=0;
    uint8_t pendingAttempt=0;
    uint32_t startedUs=0, ignoredScans=0;
    uint32_t txId=0, txHz=0, drops=0;
    uint8_t attempt=0, head=0, count=0;
    volatile bool enabled=false;
    bool firstPoll=false, firstCheck=false, firstEdge=false, startingTx=false;

    void reset() {head=count=0; drops=0; firstPoll=firstCheck=firstEdge=false;transaction=pendingTransaction=0;txId=txHz=0;attempt=pendingAttempt=0;ignoredScans=0;capture=Capture::Armed;}
    void beginTx(uint32_t id,uint32_t hz,uint32_t us=0,uint8_t command=0) {
        if (!enabled || capture==Capture::Frozen || capture==Capture::Off) return;
        if (capture==Capture::Active &&
            !(command==0x3D && pendingTransaction && pendingTransaction==transaction && pendingAttempt==attempt)) {
            push(IoHomeTimingEvent::AttemptEnd,us,hz,0);
            return; // one attempt, including its auth continuation, per arm
        }
        if (capture==Capture::Armed) startedUs=us;
        capture=Capture::Active;transaction=pendingTransaction;attempt=pendingAttempt;
        txId=id;txHz=hz;firstPoll=firstCheck=firstEdge=false;startingTx=true;
    }
    // Inline into the driver's IRAM writer; a separate COMDAT IRAM member can
    // leave its Xtensa literal pool after the l32r use site at link time.
    __attribute__((always_inline)) inline void push(IoHomeTimingEvent event,uint32_t us,uint32_t rxHz,uint8_t state,uint16_t irq=0) {
        if (!enabled || capture!=Capture::Active) return;
        if (uint32_t(us-startedUs)>=10000000U) event=IoHomeTimingEvent::CaptureExpired;
        const bool rxEvent=event==IoHomeTimingEvent::RxSetupBegin || event==IoHomeTimingEvent::RxRegistersDone ||
            event==IoHomeTimingEvent::RxModeWritten || event==IoHomeTimingEvent::RxReturn ||
            event==IoHomeTimingEvent::PreambleIsr || event==IoHomeTimingEvent::SyncIsr ||
            event==IoHomeTimingEvent::PayloadIsr || event==IoHomeTimingEvent::PacketRead || event==IoHomeTimingEvent::PacketParsed;
        if (rxEvent && (rxHz!=txHz || startingTx)) return;
        if(event==IoHomeTimingEvent::TxStart)startingTx=false;
        const bool terminal=event==IoHomeTimingEvent::NoChallenge || event==IoHomeTimingEvent::PostAuthNoReply ||
            event==IoHomeTimingEvent::Response04 || event==IoHomeTimingEvent::DriverError ||
            event==IoHomeTimingEvent::AttemptEnd || event==IoHomeTimingEvent::CaptureExpired || event==IoHomeTimingEvent::PeerRejected ||
            event==IoHomeTimingEvent::NoResponse || event==IoHomeTimingEvent::CaptureStopped || event==IoHomeTimingEvent::AdmissionDeferred;
        if (event==IoHomeTimingEvent::FirstTxPoll) {if(firstPoll)return;firstPoll=true;}
        if (event==IoHomeTimingEvent::ControllerCheck) {if(firstCheck)return;firstCheck=true;}
        if (event==IoHomeTimingEvent::TxDonePolled && firstEdge) return;
        if (event==IoHomeTimingEvent::TxDoneIsr) {if(firstEdge)return;firstEdge=true;}
        if (count==capacity) {if(drops!=UINT32_MAX)++drops;if(terminal)capture=Capture::Frozen;return;}
        auto &entry=entries[(head+count)%capacity];
        entry.transaction=transaction;entry.us=us;entry.txId=txId;
        entry.txHz=txHz;entry.rxHz=rxHz;entry.attempt=attempt;
        entry.event=uint8_t(event);entry.state=state;entry.cachedIrq=irq;++count;
        if(terminal)capture=Capture::Frozen;
    }
    bool pop(Entry &entry) {
        if (!count) return false;
        entry=entries[head];head=(head+1)%capacity;--count;return true;
    }
};

#undef IOHC_TIMING_IRAM
