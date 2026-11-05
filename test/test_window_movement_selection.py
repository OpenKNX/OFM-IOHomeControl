"""Execute the production movement selector across main-only and MVS configurations."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def method(source,signature):
 start=source.index(signature);end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
source=(root/'src/IoHomecontrolChannel.cpp').read_text()
methods='\n'.join(method(source,s) for s in ['uint8_t IoHomecontrolChannel::movementExecuteProfile() const','bool IoHomecontrolChannel::sendTwoWayMovement('])
harness=r'''
#include <cassert>
#include "protocol/IoHomeCommands.h"
#include "protocol/IoHomeProfileRegistry.h"
bool mvsEnabled=false;
#define ParamBASE_ModuleEnabled_MVS mvsEnabled
#define ParamIOHC_cDeviceType 1
struct Controller {
 unsigned plain=0,speed=0;uint8_t target=0,selected=0;
 bool sendCommand(uint32_t,const uint8_t*,IoHomeCommand,uint8_t p,uint8_t,uint8_t mode){++plain;target=p;selected=mode;return true;}
 bool sendProfileMovementCommand(uint32_t,const uint8_t*,uint8_t p,uint8_t,uint16_t){++speed;target=p;return true;}
};
struct IoHomecontrolChannel {
 bool enabled=false,mIs1W=false;uint8_t mRequestedSpeedIndex=0,mMovementMode=0;uint16_t mRequestedSpeedRaw=0;
 uint32_t mNodeId=1;uint8_t mEncKey[16]{};Controller mController;uint8_t sub=0;
 bool windowMainOnly()const{return enabled;}
 const IoHomeProfileDescriptor *getEffectiveProfileDescriptor()const{return ioHomeProfileDescriptor(4,sub);}
 uint8_t movementExecuteProfile()const;bool sendTwoWayMovement(uint8_t);
};
'''
tests=r'''
int main(){for(bool mvs:{false,true})for(bool enabled:{false,true})for(unsigned mode=0;mode<3;++mode)for(uint8_t sub:{0,1})for(uint8_t target:{0,100}){
 mvsEnabled=mvs;IoHomecontrolChannel ch;ch.enabled=enabled;ch.mMovementMode=mode;ch.sub=sub;
 assert(ch.sendTwoWayMovement(target));assert(ch.mController.target==target);
 if(enabled){assert(ch.mController.plain==1&&ch.mController.speed==0&&ch.mController.selected==0xFF);
  ch.mRequestedSpeedIndex=1;assert(!ch.sendTwoWayMovement(target));assert(ch.mController.plain==1&&ch.mController.speed==0);}
#ifdef MVS_ParamBlockOffset
 else if(mvs)assert(ch.mController.speed==1&&ch.mController.plain==0);
 else assert(ch.mController.plain==1&&ch.mController.selected==0xFF);
#else
 else assert(ch.mController.plain==1);
#endif
}}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text('#include <initializer_list>\n'+harness+methods+tests)
 for defines in [[],['-DMVS_ParamBlockOffset=0']]:
  subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17',*defines,'-I'+str(root/'src'),str(p/'test.cpp'),str(root/'src/protocol/IoHomeProfileRegistry.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
print('Production Window movement selector: profiles 0100/0101, main-only opt-in, MVS compiled/enabled/disabled, movement modes and explicit speed conflict passed')
