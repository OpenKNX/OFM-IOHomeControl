#pragma once
#include "IoHomeProductRuntime.h"
#include "IoHomeProductModes.h"
#include "IoHomeProductSpecific.h"
// Explicit diagnostic selection never identifies the commercial device variant.
enum class IoHomeDiagnosticProduct:uint8_t {None,HeatPump,HeatingInterface,GenericHeater,AtlanticHeater,AtlanticDhwV2,AtlanticDhwCentikelvin,Siren,HeatPumpModes,AtlanticDhwModes,Pergola};
enum class IoHomeValueUnit:uint8_t {Unknown,Celsius,Numeric,Packed,Percent};
struct IoHomeDecodedValue {uint16_t raw=0;double value=0;uint32_t generation=0,ageMs=0;IoHomeProductRuntime::Trust trust=IoHomeProductRuntime::Trust::None;IoHomeValueUnit unit=IoHomeValueUnit::Unknown;bool present=false,fresh=false,known=false;};
inline IoHomeDecodedValue ioHomeDecodeSelectedProductValue(const IoHomeProductRuntime &runtime,IoHomeDiagnosticProduct product,uint8_t index,uint32_t now,const IoHomeTemperatureContext &supplied={}) {
 IoHomeDecodedValue out;const auto *s=runtime.sample(index);if(!s)return out;
 out.raw=s->raw;out.generation=s->generation;out.present=s->present;out.trust=s->trust;out.ageMs=s->present?uint32_t(now-s->receivedMs):0;out.fresh=IoHomeProductRuntime::fresh(*s,now,5000);
 if(!out.fresh||s->trust==IoHomeProductRuntime::Trust::None)return out;
 if(product>=IoHomeDiagnosticProduct::HeatPump&&product<=IoHomeDiagnosticProduct::AtlanticDhwCentikelvin){
  const auto temp=IoHomeTemperatureProduct(uint8_t(product)-1);auto context=supplied;
  if(product==IoHomeDiagnosticProduct::GenericHeater&&index==13){const auto *comfort=runtime.sample(12);context.hasComfort=comfort->present&&comfort->generation==s->generation&&comfort->trust==s->trust&&IoHomeProductRuntime::fresh(*comfort,now,5000);context.comfortRaw=comfort->raw;}
  out.known=ioHomeDecodeProductTemperature(temp,index,s->raw,context,out.value);
  out.unit=product==IoHomeDiagnosticProduct::AtlanticHeater&&index==13?IoHomeValueUnit::Numeric:IoHomeValueUnit::Celsius;
 }else if(product==IoHomeDiagnosticProduct::Pergola){out.known=ioHomeDecodePergola(index,s->raw,out.value);out.unit=IoHomeValueUnit::Percent;
 }else if(product==IoHomeDiagnosticProduct::Siren&&index>=9&&index<=14){
  const uint8_t other=index%2?index+1:index-1;const auto *pair=runtime.sample(other);
  if(pair->present&&pair->generation==s->generation&&pair->trust==s->trust&&IoHomeProductRuntime::fresh(*pair,now,5000)){
   const auto value=ioHomeDecodeSirenSequence(index%2?s->raw:pair->raw,index%2?pair->raw:s->raw);
   out.known=value.knownVolume&&value.knownVisual;out.value=s->raw;out.unit=IoHomeValueUnit::Packed;
  }
 }else if((product==IoHomeDiagnosticProduct::HeatPumpModes||product==IoHomeDiagnosticProduct::AtlanticDhwModes)&&(index==15||index==16)){
  const auto *pair=runtime.sample(index==15?16:15);
  if(pair->present&&pair->generation==s->generation&&pair->trust==s->trust&&IoHomeProductRuntime::fresh(*pair,now,5000)) {out.known=true;out.value=s->raw;out.unit=IoHomeValueUnit::Packed;} // keep uninterpreted bits, no scalar mode invention
 }
 return out;
}
