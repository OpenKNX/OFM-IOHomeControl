#pragma once
#include "IoHomeCommands.h"
#include <cmath>
#include <cstdint>
#include <cstring>

// Atlantic ECL private registers, not the generic MP/FP or ObjectsArray API.
// Source: Laberning 4824cf65/c1d57978 AGILIA captures and Velocet ECL classes.
namespace IoHomeHeating {
enum class Encoding : uint8_t {Byte,Boolean,Mode,Regulator,Tenths,SensorHundredths,Presence,Raw};
struct Descriptor {uint16_t reg; const char *name; Encoding encoding; uint8_t width; bool writable;};
inline constexpr Descriptor registers[]={
 {0x0100,"mode",Encoding::Mode,1,true},
 {0x0102,"max_setpoint",Encoding::Tenths,2,false},
 {0x0103,"setpoint",Encoding::Tenths,2,true},
 {0x0106,"open_window_detection",Encoding::Boolean,1,true},
 {0x010E,"window_sensor",Encoding::Boolean,1,false},
 {0x0115,"room_temperature",Encoding::SensorHundredths,2,false},
 {0x0117,"regulator",Encoding::Regulator,1,false},
 {0x0101,"occupancy_raw",Encoding::Raw,0,false},
 {0x0110,"home_away_command",Encoding::Presence,1,true},
 {0x012C,"power_on_list_raw",Encoding::Raw,0,false},
 {0x012D,"controller_address_raw",Encoding::Raw,0,false},
 {0x0130,"setpoint_block_raw",Encoding::Raw,0,false}
};
constexpr uint8_t count=sizeof(registers)/sizeof(registers[0]);
inline int index(uint16_t reg){for(uint8_t i=0;i<count;++i)if(registers[i].reg==reg)return i;return -1;}
inline const Descriptor *descriptor(uint16_t reg){
 static constexpr Descriptor effective{0x012A,"effective_setpoint_raw",Encoding::Raw,0,false};
 if(reg==0x012A)return &effective;const int i=index(reg);return i<0?nullptr:&registers[i];
}
inline bool actuator(const IoHomeProtocolIdentity &id){
 return id.valid&&id.manufacturerId==0x0C&&id.profile==52&&id.subProfile==1;
}
inline bool pairingNode(const IoHomeProtocolIdentity &id){
 return id.valid&&id.manufacturerId==0x0C&&id.profile==1023&&id.subProfile==0;
}
inline bool nextNode(uint32_t pairing,uint32_t &node){
 if(!pairing||pairing>=0xFFFFFF)return false;node=pairing+1;return true;
}
// Put naturally aligned fields first: this cache is repeated for every register
// and channel. Keep all 23 raw bytes; avoid four bytes of padding per value.
struct Value {float value=0;uint16_t raw=0;uint8_t bytes[23]{},length=0;bool valid=false,sensorFault=false;};
static_assert(sizeof(Value)==32,"Heating values must fit the ESP32 static RAM budget");
inline bool decodeValue(const Descriptor &d,const uint8_t *data,uint8_t length,Value &out){
 if(!data||!length||length>sizeof(out.bytes)||(d.width&&length!=d.width&&d.encoding!=Encoding::Presence))return false;
 Value v;v.length=length;std::memcpy(v.bytes,data,length);v.raw=data[0]|(length>1?uint16_t(data[1])<<8:0);
 v.valid=true;v.value=v.raw;
 switch(d.encoding){
 case Encoding::Tenths:if(v.raw<70||v.raw>280)return false;v.value=v.raw/10.0f;break;
 case Encoding::SensorHundredths:
  if(v.raw==0xFFFF){v.valid=false;v.sensorFault=true;break;}
  v.value=static_cast<int16_t>(v.raw)/100.0f;
  if(v.value < -40 || v.value > 80)return false;break;
 case Encoding::Boolean:if(v.raw>1)return false;break;
 case Encoding::Presence:v.valid=length==1&&v.raw<=1;break;
 case Encoding::Mode:if(v.raw>4)return false;break;
 case Encoding::Regulator:if(v.raw>2)return false;break;
 case Encoding::Raw:v.valid=false;break; // raw diagnostic, no fabricated units
 default:break;
 }
 out=v;return true;
}
inline uint8_t build(uint16_t reg,bool write,float value,uint8_t *data){
 const auto *d=descriptor(reg);if(!d||!data||(write&&!d->writable))return 0;
 data[0]=0x0C;data[1]=write?0x61:0x60;data[2]=uint8_t(reg>>8);data[3]=uint8_t(reg);
 if(!write)return 4;
 if(!std::isfinite(value))return 0;
 float scaled=d->encoding==Encoding::Tenths?value*10.0f:value;
 if(scaled<0||scaled>65535)return 0;
 const uint16_t raw=uint16_t(std::lround(scaled));
 if(d->encoding!=Encoding::Tenths && scaled!=raw)return 0;
 if(d->encoding==Encoding::Presence && raw>1)return 0;
 if(d->encoding==Encoding::Mode && raw==3)return 0; // readable, unqualified writer
 data[4]=uint8_t(raw);data[5]=uint8_t(raw>>8);Value decoded;
 return decodeValue(*d,data+4,d->width,decoded)?4+d->width:0;
}
inline bool decodeReply(const uint8_t *request,uint8_t requestLength,const uint8_t *data,uint8_t length,Value &value){
 if(!request||requestLength<4||!data||length<4||std::memcmp(request,data,4))return false;
 const uint16_t reg=uint16_t(request[2])<<8|request[3];const auto *d=descriptor(reg);
 if(!d||request[0]!=0x0C||(request[1]!=0x60&&request[1]!=0x61))return false;
 if(request[1]==0x61 && (length!=requestLength||std::memcmp(request,data,length)))return false;
 return decodeValue(*d,data+4,length-4,value);
}
struct Observation {Value decoded;uint32_t timestampMs=0,token=0;bool observed=false;};
// An acknowledged Home/Away command is not an occupancy sensor or thermal effect.
enum class PresenceResult:uint8_t {Unknown,Queued,RfSent,AcknowledgedEffectUnknown,Rejected,Timeout,QuotaBlocked,IdentityChanged,ReadbackVerifiedEffectUnknown,Invalid,Cancelled,QualificationBlocked,ReadbackDifferent,BusyBlocked,QueueBlocked};
inline const char *presenceResultName(PresenceResult r){
 switch(r){case PresenceResult::Unknown:return "unknown";case PresenceResult::Queued:return "queued";
 case PresenceResult::RfSent:return "rf_sent";case PresenceResult::AcknowledgedEffectUnknown:return "acknowledged_effect_not_observed";
 case PresenceResult::Rejected:return "rejected";case PresenceResult::Timeout:return "timeout";
 case PresenceResult::QuotaBlocked:return "quota_blocked";case PresenceResult::IdentityChanged:return "identity_changed";
 case PresenceResult::ReadbackVerifiedEffectUnknown:return "register_readback_agrees_effect_not_observed";
 case PresenceResult::Invalid:return "invalid_response";case PresenceResult::Cancelled:return "cancelled";
 case PresenceResult::ReadbackDifferent:return "register_readback_differs";case PresenceResult::BusyBlocked:return "busy_blocked";case PresenceResult::QueueBlocked:return "queue_blocked";default:return "agilia_qualification_required";}
}
struct State {
 enum class Result:uint8_t {Unknown,Queued,ReadbackPending,Observed,SensorFault,Rejected,Timeout,QuotaDenied,IdentityChanged,Invalid,CommandAcknowledged};
 Observation values[count]{};Result result=Result::Unknown;
 uint32_t token=0,node=0,revision=0,gateway=0;uint8_t key[16]{};
 uint16_t pendingRegister=0;bool busy=false;
 uint32_t nextPollMs=0,pollIntervalMs=0;uint8_t pollIndex=0,refreshRemaining=0;bool initialRead=false;PresenceResult presenceResult=PresenceResult::Unknown;
 void invalidate(){*this=State();}
};
static_assert(sizeof(State)==580,"Reuse the existing presence slot; no per-channel RAM growth");
inline const char *resultName(State::Result result){
 switch(result){case State::Result::CommandAcknowledged:return "command_acknowledged_effect_unobserved";case State::Result::Queued:return "queued";case State::Result::ReadbackPending:return "readback_pending";
 case State::Result::Observed:return "observed";case State::Result::SensorFault:return "sensor_fault";
 case State::Result::Rejected:return "peer_rejected";case State::Result::Timeout:return "auth_or_response_timeout";
 case State::Result::QuotaDenied:return "quota_denied";case State::Result::IdentityChanged:return "identity_changed";
 case State::Result::Invalid:return "readback_invalid";default:return "unknown";}
}
}
