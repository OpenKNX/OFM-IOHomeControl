"""Run the real console branch with recording RF/storage stubs; no key material logged."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1];source=(root/'src/IoHomecontrol.cpp').read_text()
def function(signature):
 a=source.index(signature);b=source.index('{',a);depth=1;e=b+1
 while depth:
  depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[a:e]
a=source.index('    if(lSub=="keyimport scan" ||');b=source.index('#if defined(IOHC_STANDALONE_DIAG)',a)
branch=source[a:b]
harness=r'''#include "protocol/IoHomeDiscoveryEvidence.h"
#include "protocol/IoHomeVerificationEvidence.h"
#include <cassert>
#include <cstring>
#include <string>
#define logInfoP(...) ((void)0)
unsigned millis(){return 100;}
enum class IoHomeAddressClass {Unicast,Other};
IoHomeAddressClass getAddressClass(unsigned n){return n>63&&n<=0xFFFFFF?IoHomeAddressClass::Unicast:IoHomeAddressClass::Other;}
bool keyHasNonZeroByte(const unsigned char*p){return p&&p[0];}
struct IoHomeNetworkStore {static bool equal(int a,int b){return a==b;}};
struct Channel {unsigned node=0x155D81;bool isPaired()const{return true;}bool is1W()const{return false;}unsigned getNodeId()const{return node;}};
struct Controller {
 bool idle=true,collision=false,queued=true,strict=false;unsigned verifies=0,scans=0;unsigned char key[16]{7};
 bool normal=true;
 bool debugRfLimitsBypassed()const{
#ifdef OPENKNX_DEBUG
 return !normal;
#else
 return false;
#endif
 }
 bool diagnosticRfAdmissionAllowed()const{return normal||debugRfLimitsBypassed();}void setVerificationBlocked(unsigned,IoHomeVerificationEvidence::Result){}void printVerificationEvidence(){}void printCollisionFirst(){}bool pauseBackgroundPolls(unsigned){return true;}
 bool idleForManagedOperation()const{return idle;}bool twoWayIdentityCollision()const{return collision;}
 const unsigned char *getSystemKey()const{return key;}unsigned getOwnNodeId()const{return 0xE4E61D;}
 void setReadOnlyDiagnosticRf(bool v){strict=v;}
 bool verifyKnownNetworkNode(unsigned n,const unsigned char *k,unsigned f){assert(n==0x155D81&&k==key&&f==255);verifies++;return queued;}
 void startDiscovery(bool spe){(void)spe;assert(strict);scans++;}
};
struct IoHomecontrol {
 Controller mController;IoHomeDiscoveryEvidence mDiscoveryEvidence;
 unsigned mDiagnosticVerifyNode=0,mKeyImportHubNodeId=0xE2D1FF,mKeyImportExtractionNodeId=0xDEADBE,mNumChannels=1;
 bool mDiagnosticScan=false,mNetworkStoreReady=true,mNetworkStoreFailed=false,mNetworkHasCommit=true,allowed=true;
 int mCommittedNetwork=1,snapshot=1;Channel channel;Channel *mChannels[1]{&channel};
 int networkSnapshot()const{return snapshot;}bool managementRequestsAllowed()const{return allowed;}
 void *findKeyImportDevice(unsigned){return nullptr;}void logDiagnosticHeader(){}void logDiagnosticSummary(){}
 bool command(std::string lSub);
};
'''
parsers=''.join(function(sig) for sig in ['bool isSpace(', 'std::string trimSpaces(', 'bool parseHex24('])
checks=r'''int main(){
 IoHomecontrol m;m.mDiscoveryEvidence.touch(0x155D81,1)->flags=IoHomeDiscoveryEvidence::PassiveAuth|IoHomeDiscoveryEvidence::Persisted;
 assert(m.command("keyimport verify 155D81"));assert(m.mController.verifies==1&&m.mController.strict&&m.mDiagnosticVerifyNode==0x155D81);
 assert(m.channel.node==0x155D81&&m.mController.key[0]==7);assert(m.mDiscoveryEvidence.find(0x155D81)->flags&IoHomeDiscoveryEvidence::PassiveAuth);
 for(unsigned mode=0;mode<6;mode++){
  IoHomecontrol f;if(mode==0)f.mController.idle=false;if(mode==1)f.mController.collision=true;if(mode==2)f.mNetworkHasCommit=false;
  if(mode==3)f.snapshot=2;if(mode==4)f.channel.node=0x562292;if(mode==5)f.allowed=false;
  assert(f.command("keyimport verify 155D81"));assert(!f.mController.verifies&&!f.mController.scans&&!f.mController.strict);
 }
 IoHomecontrol bypass;bypass.mController.normal=false;bypass.command("keyimport scan");
#ifdef OPENKNX_DEBUG
 assert(bypass.mController.scans==1);
#else
 assert(bypass.mController.scans==0);
#endif
 IoHomecontrol q;q.mController.queued=false;q.command("keyimport verify 155D81");assert(!q.mController.strict&&!q.mDiagnosticVerifyNode);
 IoHomecontrol scan;scan.command("keyimport scan");assert(scan.mController.scans==1&&scan.mDiagnosticScan&&scan.mController.strict);
 assert(scan.channel.node==0x155D81&&scan.mController.key[0]==7);
}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'console.cpp';p.write_text(harness+parsers+'bool IoHomecontrol::command(std::string lSub){'+branch+'return false;}'+checks);b=Path(d)/'console'
 for flags in [[],['-DOPENKNX_DEBUG']]:
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++17',*flags,'-I'+str(root/'src'),str(p),'-o',str(b)],check=True);subprocess.run([str(b)],check=True)
print('Production read-only verify accepts flash-known 155D81, preserves identity/evidence and rejects busy, collision, unknown and uncommitted state')
