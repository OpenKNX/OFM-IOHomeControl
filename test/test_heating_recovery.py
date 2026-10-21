"""Run production receipt recovery with real checked native receipt storage."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/IoHomecontrol.cpp').read_text()
a=source.index('void IoHomecontrol::restoreAssignmentReceipts()');b=source.index('{',a)+1;depth=1
while depth:
    depth+=(source[b]=='{')-(source[b]=='}');b+=1
method=source[a:b]
harness=r'''
#include "protocol/IoHomeAssignmentReceipt.h"
#include "protocol/IoHomeHeatingRegisters.h"
#include <cassert>
#define logInfoP(...) ((void)0)
struct IoHomecontrolChannel {
 uint32_t node=0;uint8_t key[16]{};bool oneWay=false;IoHomeProtocolIdentity identity;
 bool is1W()const{return oneWay;}uint32_t getNodeId()const{return node;}
 const uint8_t *getEncryptionKey()const{return key;}
 bool hasProtocolIdentity()const{return identity.valid;}
 void setNodeId(uint32_t v){node=v;}void setEncryptionKey(const uint8_t *v){std::memcpy(key,v,16);}
 void onProtocolIdentity(uint32_t,const IoHomeProtocolIdentity &v){identity=v;}
};
struct IoHomecontrol {
 uint8_t mNumChannels=1;IoHomecontrolChannel *mChannels[16]{};
 bool mNetworkStoreReady=true,mNetworkStoreFailed=false,mNetworkHasCommit=true;
 IoHomeAssignmentReceipt mAssignmentReceipts;
 struct {struct Binding {bool managed=false;uint32_t node=0;uint8_t key[16]{};}channels[16];}mCommittedNetwork;
 void restoreAssignmentReceipts();
};
'''
tests=r'''
int main(){
 IoHomecontrol module;IoHomecontrolChannel channel;module.mChannels[0]=&channel;
 const uint8_t key[16]={0x42};IoHomeProtocolIdentity id;id.valid=true;id.profile=52;id.subProfile=1;
 id.manufacturerId=12;id.powerSaveModeRaw=IOHC_POWER_SAVE_ALWAYS_ALIVE;
 assert(module.mAssignmentReceipts.prepare(0,0xB6BDFC,key,id));
 auto &binding=module.mCommittedNetwork.channels[0];binding.managed=true;binding.node=0xB6BDFC;std::memcpy(binding.key,key,16);
 // Network journal has restored node/key; the ordinary application still needs
 // the separately checked metadata receipt after a crash before flash.save.
 channel.node=binding.node;std::memcpy(channel.key,key,16);module.restoreAssignmentReceipts();
 assert(IoHomeHeating::actuator(channel.identity));assert(channel.identity.ioAddress==binding.node);
 assert(channel.identity.powerSaveMode==IoHomePowerMode::AlwaysAlive&&!channel.identity.fullMetadata);
 channel.identity.fullMetadata=true;module.restoreAssignmentReceipts();assert(channel.identity.fullMetadata);
 channel.identity={};binding.key[0]^=1;module.restoreAssignmentReceipts();assert(!channel.identity.valid);
 binding.key[0]^=1;module.mNetworkStoreFailed=true;module.restoreAssignmentReceipts();assert(!channel.identity.valid);
 module.mNetworkStoreFailed=false;module.mNetworkHasCommit=false;channel.node=0;
 module.restoreAssignmentReceipts();assert(channel.node==0&&!channel.identity.valid); // orphan receipt cannot pair
 module.mNetworkHasCommit=true;channel.oneWay=true;module.restoreAssignmentReceipts();assert(channel.node==0);
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'recovery.cpp';p.write_text(harness+method+tests);binary=Path(d)/'recovery'
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-DTEST_NATIVE','-I'+str(root/'src'),str(p),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Production recovery retains heater node/key/profile/power and rejects orphan/conflicting receipts')
