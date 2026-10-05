#pragma once
#include <stdint.h>
#include <cstring>
// Legacy RCM lifecycle model, distinct from master discovery. Does not enqueue
// the prerequisite variants or claim that a real peer entered configuration.
class IoHomeReceiveConfiguration {
public:
 enum class Stage:uint8_t {Idle,Prerequisite,Base,Temporary,Done,Cancelled,Timeout,IdentityChanged};
 bool begin(uint16_t matchingMarkedNodes,uint32_t peer,const uint8_t *key,uint32_t revision,uint32_t now){
  if(active()||!peer||peer>0xFFFFFF||!key||!revision)return false;
  mPeer=peer;mRevision=revision;std::memcpy(mKey,key,16);mVariant=matchingMarkedNodes==1?0:3;mInnerArmed=false;mStarted=now;
  mStage=matchingMarkedNodes?Stage::Prerequisite:Stage::Base;return true;
 }
 bool prerequisite(uint32_t peer,const uint8_t *key,uint32_t revision,uint8_t actualCommand,uint32_t now){
  if(!bound(peer,key,revision)||mStage!=Stage::Prerequisite||actualCommand!=0x37)return false;mStage=Stage::Base;mStarted=now;return true;
 }
 bool event10(uint32_t now){if(mStage==Stage::Base){mStage=Stage::Temporary;mInnerStarted=now;mInnerArmed=true;return true;}if(mStage==Stage::Temporary){mInnerArmed=false;return true;}return false;}
 bool bound(uint32_t peer,const uint8_t *key,uint32_t revision){bool same=peer==mPeer&&key&&revision==mRevision&&!std::memcmp(key,mKey,16);if(!same&&active())mStage=Stage::IdentityChanged;return same;}
 void tick(uint32_t now){
  if(mStage==Stage::Prerequisite&&uint32_t(now-mStarted)>=5000)mStage=Stage::Timeout; // chosen host model guard
  else if(mStage==Stage::Base||mStage==Stage::Temporary){if(uint32_t(now-mStarted)>=600000)mStage=Stage::Done;else if(mStage==Stage::Temporary&&mInnerArmed&&uint32_t(now-mInnerStarted)>=300000){mStage=Stage::Base;mInnerArmed=false;}}
 }
 bool cancel(){if(!active())return false;mStage=Stage::Cancelled;return true;}
 bool active()const{return mStage==Stage::Prerequisite||mStage==Stage::Base||mStage==Stage::Temporary;}
 Stage stage()const{return mStage;}uint8_t prerequisiteVariant()const{return mVariant;}
private:
 Stage mStage=Stage::Idle;uint32_t mPeer=0,mRevision=0,mStarted=0,mInnerStarted=0;uint8_t mKey[16]{},mVariant=0;bool mInnerArmed=false;
};
