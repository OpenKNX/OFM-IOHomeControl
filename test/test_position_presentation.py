"""Compile production rounding and scene inversion without rewriting measured values."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def method(text,signature):
 start=text.index(signature);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
source=(root/'src/IoHomecontrolChannel.cpp').read_text()
methods='\n'.join(method(source,s) for s in ['void IoHomecontrolChannel::publishPositionFeedback(', 'float IoHomecontrolChannel::sceneToDevicePosition(', 'uint8_t IoHomecontrolChannel::currentPositionToSceneValue('])
harness=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#define logDebugP(...) ((void)0)
bool invert=false;
#define ParamIOHC_cInvertDir invert
constexpr unsigned IoHomeCapabilityBinaryOnly=1;
constexpr int IOHC_KoCHPositionFeedback=0,DPT_Scaling=0;
struct IoHomeProfileDescriptor {unsigned capabilityFlags=0;};
struct {bool configured(){return true;}} knx;
uint8_t published=255;
struct Ko {void value(uint8_t v,int){published=v;}};
float clampPercent(float v){return v<0?0:v>100?100:v;}
struct IoHomecontrolChannel {
 float mCurrentPosition=0;bool mIs1W=false;
 const IoHomeProfileDescriptor *getEffectiveProfileDescriptor(){return nullptr;}
 Ko getKo(int){return {};}
 void publishBinaryStatus(){}
 void publishPositionFeedback(float,bool);
 float sceneToDevicePosition(uint8_t)const;
 uint8_t currentPositionToSceneValue()const;
};
'''
tests=r'''
int main(){
 for(bool oneWay:{false,true})for(bool reverse:{false,true}){
  IoHomecontrolChannel ch;ch.mIs1W=oneWay;invert=reverse;
  const float measured[]={99.66f,0.25f,98.f,60.f};
  const uint8_t displayed[]={100,0,98,60};
  for(unsigned i=0;i<4;++i){
   ch.publishPositionFeedback(measured[i],false);
   assert(ch.mCurrentPosition==measured[i]);
   const uint8_t expected=reverse?uint8_t(100-displayed[i]):displayed[i];
   assert(published==expected&&ch.currentPositionToSceneValue()==expected);
  }
  for(uint8_t user:{uint8_t(0),uint8_t(25),uint8_t(100)}){
   const float wire=ch.sceneToDevicePosition(user);
   assert(wire==(reverse?100.f-user:float(user)));
   ch.publishPositionFeedback(wire,false);assert(published==user);
  }
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'presentation.cpp';p.write_text(harness+methods+tests);binary=Path(tmp)/'presentation'
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17',str(p),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('Production endpoint presentation preserves fractional measurements and single scene inversion')
