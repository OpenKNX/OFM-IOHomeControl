#pragma once
#include "IoHomeCheckedJournal.h"
#include "IoHomeCommissioningJob.h"
// Eight key-free terminal/project events. Interrupted RF jobs are never replayed.
class IoHomeCommissioningHistory {
public:
 struct Entry {uint32_t boot=0,generation=0,node=0;uint8_t owner=0,stage=0,error=0,channel=255,projectApplied=0;};
 static constexpr unsigned Capacity=8,PayloadSize=2+Capacity*16;
 using Journal=IoHomeCheckedJournal<PayloadSize>;
 Journal journal{"iohclog"};
 bool append(const Entry &e){
  if(!e.boot||!e.generation||e.node>0xFFFFFF||e.owner<1||e.owner>4||e.stage<6||e.stage>9||e.error>4||(e.channel>=16&&e.channel!=255)||e.projectApplied>1)return false;
  uint8_t data[PayloadSize]{};auto result=journal.load(data);
  if(result==Journal::Result::Corrupt||result==Journal::Result::Unavailable||!valid(data))return false;
  uint8_t *p=data+2+data[1]*16;put32(p,e.boot);put32(p+4,e.generation);p[8]=e.node>>16;p[9]=e.node>>8;p[10]=e.node;
  p[11]=e.owner;p[12]=e.stage;p[13]=e.error;p[14]=e.channel;p[15]=e.projectApplied;
  if(data[0]<Capacity)data[0]++;data[1]=(data[1]+1)%Capacity;return journal.commit(data);
 }
 bool read(uint8_t index,Entry &out,uint8_t &count){
  uint8_t data[PayloadSize]{};auto result=journal.load(data);count=0;
  if(result!=Journal::Result::Found||!valid(data))return false;count=data[0];if(index>=count)return false;
  const uint8_t *p=data+2+((data[1]+Capacity-1-index)%Capacity)*16;
  Entry e;e.boot=get32(p);e.generation=get32(p+4);e.node=uint32_t(p[8])<<16|uint32_t(p[9])<<8|p[10];e.owner=p[11];e.stage=p[12];e.error=p[13];e.channel=p[14];e.projectApplied=p[15];out=e;return true;
 }
private:
 static bool valid(const uint8_t *p){if(p[0]>Capacity||p[1]>=Capacity||(p[0]<Capacity&&p[0]!=p[1]))return false;for(unsigned i=0;i<p[0];i++){const uint8_t *q=p+2+i*16;if(!get32(q)||!get32(q+4)||q[11]<1||q[11]>4||q[12]<6||q[12]>9||q[13]>4||(q[14]>=16&&q[14]!=255)||q[15]>1)return false;}return true;}
 static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(24-i*8);}
 static uint32_t get32(const uint8_t *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
};
