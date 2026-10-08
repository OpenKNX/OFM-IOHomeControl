#!/usr/bin/env python3
"""Compile actual production gates/wrappers/assignment methods in both modes."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/IoHomecontrol.cpp').read_text()
channel=(root/'src/IoHomecontrolChannel.cpp').read_text()

def function(text, signature):
    start=text.index(signature)
    opening=text.index('{',start)
    depth=1
    pos=opening+1
    while depth:
        depth += (text[pos]=='{')-(text[pos]=='}')
        pos+=1
    return text[start:pos]

setup=function(source,'void IoHomecontrol::setup(bool configured)')
loop=function(source,'void IoHomecontrol::loop(bool configured)')
count=function(source,'uint8_t IoHomecontrol::configuredChannelCount() const')
help_function=function(source,'void IoHomecontrol::showHelp()')
assignment=function(source,'uint8_t IoHomecontrol::assignKeyImportDevice(')
parsers=''.join(function(source, signature) for signature in ['bool isSpace(', 'std::string trimSpaces(', 'bool parseUnsignedDecimal(', 'bool parseChannelIndex('])
callbacks=''.join(function(channel, 'void IoHomecontrolChannel::'+name+'(') for name in ['onRssiUpdate','setErrorStatus','onSlatFeedback'])
channel_setup=channel.split('void IoHomecontrolChannel::setup()\n{',1)[1].split('    const uint8_t lProtocolMode',1)[0]
loop_gate=source.split('void IoHomecontrol::loop()\n{',1)[1].split('    // Radio diagnostics',1)[0]
assign_parser=source.split('    if(lSub.rfind("keyimport assign",0)==0)',1)[1].split('\n#endif',1)[0]
assign_parser='    if(lSub.rfind("keyimport assign",0)==0)'+assign_parser
stop_parser='    if(lSub.rfind("stop ",0)==0)'+source.split('    if(lSub.rfind("stop ",0)==0)',1)[1].split('    if(lSub.rfind("keyimport assign",0)==0)',1)[0]
candidate_listing=source.split('            // Use the SAME indexed final result list as assignKeyImportDevice.',1)[1].split('\n#endif',1)[0]

harness=r"""
#include "protocol/IoHomeStandaloneDiag.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdarg>
unsigned now=0;
unsigned millis(){return now;}
std::string logs;
void recordLog(const char *fmt,...){char b[1024];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);logs+=b;logs+='\n';}
#define logInfoP recordLog
#define logDebugP recordLog
struct Dpt{Dpt(int,int){}};
constexpr int DPT_Scaling=0,DPT_DecimalFactor=0,IOHC_KoCHRssi=0,IOHC_KoCHErrorStatus=1,IOHC_KoCHSlatFeedback=2;
constexpr unsigned IOHC_ChannelCount=16;
struct {bool ready=false;bool configured()const{return ready;}} knx;
struct {bool startup=false;bool afterStartupDelay()const{return startup;}
 struct {unsigned calls=0;void save(){++calls;}} flash;
 struct {void printHelpLine(const char *a,const char *b){logs+=a;logs+=b;}} console;} openknx;
enum class IoHomeNodeClass{Unknown,Actuator,Sensor};
enum class IoHomeCommand{Execute};
struct FakeIdentity {IoHomeNodeClass nodeClass=IoHomeNodeClass::Actuator;unsigned profile=4,subProfile=1,manufacturerId=2,powerSaveMode=1;bool valid=true,fullMetadata=true;};
struct RecordingKo {unsigned writes=0;template<class T,class D>void value(T,D){assert(knx.ready);++writes;}};
struct IoHomecontrolChannel {
 RecordingKo ko;RecordingKo &getKo(unsigned){return ko;}
 unsigned mLastRssi=0,mErrorStatus=0;float mCurrentSlat=0;
 void onRssiUpdate(uint8_t);void setErrorStatus(uint8_t);void onSlatFeedback(float);
 unsigned node=0;bool oneWay=false;uint8_t key[16]{};unsigned setupReads=0;
 unsigned _channelIndex=0;unsigned power=99,confirm=99,delay=99,acei=0,manual=99;
 enum class TwoWayPowerClass{Automatic};enum class PairingDiscoverConfirmMode{Send};
 struct TwoWayDiscoverySettings{};
 FakeIdentity identity;
 bool isPaired()const{return node!=0;}bool allowsActuatorControls()const{return true;}
 const FakeIdentity &getProtocolIdentity()const{return identity;}
 bool is1W()const{return oneWay;}bool isOperational()const{return node!=0;}
 unsigned getNodeId()const{return node;}const uint8_t *getEncryptionKey()const{return key;}
 void setIs1W(bool v){oneWay=v;}void setManualProfileOverride(unsigned v){manual=v;}
 void setConfigured2WPowerClass(TwoWayPowerClass){power=0;}
 void setConfigured2WDiscoverConfirmMode(PairingDiscoverConfirmMode){confirm=1;}
 void setConfigured2WKeyInitDelay(unsigned v){delay=v;}void setConfigured2WAcei(unsigned v){acei=v;}
 void setConfigured2WDiscoverySettings(TwoWayDiscoverySettings){}
 void setup();
};
constexpr unsigned IOHC_ACEI_DEFAULT=0x43;
"""+parsers+r"""
struct IoHomecontrol {
 enum class KeyImportPhase{Idle,Complete};
 struct Device {bool valid=true;unsigned nodeId=0x123456;FakeIdentity protocolIdentity;
 bool passiveAuthVerified=true,directedVerified=true,speResponseSeen=true;};
 KeyImportPhase mKeyImportPhase=KeyImportPhase::Complete;
 using KeyImportDevice=Device;
 Device mKeyImportDevices[2];unsigned mKeyImportDeviceCount=1,mNumChannels=1;
 struct {uint8_t key[16]{7};}mKeyImportKey;
 IoHomecontrolChannel first,second;IoHomecontrolChannel *mChannels[2]{&first,&second};
 unsigned setupCalls=0,loops=0,restores=0,persists=0,snapshots=0;
 bool mIdentityRestoreInitDone=false,mNetworkStoreReady=true,mNetworkStoreFailed=false,allowed=true;
 unsigned mStandaloneStartupAtMs=0;bool mStandaloneStartupReady=false;
 struct {bool idle=true;unsigned stops=0;bool idleForManagedOperation()const{return idle;}
 bool sendCommand(unsigned,const uint8_t *,IoHomeCommand,unsigned param){assert(param==0xD2);++stops;return true;}}mController;
 bool managementRequestsAllowed()const{return allowed;}
 void setup(){++setupCalls;}
 void processAfterStartupDelay(){++restores;mIdentityRestoreInitDone=true;}
 void serviceMetadataSnapshots(){++snapshots;}
 bool applyKeyImportDeviceToChannel(const Device &d,uint8_t index){
  if(mNetworkStoreFailed)return false;
  ++persists;mChannels[index]->node=d.nodeId;std::memcpy(mChannels[index]->key,mKeyImportKey.key,16);return true;
 }
 uint8_t assignKeyImportDevice(uint8_t,uint8_t,uint8_t &);
 uint8_t configuredChannelCount()const;
 void showHelp();
 void loop();
#if defined(IOHC_STANDALONE_DIAG) && IOHC_STANDALONE_DIAG
 void setup(bool);void loop(bool);
#else
 // Exact framework default behavior when standalone overrides are absent.
 void setup(bool configured){if(configured)setup();}
 void loop(bool configured){if(configured)loop();}
#endif
 bool parse(const std::string &lSub){
#if defined(IOHC_STANDALONE_DIAG) && IOHC_STANDALONE_DIAG
"""+stop_parser+assign_parser+r"""
#endif
 return false;
 }
 void candidates(){
#if defined(IOHC_STANDALONE_DIAG) && IOHC_STANDALONE_DIAG
"""+candidate_listing+r"""
#endif
 }
};
void IoHomecontrolChannel::setup(){
"""+channel_setup+r"""
 ++setupReads;
}
void IoHomecontrol::loop(){
"""+loop_gate+r"""
 ++loops;
}
#if defined(IOHC_STANDALONE_DIAG) && IOHC_STANDALONE_DIAG
"""+setup+loop+r"""
#endif
"""+count+assignment+help_function+callbacks+r"""
int main(){
 IoHomecontrol m;
 m.setup(false);m.loop(false);
 if(kIoHomeStandaloneDiag){
  assert(m.setupCalls==1&&m.loops==0&&m.restores==0&&m.configuredChannelCount()==1);
  now=999;m.loop(false);assert(m.restores==0);
  now=1000;m.loop(false);assert(m.restores==1&&m.loops==1);
  m.loop(false);assert(m.restores==1&&m.loops==2);
 }else{assert(m.setupCalls==0&&m.loops==0&&m.restores==0&&m.configuredChannelCount()==16);}
 IoHomecontrolChannel channel;channel.setup();
 assert(channel.setupReads==0);
 if(kIoHomeStandaloneDiag)assert(!channel.oneWay&&channel.manual==0&&channel.power==0&&channel.confirm==1&&channel.delay==300);
 channel.onRssiUpdate(45);channel.setErrorStatus(1);channel.onSlatFeedback(25);
 assert(channel.ko.writes==0&&channel.mLastRssi==45&&channel.mErrorStatus==1&&channel.mCurrentSlat==25);
 knx.ready=true;channel.onRssiUpdate(55);channel.setErrorStatus(2);channel.onSlatFeedback(50);assert(channel.ko.writes==3);knx.ready=false;
 IoHomecontrolChannel other;other._channelIndex=1;other.setup();assert(other.setupReads==0&&other.manual==99);
 knx.ready=true;other.setup();assert(other.setupReads==1);
 assert(m.configuredChannelCount()==16);knx.ready=false;
 m.mIdentityRestoreInitDone=false;m.loop();assert(m.loops==(kIoHomeStandaloneDiag?2U:0U));
 logs.clear();m.showHelp();
 assert((logs.find("keyimport assign RESULT_INDEX")!=std::string::npos)==kIoHomeStandaloneDiag);
 assert((logs.find("iohc01 stop")!=std::string::npos)==kIoHomeStandaloneDiag);
 logs.clear();m.candidates();
 if(kIoHomeStandaloneDiag){
  assert(logs.find("index=0")!=std::string::npos&&logs.find("assigned-channel=0")!=std::string::npos);
  assert(logs.find("key=")==std::string::npos&&logs.find("07000000")==std::string::npos);
  assert(m.parse("keyimport assign 99 1")&&m.persists==0);
  for(const auto text:{"keyimport assign -1 1","keyimport assign 4294967296 1","keyimport assign 0 1 extra","keyimport assign 0 4294967297"})assert(m.parse(text)&&m.persists==0);
  assert(m.parse("keyimport assign 0 2")&&m.persists==0);
  m.allowed=false;m.parse("keyimport assign 0 1");assert(m.persists==0);m.allowed=true;
  m.mController.idle=false;m.parse("keyimport assign 0 1");assert(m.persists==0);m.mController.idle=true;
  m.mNetworkStoreReady=false;m.parse("keyimport assign 0 1");assert(m.persists==0);m.mNetworkStoreReady=true;
  m.mKeyImportPhase=IoHomecontrol::KeyImportPhase::Idle;m.parse("keyimport assign 0 1");assert(m.persists==0);
  m.mKeyImportPhase=IoHomecontrol::KeyImportPhase::Complete;
  m.first.node=0xABCDEF;m.parse("keyimport assign 0 1");assert(m.persists==0&&m.first.node==0xABCDEF);m.first.node=0;
  m.parse("stop 01");assert(m.mController.stops==0);
  m.parse("keyimport assign 0 1");assert(m.persists==1&&m.snapshots==1&&m.first.node==0x123456&&m.first.key[0]==7);
  m.parse("stop 01");assert(m.mController.stops==1);
  m.mController.idle=false;m.parse("stop 01");assert(m.mController.stops==1);m.mController.idle=true;
  m.parse("stop 02");assert(m.mController.stops==1);
  m.parse("keyimport assign 0 1");assert(m.persists==1); // duplicate is idempotent
  m.first.key[0]=8;m.parse("keyimport assign 0 1");assert(m.persists==1&&m.first.key[0]==8);
 }else{assert(!m.parse("keyimport assign 0 1")&&!m.parse("stop 01")&&m.persists==0&&logs.empty());}
}
"""

with tempfile.TemporaryDirectory() as temp:
    cpp=Path(temp)/'standalone.cpp';cpp.write_text(harness)
    for mode in (None,0,1):
        binary=Path(temp)/f'mode-{mode}'
        flags=[] if mode is None else [f'-DIOHC_STANDALONE_DIAG={mode}']
        subprocess.run(['clang++','-std=c++17',*flags,'-I'+str(root/'src'),str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

assert 'if(!knx.configured())return' in function(channel,'void IoHomecontrolChannel::updateBatteryKo()')
assert 'if(!knx.configured())return' in function(channel,'void IoHomecontrolChannel::loop()')
assert 'if(knx.configured())KoIOHC_ModuleStatus.value' in source
print('Standalone production wrappers, gates, channel defaults and import assignment passed in undefined/0/1 builds')
