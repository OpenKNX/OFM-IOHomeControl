"""Execute the production event console branch and raw-event formatter offline."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
def block(source,needle):
    start=source.index(needle);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
command=block((root/'src/IoHomecontrol.cpp').read_text(),'if(iCmd.rfind("iohc event ",0)==0)')
formatter=block((root/'src/IoHomecontrolChannel.cpp').read_text(),'void IoHomecontrolChannel::printLastDeviceEvent()')
harness=r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "protocol/IoHomeDeviceEvent.h"
uint32_t now=10;uint32_t millis(){return now;}
std::string messages;
void logInfoP(const char*format,...){char text[512];va_list args;va_start(args,format);
vsnprintf(text,sizeof(text),format,args);va_end(args);messages+=text;messages+='\n';}
struct IoHomecontrolChannel {
 unsigned _channelIndex=0;
 IoHomeDeviceEvent event;
 const IoHomeDeviceEvent &lastDeviceEvent()const{return event;}
 void clearLastDeviceEvent(){event.clear();}
 void printLastDeviceEvent();
};
struct IoHomecontrol {
 unsigned mNumChannels=16;
 IoHomecontrolChannel *mChannels[16]{};
 bool run(const std::string &iCmd,bool iDebugKo=false);
};
'''
tests=r'''
int main(){
 IoHomecontrol module;IoHomecontrolChannel channel;module.mChannels[0]=&channel;
 assert(module.run("iohc event 1 status"));assert(messages.find("none (session-only)")!=std::string::npos);
 IoHomeFrame frame;frame.init();frame.commandId=IoHomeCommand::StatusUpdate;
 frame.setSrcNode(0x654321);frame.setDestNode(0x123456);
 const uint8_t data[]={5,0x60,0x20,5,5,6,0,0,0,0};memcpy(frame.data,data,sizeof(data));frame.dataLen=sizeof(data);
 assert(channel.event.record(frame,2,UINT32_MAX-9));
 messages.clear();assert(module.run("iohc event 1 status"));
 for(const char*expected:{"Last Device Event ch=1","event_id=2005","count=1","src=654321","dst=123456",
      "cmd=71","rx_freq_idx=2","age_ms=20","raw=05 60 20 05 05 06 00 00 00 00 ","proof=inbound_challenge","UNIT UNKNOWN"})
     assert(messages.find(expected)!=std::string::npos);
 for(const char*bad:{"iohc event 0 clear","iohc event 17 clear","iohc event 1 clear extra","iohc event x clear","iohc event 1 subscribe"}){
     messages.clear();assert(module.run(bad));assert(channel.event.valid);
     assert(messages.find("Usage:")!=std::string::npos);
 }
 messages.clear();assert(module.run("iohc event 1 clear",true));assert(channel.event.valid);
 assert(messages.find("requires local console")!=std::string::npos);
 messages.clear();assert(module.run("iohc event 1 status",true));assert(messages.find("raw=")==std::string::npos);
 assert(module.run("iohc event 2 status")); // absent channel safely ignored
 IoHomecontrolChannel last;last._channelIndex=15;module.mChannels[15]=&last;last.event=channel.event;
 messages.clear();assert(module.run("iohc event 16 status"));assert(messages.find("ch=16")!=std::string::npos);
 assert(module.run("iohc event 16 clear"));assert(!last.event.valid&&channel.event.valid);
 messages.clear();assert(module.run("iohc event 1 clear"));assert(!channel.event.valid&&channel.event.count==0);
 assert(messages.find("no RF")!=std::string::npos);
 assert(!module.run("iohc battery 1 status"));
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)
    (path/'console.cpp').write_text(harness+formatter+'\nbool IoHomecontrol::run(const std::string &iCmd,bool iDebugKo){'+command+'return false;}\n'+tests)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-DTEST_NATIVE',
                    '-I'+str(root/'src'),'-I'+str(root/'test'),str(path/'console.cpp'),
                    str(root/'src/protocol/IoHomeFrame.cpp'),str(root/'src/protocol/IoHomeCrypto.cpp'),
                    '-o',str(path/'console')],check=True)
    subprocess.run([str(path/'console')],check=True)
print('Production Last Device Event console: raw readback, age wrap, local-only access, bounds and clear passed')
