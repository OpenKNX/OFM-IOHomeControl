"""Compile the actual read-only SML codec against the attributed application bodies."""
import json,os,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
doc=json.loads((ROOT/'test/corpus/alignment-20261113/status/velux_sml_queued_wait_power_mp_fp.json').read_text())
assert doc['kind']=='application_reply_body' and doc['frequency_hz'] is None
source='''#include <cassert>
#include <cstring>
#include "protocol/IoHomeSmlExecution.h"
int main(){
IoHomeProtocolIdentity id{};id.valid=true;id.ioAddress=0x123456;id.profile=2;id.manufacturerId=1;
IoHomeFrame f;f.init();f.commandId=IoHomeCommand::PrivateResponse;f.setSrcNode(0x123456);f.setDestNode(0x654321);
'''
for row in doc['samples']:
    body=bytes.fromhex(row['body']);assert len(body)==8
    source+='{const uint8_t body[]={'+','.join(str(v) for v in body)+'};'
    source+='f.dataLen=8;std::memcpy(f.data,body,8);auto v=IoHomeSmlExecution::decode(f,id,0x123456,0x654321,true);'
    source+=f'assert(v.valid&&v.state==IoHomeSmlExecution::State::{row["state"]}&&v.currentRaw=={row["current_raw"]});'
    source+='f.dataLen=7;assert(!IoHomeSmlExecution::decode(f,id,0x123456,0x654321,true).valid);}\n'
source+='}\n'
with tempfile.TemporaryDirectory(prefix='iohc-sml-observations-') as tmp:
    path=Path(tmp)/'sml.cpp';path.write_text(source);binary=Path(tmp)/'sml'
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-DTEST_NATIVE','-I'+str(ROOT/'src'),str(path),str(ROOT/'src/protocol/IoHomeFrame.cpp'),str(ROOT/'src/protocol/IoHomeCrypto.cpp'),'-I'+str(ROOT/'test'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Attributed SML application bodies passed the production codec; RF/authentication unqualified')
