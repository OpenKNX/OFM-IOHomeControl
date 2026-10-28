#include "radio/RadioSX1276.h"
#include "radio/sx1276Regs-Fsk.h"
#include <cassert>
#include <cstdio>
#include <vector>

static uint32_t now;
static uint32_t clockUs(){return now;}
struct Bus {
    uint8_t regs[128]{};
    static uint8_t read(void *p,uint8_t a){return static_cast<Bus*>(p)->regs[a];}
    static void write(void *p,uint8_t a,uint8_t v){static_cast<Bus*>(p)->regs[a]=v;}
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
    radio.setTimingContext(42,1);
    uint8_t payload[]={0x4B,0x20,0};
    now=10;assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);
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
        if(entry.event==uint8_t(IoHomeTimingEvent::TxDoneIsr)){edge=entry.us;++edgeCount;}
        if(entry.event==uint8_t(IoHomeTimingEvent::FirstTxPoll))++pollCount;
        if(entry.event==uint8_t(IoHomeTimingEvent::CompletionReturn))observed=entry.us;
        if(entry.event==uint8_t(IoHomeTimingEvent::RxModeWritten))rx=entry.us;
    }
    assert(edgeCount==1 && pollCount==1 && radio.timingDrops()==0);
    assert(uint32_t(rx-edge)==15100 && uint32_t(rx-observed)==100);
    // A stale RX edge must never become the next TX's completion timestamp.
    radio.nativeDio0Edge(15400);
    radio.setTimingContext(43,2);now=15500;
    assert(radio.startTransmit(payload,sizeof(payload))==RadioError::None);
    now=15600;bus.regs[REG_IRQFLAGS2]=RF_IRQFLAGS2_PACKETSENT;assert(radio.isTxDone());
    bool fallback=false;
    while(radio.popTiming(entry)) if(entry.txId==2) {
        assert(entry.event!=uint8_t(IoHomeTimingEvent::TxDoneIsr));
        fallback|=entry.event==uint8_t(IoHomeTimingEvent::TxDonePolled);
    }
    assert(fallback);
    // Timestamp zero and wrap are valid; validity follows event presence.
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
#endif
    puts("SX1276 timing: ownership, first edge, delayed service, fallback, wrap, overflow and disabled mode passed");
}
