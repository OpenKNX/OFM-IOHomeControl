"""Compile the actual channel publication method against a recording KNX adapter."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/IoHomecontrolChannel.cpp").read_text()
method = source.split("void IoHomecontrolChannel::updateBatteryKo() {", 1)[1].split("\nvoid IoHomecontrolChannel::onBatteryStatus", 1)[0]
harness = r'''#include "protocol/IoHomeBattery.h"
#include <cassert>
struct Dpt {Dpt(int,int){}};
enum ComFlag {WriteRequest,Ok,Uninitialized};
struct RecordingKo {
    bool cached=false,known=false;unsigned writes=0;ComFlag flag=Uninitialized;
    void commFlag(ComFlag f){flag=f;} // Standard KNX preserves its cached validity.
    bool value(bool low,Dpt){cached=low;known=true;++writes;flag=WriteRequest;return true;}
    bool valueCompare(bool low,Dpt d){if(known&&cached==low)return false;return value(low,d);}
};
struct {RecordingKo ko;RecordingKo &getGroupObject(int){return ko;}} knx;
#define BAT_KoCalcNumber(x) (x)
#define BAT_KocLow 1
struct IoHomecontrolChannel {
    IoHomeBatteryInfo mBatteryInfo;bool mBatteryAlarmPublished=false;bool enabled=true;
    bool batteryMonitoring()const{return enabled;}
    void updateBatteryKo();
};
void IoHomecontrolChannel::updateBatteryKo() {
''' + method + r'''
int main(){
    IoHomecontrolChannel ch;
    ch.updateBatteryKo();assert(!knx.ko.known&&knx.ko.writes==0);
    ch.mBatteryInfo.observeSomfy(3,0x4B,100);ch.updateBatteryKo();assert(!knx.ko.cached);
    ch.mBatteryInfo.observeStatus(0x20,4,200);ch.updateBatteryKo();assert(knx.ko.cached);
    ch.mBatteryInfo.observeSomfy(1,0x4B,300);ch.updateBatteryKo();assert(knx.ko.cached);
    ch.mBatteryInfo.observeStatus(0x60,4,400);ch.updateBatteryKo();assert(!knx.ko.cached);
    ch.mBatteryInfo.observeError(0x12,0xFE,500);ch.updateBatteryKo();assert(knx.ko.cached);
    ch.mBatteryInfo.observeStatus(0x40,4,600);ch.updateBatteryKo();assert(!knx.ko.cached);
    ch.mBatteryInfo.observeError(0x12,0xFE,600);ch.updateBatteryKo();assert(!knx.ko.cached);
    ch.mBatteryInfo.observeSomfy(1,0x4B,600);ch.updateBatteryKo();assert(knx.ko.cached);
    assert(knx.ko.writes==6);
    // Unknown evidence does not clear the last confirmed state.
    ch.mBatteryInfo.observeStatus(0,4,700);ch.updateBatteryKo();assert(knx.ko.cached&&knx.ko.writes==6);
    // Context reset preserves the cache but forces the next confirmation to send.
    ch.mBatteryInfo={};ch.updateBatteryKo();assert(knx.ko.known&&knx.ko.cached);
    ch.mBatteryInfo.observeStatus(0x20,4,800);ch.updateBatteryKo();assert(knx.ko.writes==7);
    ch.mBatteryInfo={};ch.mBatteryAlarmPublished=false;
    ch.mBatteryInfo.observeSomfy(3,0x4B,0xFFFFFFF0);ch.updateBatteryKo();assert(!knx.ko.cached);
    ch.mBatteryInfo.observeStatus(0x20,4,0x10);ch.updateBatteryKo();assert(knx.ko.cached);
    ch.enabled=false;ch.updateBatteryKo();assert(knx.ko.cached&&knx.ko.flag==Uninitialized);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "publication.cpp"
    binary = Path(directory) / "publication"
    cpp.write_text(harness)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I" + str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Battery KO arbitration, cache preservation and reset publication passed")
