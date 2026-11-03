"""Execute actual production appliance control gate and device labels offline."""
from pathlib import Path
import os, subprocess, tempfile, json, re
root=Path(__file__).resolve().parents[1]
def function(text,needle):
    start=text.index(needle);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
source=(root/'src/IoHomecontrolChannel.cpp').read_text()
gate=function(source,'bool IoHomecontrolChannel::allowsActuatorControls() const')
label=function(source,'const char *iohcDeviceTypeLabel(')
harness=r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
#include "protocol/IoHomeCommands.h"
unsigned category=0;
#define ParamIOHC_cDeviceType category
struct IoHomecontrolChannel {
 IoHomeProtocolIdentity mProtocolIdentity;
 bool allowsActuatorControls() const;
};
'''
tests=r'''
int main(){
 assert(!std::strcmp(iohcDeviceTypeLabel(0x33),"domestic_hot_water"));
 assert(!std::strcmp(iohcDeviceTypeLabel(0x34),"electrical_heater"));
 assert(!std::strcmp(iohcDeviceTypeLabel(2),"roller_shutter"));
 assert(!std::strcmp(iohcDeviceTypeLabel(0x133),"unknown"));
 IoHomecontrolChannel channel;
 for(unsigned c=0;c<15;++c){
  category=c;
  for(uint16_t type:{uint16_t(0x33),uint16_t(0x34)})for(uint8_t subtype:{uint8_t(0),uint8_t(1),uint8_t(63)}){
   channel.mProtocolIdentity.valid=true;channel.mProtocolIdentity.profile=type;channel.mProtocolIdentity.subProfile=subtype;
   for(auto cls:{IoHomeNodeClass::Unknown,IoHomeNodeClass::Actuator,IoHomeNodeClass::Sensor}){
    channel.mProtocolIdentity.nodeClass=cls;assert(!channel.allowsActuatorControls());
   }
  }
  channel.mProtocolIdentity.profile=2;channel.mProtocolIdentity.nodeClass=IoHomeNodeClass::Actuator;
  assert(channel.allowsActuatorControls());
  channel.mProtocolIdentity.nodeClass=IoHomeNodeClass::Sensor;assert(!channel.allowsActuatorControls());
  channel.mProtocolIdentity={};assert(channel.allowsActuatorControls()); // legacy configured channels unchanged
 }
 category=15;assert(!channel.allowsActuatorControls());
}
'''
# Check the generated C++ fixture stays byte-for-byte consistent with its provenance JSON.
data=json.loads((root/'test/corpus/atlantic_issues_198_199.json').read_text())
rows=data['issues']['199']['observations']
assert len(rows)==13 and len({r['id'] for r in rows})==13
header=(root/'test/corpus/atlantic_issues_198_199.h').read_text()
for row in rows:
    raw=bytes.fromhex(row['hex'])
    assert (raw[0]&31)+1==len(raw)
    expected='{'+','.join(f'0x{x:02X}' for x in raw)+'}'
    assert expected in header
    assert f'"{row["id"]}",{row["frequency_hz"]}U,{row["capture_uptime_ms"]}U,{row["source_body_line"]},' in header
    assert int.from_bytes(raw[2:5],'big')==int(row['destination'],16)
    assert int.from_bytes(raw[5:8],'big')==int(row['source'],16)
assert [r['command'] for r in data['issues']['198']['message_events']]==[0x28,0x29,0x2C,0x2D,0x31,0x3C,0x32,0x6F]
assert not data['issues']['198']['raw_key_confirmation_supplied']
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder);(folder/'guard.cpp').write_text(harness+gate+label+tests)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-I'+str(root/'src'),str(folder/'guard.cpp'),'-o',str(folder/'guard')],check=True)
    subprocess.run([str(folder/'guard')],check=True)
print('Atlantic issue fixtures, production type labels and ordinary appliance-control guard passed')
