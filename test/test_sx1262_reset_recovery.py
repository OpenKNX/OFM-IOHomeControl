"""Fault-inject the production standby/reset dispatch with SX1262 command mocks."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "src/radio/RadioSX1262.cpp").read_text()
start = source.index("void RadioSX1262::standby()")
method = source[start:source.index("RadioState RadioSX1262::state()", start)]
harness = r'''
#include <cassert>
#include <cstdint>
enum class RadioState { Idle, Receiving, Transmitting };
enum class RadioError { None, HardwareError };
constexpr uint8_t SX1262_STDBY_RC=0, SX1262_STDBY_XOSC=1;
constexpr uint8_t SX1262_CMD_GET_STATUS=0xC0, SX1262_CMD_SET_STANDBY=0x80;
struct RadioSX1262 {
    enum class TxCompletionPhase { WaitingForIrq, Cleanup };
    TxCompletionPhase mTxCompletionPhase=TxCompletionPhase::Cleanup;
    RadioState mState=RadioState::Transmitting;
    bool mInitialized=true, mEms2Mode=false, readable=true, writable=true, failConfigure=false;
    uint8_t status=0x20, mStandbyMode=SX1262_STDBY_XOSC, mOutputPower=17;
    uint32_t mCurrentFreq=869850000;
    unsigned configurations=0, reads=0, writes=0, tuning=0, powerWrites=0, rxSwitches=0;
    bool readCommand(uint8_t op,uint8_t *data,int n) {
        assert(op==SX1262_CMD_GET_STATUS && n==1); ++reads; *data=status; return readable;
    }
    bool sendCommand(uint8_t op,uint8_t *data,int n) {
        assert(op==SX1262_CMD_SET_STANDBY && n==1 && *data==mStandbyMode);
        ++writes; return writable;
    }
    void setRfSwitchRx() { ++rxSwitches; }
    RadioError configure() {
        ++configurations;
        assert(mState==RadioState::Idle && mStandbyMode==SX1262_STDBY_RC);
        standby(); // the production configurator also calls standby
        mCurrentFreq=868950000; mOutputPower=14; mStandbyMode=SX1262_STDBY_XOSC;
        return failConfigure ? RadioError::HardwareError : RadioError::None;
    }
    RadioError setFrequencyInternal(uint32_t hz,bool blocking) {
        assert(blocking); ++tuning; mCurrentFreq=hz; return RadioError::None;
    }
    RadioError setOutputPowerInternal(uint8_t power,bool blocking) {
        assert(blocking); ++powerWrites; mOutputPower=power; return RadioError::None;
    }
    void standby();
};
'''
tests = r'''
int main() {
    RadioSX1262 reset;
    reset.standby();
    assert(reset.mInitialized && reset.mState==RadioState::Idle);
    assert(reset.configurations==1 && reset.reads==1 && reset.writes==1);
    assert(reset.mCurrentFreq==869850000 && reset.mOutputPower==17);
    assert(reset.tuning==1 && reset.powerWrites==1 && reset.rxSwitches==2);
    assert(reset.mTxCompletionPhase==RadioSX1262::TxCompletionPhase::WaitingForIrq);
    reset.standby();
    assert(reset.configurations==1 && reset.reads==1);

    RadioSX1262 normal;
    normal.status=0x30; normal.standby();
    assert(normal.configurations==0 && normal.reads==1 && normal.writes==1);
    assert(normal.mInitialized && normal.mState==RadioState::Idle);

    RadioSX1262 busy;
    busy.readable=false; busy.writable=false; busy.standby();
    assert(busy.configurations==0 && busy.mState==RadioState::Transmitting);

    RadioSX1262 failed;
    failed.failConfigure=true; failed.standby(); failed.standby();
    assert(!failed.mInitialized && failed.configurations==1);
    assert(failed.tuning==0 && failed.powerWrites==0);

    RadioSX1262 ems2;
    ems2.mEms2Mode=true; ems2.standby();
    assert(ems2.reads==0 && ems2.configurations==0);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "recovery.cpp").write_text(harness + method + tests)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra",
                   str(path / "recovery.cpp"), "-o", str(path / "recovery")], check=True)
    subprocess.run([str(path / "recovery")], check=True)
print("SX1262 reset recovery dispatch and failure paths passed")
