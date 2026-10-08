"""Exercise the production import verification branch with recording persistence/RF."""
from pathlib import Path
import tempfile, subprocess, os
root=Path(__file__).resolve().parents[1]
source=(root/'src/IoHomecontrol.cpp').read_text()
a=source.index('    if (mKeyImportPhase == KeyImportPhase::Verifying && !mController.isKeyExtractionActive())')
b=source.index('    if (mKeyImportPhase == KeyImportPhase::Scanning &&',a)
branch=source[a:b]
harness=r'''#include <cstdint>
#include <cstring>
#include <cassert>
#define logInfoP(...) ((void)0)
enum class ControllerState {Idle,DiscoverySending,DiscoveryListening};
struct Controller {
 uint32_t own=0x123456;uint8_t key[16]{};bool collision=false;ControllerState current=ControllerState::Idle;unsigned tx=0;
 uint32_t getOwnNodeId()const{return own;}bool twoWayIdentityCollision()const{return collision;}
 bool isKeyExtractionActive()const{return false;}ControllerState state()const{return current;}
 void observeForeignController(uint32_t peer){if(peer==own)collision=true;}
 void setSystemKey(const uint8_t*p){std::memcpy(key,p,16);}
 void startDiscovery(bool){++tx;current=ControllerState::DiscoverySending;}
};
struct {struct {void save(){}}flash;}openknx;
struct IoHomecontrol {
 enum class KeyImportPhase{Verifying,Failed,Scanning};KeyImportPhase mKeyImportPhase=KeyImportPhase::Verifying;
 Controller mController;struct {bool valid=true;uint8_t key[16]{7};}mKeyImportKey;
 uint32_t mKeyImportHubNodeId=0xE2D1FF,mKeyImportExtractionNodeId=0xDEADBE;
 bool mKeyImportBroadcastComplete=true,mKeyImportDirectedAwaiting=true,mKeyImportDirectedTriedGeneric=true;
 unsigned mKeyImportDirectedCandidateIndex=9,mKeyImportDirectedNodeId=9;
 bool durable=true;unsigned commits=0;uint32_t storedNode=0;uint8_t storedKey=0;
 bool prepareTwoWayPersistence(){++commits;assert(mController.tx==0);storedNode=mController.own;storedKey=mController.key[0];return durable;}
 void processKeyImportWorkflow();
};
'''
tests=r'''int main(){
 IoHomecontrol m;m.processKeyImportWorkflow();assert(m.mController.own==0x123456);assert(m.storedNode==0x123456&&m.storedKey==7&&m.commits==1&&m.mController.tx==1);
 IoHomecontrol retry;retry.mController.own=m.storedNode;retry.mKeyImportHubNodeId=0xAABCDE;retry.processKeyImportWorkflow();assert(retry.storedNode==m.storedNode);
 IoHomecontrol legacy;legacy.mController.own=legacy.mKeyImportHubNodeId;legacy.processKeyImportWorkflow();assert(legacy.mController.collision&&legacy.mController.tx==0&&legacy.commits==1);assert(legacy.mKeyImportPhase==IoHomecontrol::KeyImportPhase::Failed);
 IoHomecontrol failure;failure.durable=false;failure.processKeyImportWorkflow();assert(failure.mController.tx==0&&failure.mKeyImportPhase==IoHomecontrol::KeyImportPhase::Failed);
 IoHomecontrol missing;missing.mKeyImportKey.valid=false;missing.processKeyImportWorkflow();assert(missing.commits==0&&missing.mController.tx==0);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'import.cpp';p.write_text(harness+'void IoHomecontrol::processKeyImportWorkflow(){'+branch+'}'+tests)
 binary=Path(d)/'import'
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17',str(p),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('Production key-import branch preserves own NID, commits key before TX and rejects collision/storage failure')
