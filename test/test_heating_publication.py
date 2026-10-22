"""Exercise actual heating KO publishers against recording KNX/controller adapters."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/IoHomecontrolChannel.cpp').read_text()
def method(signature):
    start=source.index(signature); at=source.index('{',start)+1; depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
methods='\n'.join(method(s) for s in ('void IoHomecontrolChannel::syncHeatingConfiguration()', 'bool IoHomecontrolChannel::privateHeatingEnabled()', 'bool IoHomecontrolChannel::agiliaHomeAwayEnabled()', 'void IoHomecontrolChannel::onHomeAwayCommandStatus()', 'void IoHomecontrolChannel::processHomeAwayInputKo(', 'void IoHomecontrolChannel::invalidateHeatingFeedback()', 'void IoHomecontrolChannel::processHeatingInputKo(', 'void IoHomecontrolChannel::onHeatingValue('))
harness=r'''
#include "protocol/IoHomeHeatingRegisters.h"
#include <cassert>
struct Dpt {unsigned main,sub;Dpt(unsigned a,unsigned b):main(a),sub(b){}};
struct GroupObject {unsigned writes=0;double stored=0;unsigned main=0,sub=0;
 template<class T> void value(T v,Dpt d){++writes;stored=v;main=d.main;sub=d.sub;}
 double value(Dpt)const{return stored;}
};
struct {bool enabled=true;GroupObject ko[2048];bool configured()const{return enabled;}
 GroupObject &getGroupObject(unsigned number){assert(number<2048);return ko[number];}} knx;
bool modelConfirmed=false,feature=true,maxObject=true;unsigned pollSeconds=300;
#define ParamHTG_cPollSeconds pollSeconds
#define ParamHTG_cEnabled feature
#define ParamHTG_cAgiliaHomeAway modelConfirmed
#define HAW_KoCalcNumber(i) (1960+_channelIndex*3+(i))
#define HAW_KocCommand 0
#define HAW_KocConfirmedCommand 1
#define HAW_KocStatus 2
#define ParamHTG_cMaxObject maxObject
#define HTG_KoCalcNumber(i) (1800+_channelIndex*10+(i))
#define HTG_KocRoomTemperature 0
#define HTG_KocSetpointFeedback 1
#define HTG_KocModeFeedback 2
#define HTG_KocValid 3
#define HTG_KocHeating 4
#define HTG_KocWindow 5
#define HTG_KocDetectionSet 6
#define HTG_KocDetectionFeedback 7
#define HTG_KocMaxSetpoint 8
#define HTG_KocRead 9
#define IOHC_KoCHCozyTempFeedback 21
#define IOHC_KoCHCozyWindow 24
struct IoHomecontrolChannel;
struct Controller {IoHomeHeating::State state;unsigned requests=0,refreshes=0;uint16_t lastRegister=0;float lastValue=0;
 const auto &heatingState(unsigned)const{return state;}
 bool requestHeatingRegister(IoHomecontrolChannel*,uint16_t reg,bool write=false,float value=0){assert(write);++requests;lastRegister=reg;lastValue=value;return true;}
 void invalidateHeatingChannel(unsigned){state.invalidate();}
 bool requestHomeAway(IoHomecontrolChannel*,bool home){++requests;lastRegister=0x0110;lastValue=home;return true;}
 bool refreshHeating(unsigned){++refreshes;return true;}
 bool configureHeatingPoll(unsigned,unsigned seconds){state.pollIntervalMs=seconds*1000;return true;}
};
struct IoHomecontrolChannel {
 unsigned _channelIndex=0;uint32_t mNodeId=0x123456;bool mIs1W=false,mPaired=true;IoHomeProtocolIdentity id;Controller mController;
 bool mHeatingConfigSeen=false,mHeatingWasEnabled=false;uint16_t mHeatingConfiguredPoll=0;
 void syncHeatingConfiguration();bool privateHeatingEnabled()const;void invalidateHeatingFeedback();void processHeatingInputKo(uint8_t,GroupObject&);
 void onHeatingValue(uint16_t,const IoHomeHeating::Value&);
 bool agiliaHomeAwayEnabled()const;void onHomeAwayCommandStatus();void processHomeAwayInputKo(uint8_t,GroupObject&);
 bool isOperational()const{return mPaired;}const auto &getProtocolIdentity()const{return id;}
 GroupObject &getKo(unsigned index){return knx.getGroupObject(600+index);}
};
'''
tests=r'''
int main(){
 IoHomecontrolChannel ch;ch.id.valid=true;ch.id.manufacturerId=12;ch.id.profile=52;ch.id.subProfile=1;ch.id.ioAddress=0x123456;
 auto &state=ch.mController.state;IoHomeHeating::Value measured;measured.valid=true;measured.value=19.13f;
 state.values[5]={measured,1,1,true};ch.onHeatingValue(0x0115,measured);
 assert(knx.ko[1800].writes==1&&knx.ko[1800].main==9&&knx.ko[1800].sub==1);
 assert(knx.ko[1803].stored==32);assert(knx.ko[1804].writes==0); // no invented heating status
 IoHomeHeating::Value fault;fault.sensorFault=true;state.values[5]={fault,2,2,true};ch.onHeatingValue(0x0115,fault);
 assert(knx.ko[1800].writes==1&&knx.ko[1803].stored==0);
 measured.value=0;measured.raw=0;state.values[6]={measured,3,3,true};ch.onHeatingValue(0x0117,measured);
 assert(knx.ko[1804].writes==1&&knx.ko[1804].stored==0);
 measured.value=2;measured.raw=2;ch.onHeatingValue(0x0100,measured);
 assert(knx.ko[1802].stored==2&&knx.ko[1802].main==5&&knx.ko[1802].sub==10);
 GroupObject input;input.stored=1;ch.processHeatingInputKo(5,input);assert(ch.mController.requests==0);
 ch.processHeatingInputKo(6,input);assert(ch.mController.requests==1&&ch.mController.lastRegister==0x0106&&ch.mController.lastValue==1);
 ch.processHeatingInputKo(9,input);assert(ch.mController.refreshes==1);
 ch.id.manufacturerId=2;const auto count=knx.ko[1800].writes;ch.onHeatingValue(0x0115,measured);ch.processHeatingInputKo(6,input);
 assert(knx.ko[1800].writes==count&&ch.mController.requests==1);
 ch.id.manufacturerId=12;ch.mIs1W=true;ch.processHeatingInputKo(6,input);assert(ch.mController.requests==1);
 ch.mIs1W=false;feature=false;ch.onHeatingValue(0x0115,measured);assert(knx.ko[1800].writes==count);
 feature=true;ch.invalidateHeatingFeedback();assert(knx.ko[1803].stored==0);
 ch.syncHeatingConfiguration();assert(state.pollIntervalMs==300000);
 state.pollIntervalMs=60000;ch.syncHeatingConfiguration();assert(state.pollIntervalMs==60000); // CLI override survives normal loops
 pollSeconds=600;ch.syncHeatingConfiguration();assert(state.pollIntervalMs==600000);
 feature=false;ch.syncHeatingConfiguration();assert(state.pollIntervalMs==0); // explicit ETS disable wins
 feature=true;input.stored=1;const auto previous=ch.mController.requests;
 ch.processHomeAwayInputKo(0,input);assert(ch.mController.requests==previous);
 modelConfirmed=true;ch.processHomeAwayInputKo(0,input);assert(ch.mController.requests==previous+1&&ch.mController.lastRegister==0x0110&&ch.mController.lastValue==1);
 input.stored=0;ch.processHomeAwayInputKo(0,input);assert(ch.mController.lastValue==0);
 using R=IoHomeHeating::PresenceResult;
 state.presenceResult=R::Queued;ch.onHomeAwayCommandStatus();assert(knx.ko[1962].stored==1&&knx.ko[1961].writes==0);
 state.presenceResult=R::RfSent;ch.onHomeAwayCommandStatus();assert(knx.ko[1962].stored==2&&knx.ko[1961].writes==0);
 measured.valid=true;measured.raw=1;state.values[IoHomeHeating::index(0x0110)]={measured,10,20,true};
 state.presenceResult=R::AcknowledgedEffectUnknown;ch.onHomeAwayCommandStatus();assert(knx.ko[1961].stored==1&&knx.ko[1961].main==1&&knx.ko[1961].sub==1&&knx.ko[1962].stored==3);
 const auto confirmationWrites=knx.ko[1961].writes;
 state.presenceResult=R::Rejected;ch.onHomeAwayCommandStatus();assert(knx.ko[1961].writes==confirmationWrites&&knx.ko[1962].stored==4);
 ch._channelIndex=15;state.presenceResult=R::AcknowledgedEffectUnknown;ch.onHomeAwayCommandStatus();assert(knx.ko[2006].stored==1&&knx.ko[2007].stored==3);
 ch.invalidateHeatingFeedback();assert(knx.ko[2007].stored==0&&!state.values[IoHomeHeating::index(0x0110)].observed);
 ch._channelIndex=0;ch.id.profile=22;const auto requests=ch.mController.requests;ch.processHomeAwayInputKo(0,input);assert(ch.mController.requests==requests);


}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'heating.cpp';p.write_text(harness+methods+tests);binary=Path(d)/'heating'
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-I'+str(root/'src'),str(p),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Production heating KO validity, sensor fault, DPTs, metadata guards and read-only window passed')
