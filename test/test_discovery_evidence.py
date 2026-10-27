"""Deterministic sanitized three-shutter replay of production evidence accounting."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
code=r'''#include "protocol/IoHomeDiscoveryEvidence.h"
#include <cassert>
int main(){
 IoHomeDiscoveryEvidence e;e.reset();assert(e.session==1);
 auto *p=e.touch(0x155D81,10);p->flags=IoHomeDiscoveryEvidence::Passive2f|IoHomeDiscoveryEvidence::PassiveChallenge|IoHomeDiscoveryEvidence::PassiveAuth|IoHomeDiscoveryEvidence::Persisted;p->channel=2;
 e.active(0x562292,20);e.active(0xE50470,30);e.active(0xE50470,40);
 assert(e.activePackets==3&&e.unique(IoHomeDiscoveryEvidence::Active2b)==2&&e.duplicates==1);
 assert(!(e.find(0x155D81)->flags&IoHomeDiscoveryEvidence::Active2b));
 assert(e.unique(IoHomeDiscoveryEvidence::PassiveAuth)==1&&e.unique(IoHomeDiscoveryEvidence::Persisted)==1);
 assert(e.find(0xE50470)->firstSeen==30&&e.find(0xE50470)->lastSeen==40);
 p=e.find(0x155D81);p->failure=IoHomeDiscoveryEvidence::Failure::NoReply;assert(p->flags&IoHomeDiscoveryEvidence::PassiveAuth);
 p->flags|=IoHomeDiscoveryEvidence::Directed2f|IoHomeDiscoveryEvidence::DirectedAuth;assert(e.unique(IoHomeDiscoveryEvidence::DirectedAuth)==1);
 for(unsigned i=0;i<20;i++)e.touch(0x100000+i,100);assert(e.count==e.Capacity&&e.overflow==15);
 e.reset();e.standard(0xE50470,1);e.standard(0x562292,2);e.standard(0xE50470,3);
 assert(e.standardPackets==3&&e.duplicates==1&&e.unique(IoHomeDiscoveryEvidence::Active2b)==0&&e.find(0xE50470)->active29);
 e.reset();assert(e.session==3&&e.count==0&&e.activePackets==0&&e.overflow==0);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'ledger.cpp';p.write_text(code);b=Path(d)/'ledger'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I'+str(root/'src'),str(p),'-o',str(b)],check=True)
 subprocess.run([str(b)],check=True)
print('Three-device provenance, duplicate accounting, directed timeout/success and bounded overflow passed')

source=(root/'src/IoHomecontrol.cpp').read_text()
a=source.index('void IoHomecontrol::onDiagnosticFrame(');b=source.index('void IoHomecontrol::onDiagnosticConfig(',a)
production=source[a:b]
harness=r'''#include "protocol/IoHomeDiscoveryEvidence.h"
#include <cassert>
constexpr unsigned IOHC_CTRL0_MODE_1W=128;
enum class IoHomeCommand {Discover2EResponse=0x2F,ChallengeRequest=0x3C,StatusUpdate=0x71,DiscoverSPEResponse=0x2B};
struct IoHomeFrame {unsigned ctrlByte0=0,src=0,dst=0;IoHomeCommand commandId=IoHomeCommand::Discover2EResponse;
 unsigned getSrcNodeId()const{return src;}unsigned getDestNodeId()const{return dst;}};
struct IoHomecontrol {
 IoHomeDiscoveryEvidence mDiscoveryEvidence;unsigned mKeyImportHubNodeId=0xE2D1FF,mDiagnosticVerifyNode=0x155D81;
 bool mDiagnosticScan=false;enum class KeyImportPhase{Idle,Scanning};KeyImportPhase mKeyImportPhase=KeyImportPhase::Idle;
 struct {unsigned getOwnNodeId()const{return 0xE4E61D;}}mController;
 void onDiagnosticFrame(const IoHomeFrame &,uint8_t,int16_t,uint32_t);
};
'''
checks=r'''int main(){IoHomecontrol m;m.mDiscoveryEvidence.originalHub=0xE2D1FF;auto *p=m.mDiscoveryEvidence.touch(0x155D81,1);
 p->flags=IoHomeDiscoveryEvidence::PassiveAuth|IoHomeDiscoveryEvidence::Persisted;
 IoHomeFrame f;f.src=0x155D81;f.dst=0xE2D1FF;m.onDiagnosticFrame(f,2,-73,100);
 assert(p->flags&IoHomeDiscoveryEvidence::Passive2f);assert(!(p->flags&IoHomeDiscoveryEvidence::Directed2f));
 f.dst=0xE4E61D;m.onDiagnosticFrame(f,0,-72,200);
 assert(p->flags&IoHomeDiscoveryEvidence::Directed2f);assert(!(p->flags&IoHomeDiscoveryEvidence::DirectedAuth));
 assert(!(p->flags&IoHomeDiscoveryEvidence::Active2b));assert(p->src==0x155D81&&p->dst==0xE4E61D&&p->rssi==-72&&p->lastSeen==200);
 f.ctrlByte0=128;m.onDiagnosticFrame(f,1,-10,300);assert(p->lastSeen==200);
}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'production.cpp';p.write_text(harness+production+checks);b=Path(d)/'production'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I'+str(root/'src'),str(p),'-o',str(b)],check=True);subprocess.run([str(b)],check=True)
print('Production observation hook keeps foreign hub, directed response and authentication proof independent')

# Finalizing an import must not erase the only diagnostic record of passive nodes.
reset=source[source.index('void IoHomecontrol::resetKeyImportWorkflow()'):source.index('IoHomecontrol::KeyImportDevice *IoHomecontrol::findKeyImportDevice')]
assert 'mDiscoveryEvidence.reset' not in reset
assert 'mDiscoveryEvidence.reset();\n            mKeyImportPhase = KeyImportPhase::Extracting' in source
