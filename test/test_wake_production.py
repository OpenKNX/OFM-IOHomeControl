"""Exercise production channel evidence and SX1262 setters with recording adapters."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
exchange_limit=next(line for line in (root/'src/controller/IoHomeController.h').read_text().splitlines() if line.startswith('#define IOHC_EXCHANGE_MAX_ATTEMPTS '))
def method(source,signature):
    start=source.index(signature);i=source.index('{',start);depth=1;end=i+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
channel=(root/'src/IoHomecontrolChannel.cpp').read_text()
methods='\n'.join(method(channel,signature) for signature in [
    'void IoHomecontrolChannel::observe2WMovingStatus(',
    'void IoHomecontrolChannel::onCommandExchangeResult(',
    'void IoHomecontrolChannel::onUnanswered2WWake(',
    'TwoWayWakeBelief IoHomecontrolChannel::twoWayWakeBeliefAt(',
    'bool IoHomecontrolChannel::twoWayLastHeardAgeAt(',
    'void IoHomecontrolChannel::onStatusPollFailed(',
    'bool IoHomecontrolChannel::requestStatus(bool'])
harness=r'''#include <cassert>
#include <cstdint>
#include "protocol/IoHomeCommands.h"
unsigned now=1000;unsigned millis(){return now;}
#define logInfoP(...) ((void)0)
#define logDebugP(...) ((void)0)
constexpr uint32_t kTrackedStatusPollWindowMs=600000;
bool timeReached(uint32_t a,uint32_t b){return int32_t(a-b)>=0;}
unsigned delayTimerInit(){return millis();}
enum class ParameterSemantic {SlatOrientation,HangerOrientation};
struct IoHomeProfileDescriptor{};
uint8_t ioHomeParameterIndex(const IoHomeProfileDescriptor*,ParameterSemantic){return 3;}
struct Controller {
 unsigned submissions=0;uint8_t maxAttempts=0;
 bool sendBackgroundCommand(uint32_t,const uint8_t*,IoHomeCommand,uint8_t,uint8_t,uint8_t attemptsParam,uint8_t attempts) {
  (void)attemptsParam;++submissions;maxAttempts=attempts;return true;
 }
};
struct IoHomecontrolChannel {
 bool mHas2WHeardEvidence=false,mHas2WMovingEvidence=false,mConfirmsExecute=false,mStopSettlePollPending=false;
 uint32_t mLast2WHeardMs=0,mLast2WMovingEvidenceMs=0,m2WMovingEvidenceGeneration=0;
 bool snapshot=true;unsigned restores=0,polls=0;
 bool mIs1W=false,mStatusExpected=false,mSingleFollowUpPollPending=false;
 uint32_t mNodeId=0x123456,mStatusPollTimer=0,mPollTrackingDeadlineMs=0,mNextStatusPollMs=0;
 uint8_t mEncKey[16]{},mAuthPollFailures=0,mStatusPollFailures=0;
 Controller mController;
 bool allowsActuatorControls(){return true;}
 const IoHomeProfileDescriptor*getEffectiveProfileDescriptor(){return nullptr;}
 bool isTiltCapableDeviceType(){return false;}
 bool requestStatus(bool);
 void onStatusPollFailed(bool);
 void clearStopTravelSnapshot(){snapshot=false;}
 void restoreStopTravelSnapshot(){++restores;}
 void scheduleStatusPoll(unsigned){++polls;}
 unsigned defaultTrackedStatusPollDelayMs(){return 500;}
 void observe2WMovingStatus(bool);
 void onCommandExchangeResult(IoHomeCommand,uint8_t,IoHomeCommandExchangeResult);
 void onUnanswered2WWake(uint32_t,uint32_t);
 TwoWayWakeBelief twoWayWakeBeliefAt(uint32_t,bool=false)const;
 bool twoWayLastHeardAgeAt(uint32_t,uint32_t&)const;
};
'''
tests=r'''int main(){
 IoHomecontrolChannel ch;
 ch.onCommandExchangeResult(IoHomeCommand::Execute,50,IoHomeCommandExchangeResult::Completed);
 assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Asleep);
 for(auto result:{IoHomeCommandExchangeResult::Completed,IoHomeCommandExchangeResult::AuthenticatedUnconfirmed,
                  IoHomeCommandExchangeResult::ExplicitlyRejected,IoHomeCommandExchangeResult::Unknown,
                  IoHomeCommandExchangeResult::SessionExhausted,IoHomeCommandExchangeResult::MediaAccessFailed,
                  IoHomeCommandExchangeResult::FailedBeforeAuthentication}) {
  ch.observe2WMovingStatus(true);ch.onCommandExchangeResult(IoHomeCommand::Execute,0xD2,result);
  assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Awake);
  ch.observe2WMovingStatus(false);ch.onCommandExchangeResult(IoHomeCommand::Execute,0xD2,result);
  assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Asleep);
 }
 ch.observe2WMovingStatus(true);ch.onUnanswered2WWake(now,ch.m2WMovingEvidenceGeneration);assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Asleep);
 ++now;ch.observe2WMovingStatus(true);ch.onUnanswered2WWake(now-1,ch.m2WMovingEvidenceGeneration);assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Awake);
 now=UINT32_MAX-10;ch.observe2WMovingStatus(true);now=10;assert(ch.twoWayWakeBeliefAt(now)==TwoWayWakeBelief::Awake);
 uint32_t age;assert(ch.twoWayLastHeardAgeAt(now,age)&&age==21);
 ch.observe2WMovingStatus(false);assert(ch.twoWayWakeBeliefAt(now,true)==TwoWayWakeBelief::Asleep);
 assert(ch.restores==6);assert(ch.polls==6);
 ch.mStopSettlePollPending=false;assert(ch.requestStatus(true)&&ch.mController.maxAttempts==1);
 ch.mStopSettlePollPending=true;assert(ch.requestStatus(true)&&ch.mController.maxAttempts==IOHC_EXCHANGE_MAX_ATTEMPTS);
 assert(!ch.mStopSettlePollPending);
 ch.mIs1W=true;auto submissions=ch.mController.submissions;assert(!ch.requestStatus(true)&&ch.mController.submissions==submissions);
 ch.mIs1W=false;now=UINT32_MAX-100;
 ch.onStatusPollFailed(false);assert(ch.mStatusPollFailures==1&&uint32_t(ch.mNextStatusPollMs-now)==1000);
 ch.onStatusPollFailed(true);assert(ch.mAuthPollFailures==1&&uint32_t(ch.mNextStatusPollMs-now)==2000);
 ch.mPollTrackingDeadlineMs=now+500;ch.onStatusPollFailed(false);
 assert(!ch.mSingleFollowUpPollPending&&ch.mNextStatusPollMs==0);
 ch.mStatusPollFailures=255;ch.mAuthPollFailures=255;ch.mPollTrackingDeadlineMs=now+600000;
 ch.onStatusPollFailed(false);assert(ch.mStatusPollFailures==255&&uint32_t(ch.mNextStatusPollMs-now)==2000);
 ch.onStatusPollFailed(true);assert(ch.mAuthPollFailures==255&&uint32_t(ch.mNextStatusPollMs-now)==4000);
}
'''
radio=(root/'src/radio/RadioSX1262.cpp').read_text()
radio_methods='\n'.join(method(radio,s) for s in ['RadioError RadioSX1262::setPreambleLengthInternal(', 'bool RadioSX1262::applyPacketParams('])
radio_harness=r'''#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include "radio/RadioTypes.h"
#include "radio/SX1262Preamble.h"
#include "radio/SX1262IoHomePhy.h"
#include "radio/sx1262Regs-Fsk.h"
#include "protocol/IoHomeCommands.h"
#define logError(...) ((void)0)
struct RadioSX1262 {
 bool mInitialized=true,mSoftwarePhyMode=true,failStandby=false,failCommand=false;
 RadioState mState=RadioState::Receiving;
 uint16_t mPreambleLength=1024;
 uint8_t mPacketPayloadLen=48,mSyncWordBits=24,params[9]{};
 unsigned standbyCalls=0,commands=0;
 bool tryStandby(bool){++standbyCalls;if(failStandby)return false;mState=RadioState::Idle;return true;}
 bool sendCommand(unsigned op,const uint8_t*data,unsigned length,bool){assert(op==SX1262_CMD_SET_PACKET_PARAMS&&length==9);++commands;memcpy(params,data,9);return !failCommand;}
 RadioError setPreambleLengthInternal(uint16_t,bool);
 bool applyPacketParams(bool=true);
};
'''
radio_tests=r'''int main(){
 RadioSX1262 r;
 for(uint16_t value:{0,8192,65535}){
  auto state=r.mState;auto length=r.mPreambleLength;auto calls=r.commands;auto standby=r.standbyCalls;
  assert(r.setPreambleLengthInternal(value,false)==RadioError::InvalidParam);
  assert(r.mState==state&&r.mPreambleLength==length&&r.commands==calls&&r.standbyCalls==standby);
 }
 for(uint16_t value:{8,1024,2450,8191}) {
  assert(r.setPreambleLengthInternal(value,true)==RadioError::None);
  assert(r.mPreambleLength==value&&((uint16_t(r.params[0])<<8)|r.params[1])==uint32_t(value)*8);
 }
 r.mState=RadioState::Receiving;r.failStandby=true;
 assert(r.setPreambleLengthInternal(2450,false)==RadioError::Busy&&r.mPreambleLength==8191);
 r.failStandby=false;r.failCommand=true;
 assert(r.setPreambleLengthInternal(2450,false)==RadioError::Busy&&r.mPreambleLength==8191);
 r.mPreambleLength=8192;auto calls=r.commands;assert(!r.applyPacketParams()&&r.commands==calls);
}
'''
with tempfile.TemporaryDirectory() as d:
    for name,src in [('channel',harness+methods+tests),('radio',radio_harness+radio_methods+radio_tests)]:
        p=Path(d)/(name+'.cpp');p.write_text(exchange_limit+'\n#include <initializer_list>\n'+src)
        binary=Path(d)/name
        subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-I'+str(root/'src'),str(p),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('Production STOP/motion evidence and SX1262 packet parameters/bounds passed')
