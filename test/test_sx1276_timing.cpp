#include "radio/RadioSX1276.h"
#include "radio/sx1276Regs-Fsk.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

static uint32_t now;
static uint32_t clockUs(){return now;}
struct Bus {
    uint8_t regs[128]{};RadioSX1276 *radio=nullptr;bool injectOldRx=false;
    static uint8_t read(void *p,uint8_t a){return static_cast<Bus*>(p)->regs[a];}
    static void write(void *p,uint8_t a,uint8_t v){auto *b=static_cast<Bus*>(p);b->regs[a]=v;if(a==REG_FIFO && b->injectOldRx)b->radio->nativeDio0Edge(now);}
};
int main() {
    Bus bus;bus.regs[REG_VERSION]=0x12;
    RadioSX1276 radio;radio.setNativeTransport(&bus,Bus::read,Bus::write);
    radio.setNativeTimingClock(clockUs);radio.init(1,2,3);
    assert(!radio.timingRecording());
    IoHomeRadioTiming::Entry entry;
#if !IOHC_SX1276_TIMING
    assert(!radio.setTimingRecording(true));assert(!radio.popTiming(entry));
    assert(radio.timingBufferBytes()==0);
#else
    assert(radio.setTimingRecording(true));
    assert(!radio.preambleEvidenceAvailable() && !radio.syncEvidenceAvailable());
    // Regression for the tester's unowned scan flood (>60 seconds at 5ms).
    for(unsigned i=0;i<13000;++i) {
        now=i*5000U;radio.setFrequency(i%3==0?868250000:i%3==1?868950000:869850000);
        assert(radio.startReceive()==RadioError::None);
    }
    assert(!radio.popTiming(entry));assert(radio.timingDrops()==0);
    assert(radio.timingIgnoredScans()==52000);radio.setFrequency(868950000);
    assert(radio.startTransmit(nullptr,0)==RadioError::InvalidParam);
    assert(!radio.popTiming(entry));
    radio.setTimingContext(42,1);
    uint8_t payload[]={0x4B,0x20,0};
    bus.radio=&radio;bus.injectOldRx=true;
    now=10;assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);bus.injectOldRx=false;
    assert(!radio.setTimingRecording(false)); // ownership: no mode changes mid-TX
    // Repeated in-progress polls must leave room for the completion stages.
    bus.regs[REG_IRQFLAGS2]=0;
    for(unsigned i=0;i<100;++i) {now=20+i;assert(!radio.isTxDone());}
    now=200;radio.nativeDio0Edge(now);radio.nativeDio0Edge(201);
    radio.setTimingContext(999,9); // busy callers cannot steal an in-flight context
    now=15200;bus.regs[REG_IRQFLAGS2]=RF_IRQFLAGS2_PACKETSENT;
    assert(radio.isTxDone());assert(radio.state()==RadioState::Idle);
    now=15300;assert(radio.startReceive()==RadioError::None);
    uint32_t edge=0,observed=0,rx=0;unsigned edgeCount=0,pollCount=0;
    while(radio.popTiming(entry)) {
        assert(entry.transaction==42 && entry.attempt==1 && entry.txId==1);
        assert(entry.event!=uint8_t(IoHomeTimingEvent::PayloadIsr));
        if(entry.event==uint8_t(IoHomeTimingEvent::TxDoneIsr)){edge=entry.us;++edgeCount;}
        if(entry.event==uint8_t(IoHomeTimingEvent::FirstTxPoll))++pollCount;
        if(entry.event==uint8_t(IoHomeTimingEvent::CompletionReturn))observed=entry.us;
        if(entry.event==uint8_t(IoHomeTimingEvent::RxModeWritten))rx=entry.us;
    }
    assert(edgeCount==1 && pollCount==1 && radio.timingDrops()==0);
    assert(uint32_t(rx-edge)==15100 && uint32_t(rx-observed)==100);
    // The auth continuation has its own TX id within the same capture.
    uint8_t answer[]={0x0E,0,0,0,1,0,0,2,0x3D,0,0,0,0,0,0};
    radio.setTimingContext(42,1);now=15400;
    assert(radio.startTransmit(answer,sizeof(answer))==RadioError::None);
    now=15500;bus.regs[REG_IRQFLAGS2]=RF_IRQFLAGS2_PACKETSENT;assert(radio.isTxDone());
    assert(radio.startReceive()==RadioError::None);radio.timingEvent(IoHomeTimingEvent::PostAuthNoReply);
    assert(radio.timingCaptureState()==uint8_t(IoHomeRadioTiming::Capture::Frozen));
    for(unsigned i=0;i<13000;++i) {now+=5000;radio.startReceive();}
    assert(radio.timingDrops()==0);bool terminal=false;
    while(radio.popTiming(entry)) {assert(entry.txId==2 && entry.transaction==42);terminal|=entry.event==uint8_t(IoHomeTimingEvent::PostAuthNoReply);}
    assert(terminal);
    // A stale RX edge must never become the next TX's completion timestamp.
    radio.nativeDio0Edge(15400);
    assert(radio.setTimingRecording(true));
    radio.setTimingContext(43,2);now=15500;
    assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);
    now=15600;bus.regs[REG_IRQFLAGS2]=RF_IRQFLAGS2_PACKETSENT;assert(radio.isTxDone());
    bool fallback=false;
    while(radio.popTiming(entry)) if(entry.txId==3) {
        assert(entry.event!=uint8_t(IoHomeTimingEvent::TxDoneIsr));
        fallback|=entry.event==uint8_t(IoHomeTimingEvent::TxDonePolled);
    }
    assert(fallback);
    // Timestamp zero and wrap are valid; validity follows event presence.
    assert(radio.setTimingRecording(true));
    now=0xFFFFFFE0U;radio.setTimingContext(44,1);
    assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);
    radio.nativeDio0Edge(0xFFFFFFF0U);now=20;assert(radio.isTxDone());
    while(radio.popTiming(entry)) {
        if(entry.event==uint8_t(IoHomeTimingEvent::TxDoneIsr))edge=entry.us;
        if(entry.event==uint8_t(IoHomeTimingEvent::CompletionReturn))observed=entry.us;
    }
    assert(uint32_t(observed-edge)==36);
    for(unsigned i=0;i<70;++i)radio.timingEvent(IoHomeTimingEvent::PacketParsed);
    assert(radio.timingDrops()==6);
    assert(radio.setTimingRecording(false));
    unsigned count=0;while(radio.popTiming(entry))++count;assert(count==64);
    radio.timingEvent(IoHomeTimingEvent::PacketParsed);assert(!radio.popTiming(entry));
    // A software DIO flag without an ISR timestamp or PacketSent is not valid
    // polling evidence, even though the existing driver consumes that flag.
    assert(radio.setTimingRecording(true));radio.setTimingContext(45,1);now=1000;
    assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);
    bus.regs[REG_IRQFLAGS2]=0;radio.nativeDio0Edge(1100,false);now=1200;assert(radio.isTxDone());
    while(radio.popTiming(entry))assert(entry.event!=uint8_t(IoHomeTimingEvent::TxDoneIsr)&&entry.event!=uint8_t(IoHomeTimingEvent::TxDonePolled));
    assert(radio.setTimingRecording(false));
    // Host model of the ESP32 portMUX contract: concurrent ISR writer/task
    // reader see complete entries, and every lost in-scope write is counted.
    IoHomeRadioTiming shared;shared.reset();shared.enabled=true;
    shared.pendingTransaction=123;shared.pendingAttempt=1;shared.beginTx(1,868950000,0,0);
    shared.push(IoHomeTimingEvent::TxStart,0,868950000,2);IoHomeRadioTiming::Entry discarded;assert(shared.pop(discarded));
    std::mutex mux;std::atomic<bool> done{false};unsigned consumed=0;
    std::thread producer([&](){for(unsigned i=1;i<=10000;++i){
        std::lock_guard<std::mutex> lock(mux);
        shared.push(IoHomeTimingEvent::PayloadIsr,i,868950000,2);
    }done=true;});
    while(!done) {std::lock_guard<std::mutex> lock(mux);if(shared.pop(entry)){
        assert(entry.transaction==123&&entry.txId==1&&entry.attempt==1);++consumed;}}
    producer.join();while(shared.pop(entry))++consumed;
    assert(consumed+shared.drops==10000);
    IoHomeRadioTiming rollover;rollover.reset();rollover.enabled=true;
    rollover.pendingTransaction=1;rollover.pendingAttempt=1;rollover.beginTx(1,868950000,10,0);
    rollover.push(IoHomeTimingEvent::TxStart,10,868950000,2);
    rollover.pendingTransaction=2;rollover.pendingAttempt=2;rollover.beginTx(2,868950000,20,0);
    assert(rollover.capture==IoHomeRadioTiming::Capture::Frozen && rollover.txId==1 && rollover.transaction==1);
    bool boundary=false;while(rollover.pop(entry))boundary|=entry.event==uint8_t(IoHomeTimingEvent::AttemptEnd);
    assert(boundary);
    rollover.reset();rollover.enabled=true;rollover.beginTx(3,868950000,0,3);
    rollover.push(IoHomeTimingEvent::PacketRead,10000000,868950000,2);
    assert(rollover.capture==IoHomeRadioTiming::Capture::Frozen);
    assert(rollover.pop(entry) && entry.event==uint8_t(IoHomeTimingEvent::CaptureExpired));
#endif
    puts("SX1276 timing: ownership, first edge, delayed service, fallback, wrap, overflow and disabled mode passed");
}
