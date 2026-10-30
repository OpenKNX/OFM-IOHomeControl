"""Compile the actual BUSY method with deterministic ESP32-width clock/pin mocks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/radio/RadioSX1262.cpp").read_text()
start = source.index("bool RadioSX1262::waitBusy(")
end = source.index("{", start) + 1
depth = 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
method = source[start:end]

harness = r'''
#define ESP32
#include <cassert>
#include <cstdint>
#include <functional>
uint32_t now;
unsigned reads, yields;
std::function<int()> pin;
uint32_t millis(){return now;}
uint32_t micros(){return now*1000U;}
int digitalRead(int){++reads;return pin();}
void yield(){++yields;++now;}
constexpr int HIGH=1;
constexpr uint32_t SX1262_BUSY_TIMEOUT_MS=100;
struct RadioSX1262 {
    int mBusyPin=0;
    uint32_t mTxBusyHighStartUs=0,mTxBusyHighTotalUs=0,mTxBusyHighMaxUs=0,mTxBusyHighHitCount=0;
    bool mTxBusyTraceActive=false,mBusyTimedOut=false;
    void accumulateTxBusyTrace(uint32_t){}
    bool waitBusy(bool);
};
void reset(uint32_t time=1000){now=time;reads=yields=0;}
'''
tests = r'''
int main(){
    // Immediate ready and nonblocking BUSY keep their original semantics.
    for(bool blocking:{false,true}){
        reset();RadioSX1262 radio;pin=[] {return 0;};
        assert(radio.waitBusy(blocking)&&reads==1&&!radio.mBusyTimedOut);
    }
    reset();RadioSX1262 nonblocking;pin=[] {return HIGH;};
    assert(!nonblocking.waitBusy(false)&&reads==1&&yields==0&&!nonblocking.mBusyTimedOut);
    // Normal readiness on the third read: no timeout re-sample needed.
    reset();RadioSX1262 normal;pin=[] {return reads<3?HIGH:0;};
    assert(normal.waitBusy(true)&&reads==3&&yields==1&&!normal.mBusyTimedOut);
    // HIGH was sampled before preemption; the deciding read must see LOW.
    // Run both normal time and uint32_t millis rollover.
    for(uint32_t time:{1000U,0xFFFFFFF0U}){
        reset(time);RadioSX1262 delayed;
        pin=[] {if(reads==2){now+=500U;return HIGH;}return reads<3?HIGH:0;};
        assert(delayed.waitBusy(true)&&reads==3&&yields==0&&!delayed.mBusyTimedOut);
        assert(uint32_t(now-time)==500U);
        // Ready cannot silently clear a preexisting latched fault.
        delayed.mBusyTimedOut=true;assert(delayed.waitBusy(true)&&delayed.mBusyTimedOut);
    }
    // Permanent BUSY must still fail in bounded time, including rollover.
    for(uint32_t time:{1000U,0xFFFFFFF0U}){
        reset(time);RadioSX1262 stuck;pin=[] {return HIGH;};
        assert(!stuck.waitBusy(true)&&stuck.mBusyTimedOut);
        assert(uint32_t(now-time)==101U&&yields==101&&reads==104);
    }
    // Several slow samples model repeated access/contention; no renewed deadline.
    reset();RadioSX1262 slow;
    pin=[] {if(reads>1&&reads<4){now+=50;return HIGH;}return reads<4?HIGH:0;};
    assert(slow.waitBusy(true)&&reads==4&&yields==1&&!slow.mBusyTimedOut);
    assert(now==1101);
    // A late deciding HIGH remains a real fault, rather than an unbounded retry.
    reset();RadioSX1262 lateBusy;
    pin=[] {if(reads==2)now+=500;return HIGH;};
    assert(!lateBusy.waitBusy(true)&&reads==3&&yields==0&&lateBusy.mBusyTimedOut);
    // Existing timing instrumentation is still finalized on immediate ready.
    reset();RadioSX1262 traced;traced.mTxBusyHighStartUs=micros()-10;
    pin=[] {return 0;};assert(traced.waitBusy(true));
    assert(traced.mTxBusyHighStartUs==0&&traced.mTxBusyHighTotalUs==10&&traced.mTxBusyHighMaxUs==10);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "wait.cpp").write_text(harness + method + tests)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                    str(path / "wait.cpp"), "-o", str(path / "wait")], check=True)
    subprocess.run([str(path / "wait")], check=True)
print("SX1262 production BUSY wait: fresh deadline sample, bounded failures, nonblocking, rollover and trace passed")
