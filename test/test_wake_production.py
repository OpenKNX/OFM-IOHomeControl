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
    'void IoHomecontrolChannel::onEstimate(',
    'void IoHomecontrolChannel::onPositionFeedback(',
    'void IoHomecontrolChannel::onTargetPositionFeedback(',
    'void IoHomecontrolChannel::observe2WMovingStatus(',
    'void IoHomecontrolChannel::onCommandExchangeResult(',
    'void IoHomecontrolChannel::onUnanswered2WWake(',
    'TwoWayWakeBelief IoHomecontrolChannel::twoWayWakeBeliefAt(',
    'bool IoHomecontrolChannel::twoWayLastHeardAgeAt(',
    'void IoHomecontrolChannel::onStatusPollFailed(',
    'void IoHomecontrolChannel::onStatusUpdate(',
    'void IoHomecontrolChannel::clearStatusPollTracking(',
    'void IoHomecontrolChannel::stopTravelEstimation(',
    'bool IoHomecontrolChannel::requestStatus(bool'])
harness=r'''#include <cassert>
#include <cstdint>
#include "protocol/IoHomeCommands.h"
#include "protocol/IoHomeOptimisticPosition.h"
#include "protocol/IoHomeStatusPollRetry.h"
#include "protocol/IoHomeRemainingTime.h"
unsigned now=1000;unsigned millis(){return now;}
#define logInfoP(...) ((void)0)
#define logDebugP(...) ((void)0)
#define logInfoP(...) ((void)0)
constexpr uint32_t kTrackedStatusPollWindowMs=600000,kTrackedStatusEstimateBiasMs=1000;
constexpr uint32_t kLowPowerTrackedPollMinMs=10000;
bool timeReached(uint32_t a,uint32_t b){return int32_t(a-b)>=0;}
unsigned delayTimerInit(){return millis();}
float clampPercent(float v){return v<0?0:v>100?100:v;}
enum class ParameterSemantic {SlatOrientation,HangerOrientation};
struct IoHomeProfileDescriptor{};
uint8_t ioHomeParameterIndex(const IoHomeProfileDescriptor*,ParameterSemantic){return 3;}
struct Controller {
 unsigned submissions=0;uint8_t maxAttempts=0;uint32_t rfWait=0;
 unsigned statusPollRetryAfterMs(){return rfWait;}
 bool sendStatusPoll(void*,bool,uint16_t,uint8_t attemptsParam,uint8_t attempts) {
  (void)attemptsParam;++submissions;maxAttempts=attempts;return true;
 }
};
struct Knx {bool enabled=false;bool configured(){return enabled;}} knx;
bool movementKo=false;unsigned movementPublications=0;
struct Dpt {Dpt(int,int){}};
constexpr int IOHC_KoCHMovementStatus=0;
struct Ko {void value(bool value,Dpt){movementKo=value;++movementPublications;}};
struct IoHomecontrolChannel {
 struct {bool valid=false;} mStopTravelSnapshot;
 bool mIsMoving=false;
 bool isBinaryDeviceType(){return false;}
 Ko getKo(int){return {};}
 void onStatusUpdate(bool);
 void clearStatusPollTracking();
 void stopTravelEstimation(bool);
 float staleEstimate=-1;float estimateCurrentPosition(){return staleEstimate>=0?staleEstimate:mCurrentPosition;}
 void onEstimate(uint16_t);
 void onPositionFeedback(float);
 void onTargetPositionFeedback(float);
 bool mObservedPositiveRemaining=false,mTrustedPollMoving=false;
 void startTravelEstimation(float){}
 bool mHas2WHeardEvidence=false,mHas2WMovingEvidence=false,mConfirmsExecute=false,mStopSettlePollPending=false;
 uint32_t mLast2WHeardMs=0,mLast2WMovingEvidenceMs=0,m2WMovingEvidenceGeneration=0;
 bool snapshot=true;unsigned restores=0,polls=0;
 bool mIs1W=false,mStatusExpected=false,mSingleFollowUpPollPending=false,mPaired=true,lowPower=false;
 bool effectiveLowPower2W(){return lowPower;}
 unsigned _channelIndex=0;uint32_t mRfPollRetryAtMs=0;bool mRfPollWaiting=false;
 uint32_t mNodeId=0x123456,mStatusPollTimer=0,mPollTrackingDeadlineMs=0,mNextStatusPollMs=0;
 uint8_t mEncKey[16]{},mAuthPollFailures=0,mStatusPollFailures=0;
 IoHomeOptimisticPosition mOptimisticPosition;
 uint32_t mTravelDurationMs=0,mTravelStartTime=0;
 float mCurrentPosition=0,mTargetPosition=0,mTravelStartPosition=0;
 unsigned publications=0;
 void publishPositionFeedback(float p,bool){mCurrentPosition=p;++publications;}
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
 {
  IoHomecontrolChannel c;c.mIsMoving=true;c.mPollTrackingDeadlineMs=now+600000;c.mNextStatusPollMs=now+500;
  c.onTargetPositionFeedback(80);c.onPositionFeedback(20);c.staleEstimate=90;
  c.onEstimate(256);assert(c.mCurrentPosition==20&&c.mTargetPosition==80&&c.mTravelStartPosition==20&&c.mTravelDurationMs==256000&&c.publications==1);
  assert(c.mNextStatusPollMs==now+500&&c.mPollTrackingDeadlineMs==now+600000);
  for(uint16_t raw:{uint16_t(3),uint16_t(2),uint16_t(1)})c.onEstimate(raw);
  c.mNextStatusPollMs=0;c.onEstimate(0);assert(c.mNextStatusPollMs==now+500); // configured cadence already shorter
  c.mObservedPositiveRemaining=false;c.mNextStatusPollMs=0;c.onEstimate(0);assert(!c.mNextStatusPollMs);
  for(uint16_t raw:{uint16_t(0xFFFE),uint16_t(0xFFFF)}){c.mNextStatusPollMs=0;c.mTravelDurationMs=42;c.onEstimate(raw);assert(!c.mNextStatusPollMs&&c.mTravelDurationMs==42&&!c.mObservedPositiveRemaining);}
  c.lowPower=true;c.mNextStatusPollMs=0;c.onEstimate(255);assert(c.mNextStatusPollMs==now+10000&&c.mTravelDurationMs==255000);
  c.mNextStatusPollMs=0;c.onEstimate(0);assert(c.mNextStatusPollMs==now+10000);
  c.onStatusUpdate(false);assert(!c.mObservedPositiveRemaining&&!c.mTrustedPollMoving);c.onEstimate(1);assert(!c.mNextStatusPollMs&&!c.mTravelDurationMs);
  c.mIsMoving=true;c.mPollTrackingDeadlineMs=0;c.onEstimate(1);assert(!c.mPollTrackingDeadlineMs&&!c.mNextStatusPollMs);
  c.mPollTrackingDeadlineMs=now+600000;c.mIsMoving=false;c.onEstimate(1);assert(!c.mNextStatusPollMs);
 }

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
 // Execute rejection calls the production callback, not a test reimplementation.
 ch.mOptimisticPosition.begin(50,25);ch.mOptimisticPosition.observe(30);
 ch.mTravelDurationMs=20000;ch.mTravelStartTime=100;ch.mTargetPosition=50;
 auto pollsBefore=ch.polls;
 ch.onCommandExchangeResult(IoHomeCommand::Execute,50,IoHomeCommandExchangeResult::AuthenticatedUnconfirmed);
 assert(ch.mTravelDurationMs==20000&&ch.mTargetPosition==50);
 ch.onCommandExchangeResult(IoHomeCommand::Execute,100,IoHomeCommandExchangeResult::ExplicitlyRejected);
 assert(ch.mTravelDurationMs==20000&&ch.mTargetPosition==50);
 ch.onCommandExchangeResult(IoHomeCommand::Execute,50,IoHomeCommandExchangeResult::ExplicitlyRejected);
 assert(ch.mTravelDurationMs==0&&ch.mTravelStartTime==0&&ch.mTargetPosition==30&&ch.mCurrentPosition==30);
 assert(ch.polls==pollsBefore+1&&ch.publications==1);
 ch.mOptimisticPosition={};ch.mOptimisticPosition.begin(100,40);ch.mTargetPosition=100;
 ch.onCommandExchangeResult(IoHomeCommand::Execute,100,IoHomeCommandExchangeResult::MediaAccessFailed);
 assert(ch.mTargetPosition==40&&ch.mCurrentPosition==40&&ch.publications==1);

 ch.mStopSettlePollPending=false;assert(ch.requestStatus(true)&&ch.mController.maxAttempts==1);
 ch.mStopSettlePollPending=true;assert(ch.requestStatus(true)&&ch.mController.maxAttempts==IOHC_EXCHANGE_MAX_ATTEMPTS);
 assert(!ch.mStopSettlePollPending);
 ch.mController.rfWait=3600000;auto before=ch.mController.submissions;assert(!ch.requestStatus(false));
 assert(ch.mRfPollWaiting&&ch.mRfPollRetryAtMs==millis()+3600250);assert(ch.mController.submissions==before);
 ch.mController.rfWait=0;assert(!ch.requestStatus(false));now=ch.mRfPollRetryAtMs;
 assert(ch.requestStatus(false)&&!ch.mRfPollWaiting&&ch.mController.submissions==before+1);
 ch.mIs1W=true;auto submissions=ch.mController.submissions;assert(!ch.requestStatus(true)&&ch.mController.submissions==submissions);
 ch.mIs1W=false;now=UINT32_MAX-100;ch.mPollTrackingDeadlineMs=now+600000;
 // Test the production scheduler, including wrap, exact deadline and sleeping peers.
 const uint32_t auth[]={5000,30000,120000,300000,300000};
 for(auto delay:auth){ch.onStatusPollFailed(true);assert(uint32_t(ch.mNextStatusPollMs-now)==delay);}
 const uint32_t silent[]={5000,15000,30000,60000,300000,300000};
 for(auto delay:silent){ch.onStatusPollFailed(false);assert(uint32_t(ch.mNextStatusPollMs-now)==delay);}
 assert(ch.mAuthPollFailures==0);
 ch.onStatusPollFailed(true);assert(ch.mStatusPollFailures==0&&uint32_t(ch.mNextStatusPollMs-now)==5000);
 ch.onStatusPollFailed(false);assert(ch.mAuthPollFailures==0&&uint32_t(ch.mNextStatusPollMs-now)==5000);
 ch.mStatusPollFailures=255;ch.onStatusPollFailed(false);
 assert(ch.mStatusPollFailures==255&&uint32_t(ch.mNextStatusPollMs-now)==300000);
 ch.mAuthPollFailures=255;ch.onStatusPollFailed(true);
 assert(ch.mAuthPollFailures==255&&ch.mStatusPollFailures==0&&uint32_t(ch.mNextStatusPollMs-now)==300000);
 ch.onStatusUpdate(false);assert(ch.mAuthPollFailures==0&&ch.mStatusPollFailures==0);
 ch.mPollTrackingDeadlineMs=now+600000;ch.onStatusPollFailed(true);
 assert(uint32_t(ch.mNextStatusPollMs-now)==5000);
 ch.lowPower=true;ch.mAuthPollFailures=0;ch.onStatusPollFailed(true);
 assert(uint32_t(ch.mNextStatusPollMs-now)==10000);
 ch.mPollTrackingDeadlineMs=now+5000;ch.onStatusPollFailed(false);
 assert(!ch.mSingleFollowUpPollPending&&ch.mNextStatusPollMs==0);
 auto streak=ch.mStatusPollFailures;ch.mPollTrackingDeadlineMs=now;
 ch.onStatusPollFailed(false);assert(ch.mStatusPollFailures==streak);
 ch.mPollTrackingDeadlineMs=0;ch.onStatusPollFailed(true);assert(ch.mPollTrackingDeadlineMs==0);
 ch.mPaired=false;ch.mPollTrackingDeadlineMs=now+600000;ch.onStatusPollFailed(false);
 assert(ch.mStatusPollFailures==streak);

}
'''
import sys
sys.path.insert(0,str(root/'tools'))
import laberning_corpus
_, originals=laberning_corpus.load_captures()
ssl=next(d for e,d in originals if d['id']=='velux_ssl_statuspoll_stopped_off_target')
rows=[]
for frame in ssl['frames']:
    raw=bytes.fromhex(frame['hex']);body=raw[9:]
    rows.append((body[0]&1,int.from_bytes(body[4:6],'big'),int.from_bytes(body[2:4],'big')))
checks='\n knx.enabled=true;ch.mPaired=true;\n'
for stopped,current,target in rows:
    checks+=f" ch.mCurrentPosition={current}/512.f;ch.mTargetPosition={target}/512.f;ch.mPollTrackingDeadlineMs=now+600000;ch.mNextStatusPollMs=now+2000;ch.mSingleFollowUpPollPending=true;ch.mTravelDurationMs=10000;ch.onStatusUpdate({'false' if stopped else 'true'});\n"
    checks+=f" assert(ch.mCurrentPosition=={current}/512.f&&ch.mTargetPosition=={target}/512.f);assert(movementKo=={'false' if stopped else 'true'});\n"
    if stopped:
        checks+=" assert(!ch.mIsMoving&&ch.mTravelDurationMs==0&&ch.mPollTrackingDeadlineMs==0&&ch.mNextStatusPollMs==0&&!ch.mSingleFollowUpPollPending);ch.onStatusPollFailed(true);assert(ch.mPollTrackingDeadlineMs==0&&!ch.mSingleFollowUpPollPending);\n"
checks+=' assert(movementPublications==6);\n'
last=tests.rfind('}')
tests=tests[:last]+checks+tests[last:]
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
