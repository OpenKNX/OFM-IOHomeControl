#pragma once
#include "IoHomeCheckedJournal.h"
#include "IoHomeCommands.h"
#include <cstdio>
// Private, identity/key-bound metadata authority; no freshness survives restore.
class IoHomeMetadataStore {
public:
 static constexpr unsigned PayloadSize=256;
 struct Snapshot {uint32_t node=0;uint8_t key[16]{};IoHomeProtocolIdentity identity;IoHomeProductIdentityEvidence evidence;};
 using Journal=IoHomeCheckedJournal<PayloadSize>;
 Journal journal{"iohcmeta0"};
 explicit IoHomeMetadataStore(unsigned channel=0){char name[16];std::snprintf(name,sizeof(name),"iohcmeta%u",channel);journal.setNamespace(name);}
 bool representation(const Snapshot &s,uint8_t *data){if(!data)return false;std::memset(data,0,PayloadSize);return encode(s,data);}
 bool save(const Snapshot &s){uint8_t data[PayloadSize]{};return representation(s,data)&&journal.commit(data);}
 bool load(uint32_t node,const uint8_t *key,Snapshot &out){
  uint8_t data[PayloadSize]{};if(!key||journal.load(data)!=Journal::Result::Found||getNode(data)!=node||std::memcmp(data+3,key,16))return false;
  Snapshot s;s.node=node;std::memcpy(s.key,key,16);unsigned c=19;uint8_t n=data[c++];if(n<2||n>23)return false;
  s.identity=decodeProtocolIdentity(data+c,n);c+=23;uint8_t flags=data[c++];if(flags&~15)return false;
  s.identity.hasIoBackboneAddress=flags&1;s.identity.hasMib=flags&2;s.identity.hasDiscoveryTimestamp=flags&4;s.identity.fullMetadata=flags&8;
  if(data[c]>5)return false;s.identity.nodeClass=IoHomeNodeClass(data[c++]);uint8_t keyState=data[c++];if(keyState>2&&keyState!=255)return false;s.identity.keyState=IoHomeKeyState(keyState);
  if(data[c]>3||data[c+1]>3)return false;c+=2;s.identity.metadataSource=IoHomeMetadataSource::Restored;s.identity.keyStateSource=IoHomeMetadataSource::Restored;s.identity.ioAddress=node;
  n=data[c++];if(n>23)return false;s.identity.rawDataLen=n;std::memcpy(s.identity.rawData,data+c,n);c+=23;
  uint8_t *arrays[]={s.evidence.nameResponse,s.evidence.generalInfo1,s.evidence.generalInfo2,s.evidence.generalInfo3,s.evidence.generalInfo3ErrorResponse};
  uint8_t *lengths[]={&s.evidence.nameResponseLen,&s.evidence.generalInfo1Len,&s.evidence.generalInfo2Len,&s.evidence.generalInfo3Len,&s.evidence.generalInfo3ErrorResponseLen};
  for(unsigned i=0;i<5;i++){n=data[c++];if(n>23)return false;*lengths[i]=n;std::memcpy(arrays[i],data+c,n);c+=23;}
  if(data[c]>5)return false;s.evidence.generalInfo3Outcome=IoHomeGeneralInfo3Outcome(data[c++]);
  s.evidence.manufacturerSubType=uint16_t(data[c])<<8|data[c+1];c+=2;std::memcpy(s.evidence.productFamilyLabel,data+c,48);c+=48;if(s.evidence.productFamilyLabel[47])return false;
  s.evidence.productQuirkFlags=uint16_t(data[c])<<8|data[c+1];c+=2;if(data[c]>1)return false;s.evidence.manufacturerSignatureInconsistent=data[c++];s.evidence.signatureManufacturerId=data[c++];if(data[c]>3)return false;s.evidence.identificationConfidence=IoHomeIdentificationConfidence(data[c++]);
  for(;c<PayloadSize;c++)if(data[c])return false;out=s;return true;
 }
private:
 static uint32_t getNode(const uint8_t *p){return uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];}
 static bool encode(const Snapshot &s,uint8_t *p){
  if(!s.node||s.node>0xFFFFFF||!s.identity.valid||s.identity.ioAddress!=s.node||s.identity.rawDataLen>23||uint8_t(s.identity.nodeClass)>5||uint8_t(s.identity.metadataSource)>3||uint8_t(s.identity.keyStateSource)>3||(uint8_t(s.identity.keyState)>2&&s.identity.keyState!=IoHomeKeyState::Unknown))return false;
  p[0]=s.node>>16;p[1]=s.node>>8;p[2]=s.node;std::memcpy(p+3,s.key,16);unsigned c=19;uint8_t n=encodeProtocolIdentity(s.identity,p+c+1,23);if(!n)return false;p[c]=n;c+=24;
  p[c++]=(s.identity.hasIoBackboneAddress?1:0)|(s.identity.hasMib?2:0)|(s.identity.hasDiscoveryTimestamp?4:0)|(s.identity.fullMetadata?8:0);
  p[c++]=uint8_t(s.identity.nodeClass);p[c++]=uint8_t(s.identity.keyState);p[c++]=uint8_t(s.identity.metadataSource);p[c++]=uint8_t(s.identity.keyStateSource);
  p[c++]=s.identity.rawDataLen;std::memcpy(p+c,s.identity.rawData,s.identity.rawDataLen);c+=23;
  const uint8_t *arrays[]={s.evidence.nameResponse,s.evidence.generalInfo1,s.evidence.generalInfo2,s.evidence.generalInfo3,s.evidence.generalInfo3ErrorResponse};
  const uint8_t lengths[]={s.evidence.nameResponseLen,s.evidence.generalInfo1Len,s.evidence.generalInfo2Len,s.evidence.generalInfo3Len,s.evidence.generalInfo3ErrorResponseLen};
  for(unsigned i=0;i<5;i++){if(lengths[i]>23)return false;p[c++]=lengths[i];std::memcpy(p+c,arrays[i],lengths[i]);c+=23;}
  if(uint8_t(s.evidence.generalInfo3Outcome)>5||uint8_t(s.evidence.identificationConfidence)>3||s.evidence.productFamilyLabel[47])return false;
  p[c++]=uint8_t(s.evidence.generalInfo3Outcome);p[c++]=s.evidence.manufacturerSubType>>8;p[c++]=s.evidence.manufacturerSubType;std::memcpy(p+c,s.evidence.productFamilyLabel,48);c+=48;
  p[c++]=s.evidence.productQuirkFlags>>8;p[c++]=s.evidence.productQuirkFlags;p[c++]=s.evidence.manufacturerSignatureInconsistent;p[c++]=s.evidence.signatureManufacturerId;p[c++]=uint8_t(s.evidence.identificationConfidence);return c<=PayloadSize;
 }
};
