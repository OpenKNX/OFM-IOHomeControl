#pragma once
#include "IoHomeObjectTransfer.h"
// Offline transport preparation only. No writable schema has been qualified.
// The RF queue must continue to reject these prepared operations.
class IoHomeObjectWriteSession {
public:
 static constexpr bool rfWriteQualified=false;
 // Explicit empty production allowlist. Transport knowledge cannot authorize a schema.
 static constexpr bool allowed(uint8_t provider,uint16_t object){
  (void)provider;(void)object;return false;
 }
 bool prepare(uint32_t peer,const uint8_t *key,uint32_t revision,uint32_t token,uint8_t provider,uint16_t object,uint16_t offset,const uint8_t *payload,uint16_t size,uint32_t now,uint32_t budget){
  if(!key||!payload||!size||size>IoHomeObjectTransfer::Capacity||!revision||mTransfer.active())return false;
  if(!mTransfer.begin(IoHomeObjectTransfer::Direction::Write,peer,token,provider,object,offset,size,now,budget))return false;
  mPeer=peer;mToken=token;mRevision=revision;mSize=size;std::memcpy(mKey,key,16);std::memcpy(mPayload,payload,size);return true;
 }
 bool bound(uint32_t peer,const uint8_t *key,uint32_t revision){const bool same=key&&peer==mPeer&&revision==mRevision&&!std::memcmp(key,mKey,16);if(!same)mTransfer.fail(true);return same;}
 bool openingReply(uint32_t peer,const uint8_t *key,uint32_t revision,uint32_t token,const uint8_t *data,uint8_t size){
  return bound(peer,key,revision)&&mTransfer.acceptOpening(peer,token,0x49,data,size);
 }
 bool nextMode1(){
  if(mTransfer.stage()!=IoHomeObjectTransfer::Stage::Ready)return false;
  const uint16_t remaining=mTransfer.negotiated()-mTransfer.transferred();const uint8_t bytes=remaining>18?18:remaining;
  mWord=(remaining+17)/18;return mTransfer.next(mWord,mPayload+mTransfer.transferred(),bytes);
 }
 bool chunkReply(uint32_t peer,const uint8_t *key,uint32_t revision,uint32_t token,const uint8_t *data,uint8_t size){
  if(!bound(peer,key,revision)||!data||size!=3)return false;
  const uint16_t word=uint16_t(data[1])<<8|data[2];if(word&&word!=mWord)return false;
  return mTransfer.acceptChunk(peer,token,data,size);
 }
 void tick(uint32_t now){mTransfer.tick(now);}
 bool cancel(uint32_t token){return mTransfer.cancel(token);}
 const IoHomeObjectTransfer &transport()const{return mTransfer;}
private:
 IoHomeObjectTransfer mTransfer;uint32_t mPeer=0,mToken=0,mRevision=0;uint16_t mSize=0,mWord=0;uint8_t mKey[16]{},mPayload[IoHomeObjectTransfer::Capacity]{};
};
