#pragma once
#include <cstdint>
#include <cstring>

enum class IoHomeBatteryState:uint8_t {Unknown=0,Low=1,Normal=2,Full=3};
enum class IoHomeSomfyBatteryStatus:uint8_t {VeryLow=0,Low=1,Mid=2,High=3,Unknown=4};
enum class IoHomePairedBatteryState:uint8_t {Critical=0,Low=1,Medium=2,High=3,Unknown=4,NotSupported=5};
enum class IoHomeBatterySource:uint8_t {None,StatusExecuteBits,A601Status,BatteryError};
inline const char *ioHomeBatterySourceName(IoHomeBatterySource s){
 switch(s){case IoHomeBatterySource::StatusExecuteBits:return "StatusExecuteBits";case IoHomeBatterySource::A601Status:return "A601Status";case IoHomeBatterySource::BatteryError:return "BatteryError";default:return "unknown";}
}
inline IoHomeBatteryState ioHomeBatteryState(uint8_t status){return IoHomeBatteryState((status&0x60U)>>5U);}
struct IoHomeBatteryEvidence {bool valid=false,low=false;uint8_t raw=0,command=0;uint32_t timestampMs=0;};
struct IoHomeBatteryInfo {
 IoHomeBatteryEvidence coarse,somfy,error;
 // No numeric converter is qualified. Raw/categorical inputs cannot set this.
 bool percentValid=false;uint8_t percent=0;uint32_t percentTimestampMs=0;
 IoHomeBatterySource source()const{return somfy.valid?IoHomeBatterySource::A601Status:coarse.valid?IoHomeBatterySource::StatusExecuteBits:error.valid?IoHomeBatterySource::BatteryError:IoHomeBatterySource::None;}
 const IoHomeBatteryEvidence &selected()const{return somfy.valid?somfy:coarse.valid?coarse:error;}
 bool conflict()const{return somfy.valid&&coarse.valid&&somfy.low!=coarse.low;}
 void observeStatus(uint8_t status,uint8_t command,uint32_t now){
  const uint8_t state=uint8_t(ioHomeBatteryState(status));if(!state)return;
  coarse={true,state==1,state,command,now};
 }
 void observeSomfy(unsigned state,uint8_t command,uint32_t now){if(state>3)return;somfy={true,state<2,uint8_t(state),command,now};}
 void observeError(uint8_t code,uint8_t command,uint32_t now){if(code==0x12)error={true,true,code,command,now};}
};

// Views retain the complete field, including descriptor bytes. No units/scale
// or field-specific widths are inferred. Validation precedes all callbacks.
struct IoHomeBatteryField {uint8_t pid=0,record=0,format=0;const uint8_t *data=nullptr;unsigned length=0;};
template<class Visitor> bool ioHomeBatteryFields(const uint8_t *p,unsigned length,bool paired,Visitor visit){
 if(!p||!length||length>1024)return false;
 for(unsigned pass=0;pass<2;++pass){
  unsigned at=0;uint8_t record=0;uint8_t seen[32]{};bool any=false;
  while(at<length){
   if(paired&&p[at]==0xFF){if(!any||record==255||at+1==length)return false;++at;++record;std::memset(seen,0,sizeof(seen));any=false;continue;}
   IoHomeBatteryField field;field.pid=p[at++];field.record=record;
   const uint8_t bit=uint8_t(1U<<(field.pid&7));if(seen[field.pid>>3]&bit)return false;seen[field.pid>>3]|=bit;
   if(at>=length)return false;
   // A607 uses the simple lengthMinus1 grammar for all record parameters.
   if(!paired&&field.pid>127){field.format=p[at++];if(at>=length)return false;}
   unsigned count=p[at++];
   if(!paired&&count>127){if(at>=length)return false;const unsigned high=p[at++];count=high?((high<<7)|(count&127)):count;}
   ++count;if(count>length-at)return false;
   field.data=p+at;field.length=count;at+=count;any=true;
   if(pass)visit(field);
  }
 }
 return true;
}
inline bool ioHomeBatteryUnsigned(const uint8_t *p,unsigned n,uint32_t &out){
 if(!p||!n||n>4)return false;out=0;for(unsigned i=0;i<n;++i)out=(out<<8)|p[i];return true;
}
inline int32_t ioHomeBatterySigned(uint32_t value,unsigned n){
 if(n<4&&(value&(uint32_t(1)<<(n*8-1))))value|=0xFFFFFFFFU<<(n*8);return int32_t(value);
}
inline bool ioHomeBatteryObject(uint8_t provider,uint16_t object){
 return (provider==2&&(object==0xA601||object==0xA607||object==0xA60E))||(provider==0&&(object==9||object==0x4003));
}
