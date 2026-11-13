"""Verify the optional VELUX override uses reserved bits and the actual identity gate."""
from pathlib import Path
import os,shlex,subprocess,tempfile,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1];NS={'k':'http://knx.org/xml/project/20'}
old=ET.fromstring(subprocess.check_output(['git','show','e12953d:src/IoHomeMovement.templ.xml'],cwd=ROOT,text=True))
new=ET.parse(ROOT/'src/IoHomeMovement.templ.xml').getroot()
attrs=lambda root,kind:{p.attrib['Id']:p.attrib for p in root.findall('.//k:'+kind,NS)}
for kind in ['Parameter','ParameterRef','ComObject','ComObjectRef']:
    a,b=attrs(old,kind),attrs(new,kind)
    for key,value in a.items():assert b[key]==value,(kind,key)
    assert len(b)==len(a)+(1 if kind in ('Parameter','ParameterRef') else 0)
assert old.find('.//k:Union',NS).attrib==new.find('.//k:Union',NS).attrib=={'SizeInBit':'8'}
parameter=next(p for p in new.findall('.//k:Parameter',NS) if p.attrib['Name']=='c%C%WindowTravelSpeed')
assert parameter.attrib['BitOffset']=='3' and parameter.attrib['Offset']=='0' and parameter.attrib['Value']=='0'
header=(ROOT/'src/IoHomecontrolChannel.h').read_text();a=header.index('  uint8_t selectedWindowTravelSpeed()const');b=header.index('  bool garageEndpointsOnly()',a);getter=header[a:b]
common='''#include <cassert>
#include "protocol/IoHomeCommands.h"
#include "protocol/IoHomeProfileRegistry.h"
unsigned selected=0;bool enabled=true;
'''
for configured in [False,True]:
    defines='#define ParamMVS_cWindowTravelSpeed selected\n#define ParamBASE_ModuleEnabled_MVS enabled\n' if configured else ''
    body=common+defines+'''struct Channel {
    bool mPaired=true,mIs1W=false;uint32_t mNodeId=0x123456;IoHomeProtocolIdentity mProtocolIdentity{};
    const IoHomeProfileDescriptor *getEffectiveProfileDescriptor()const{return ioHomeProfileDescriptor(4,mProtocolIdentity.subProfile);}
'''+getter+'''};
int main(){Channel ch;auto &id=ch.mProtocolIdentity;id.valid=id.fullMetadata=true;id.ioAddress=ch.mNodeId;
id.manufacturerId=1;id.profile=4;id.nodeClass=IoHomeNodeClass::Actuator;
'''
    if configured:
        body+='''for(unsigned sub=0;sub<2;++sub){id.subProfile=sub;for(selected=0;selected<4;++selected)assert(ch.selectedWindowTravelSpeed()==selected);}
selected=3;id.manufacturerId=2;assert(ch.selectedWindowTravelSpeed()==0);id.manufacturerId=1;
id.fullMetadata=false;assert(ch.selectedWindowTravelSpeed()==0);id.fullMetadata=true;
enabled=false;assert(ch.selectedWindowTravelSpeed()==0);enabled=true;
ch.mIs1W=true;assert(ch.selectedWindowTravelSpeed()==0);ch.mIs1W=false;
ch.mPaired=false;assert(ch.selectedWindowTravelSpeed()==0);
'''
    else:body+='selected=3;assert(ch.selectedWindowTravelSpeed()==0);\n'
    body+='}\n'
    with tempfile.TemporaryDirectory(prefix='iohc-window-travel-') as tmp:
        path=Path(tmp)/'test.cpp';path.write_text(body);binary=Path(tmp)/'test'
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-I'+str(ROOT/'src'),str(path),str(ROOT/'src/protocol/IoHomeProfileRegistry.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('VELUX optional override: reserved bits/default-zero, unchanged KOs/old parameters and production profile gate passed')
