"""Generate host-only supplied payload excerpts; no invented RF provenance."""
import json,sys,tempfile,os
from pathlib import Path
root=Path(__file__).resolve().parents[1]
d=json.loads((root/'test/corpus/stefan-dual-controller-20261027.json').read_text())
assert not d['full_rf_headers_available'] and not d['crc_independently_verified'] and d['synthetic_transport_in_tests']
rows=[]
for sample in d['samples']:
 data=bytes.fromhex(sample['data_hex']);assert len(data)==14 and int.from_bytes(data[6:8],'big')==sample['remaining_seconds']
 rows.append('{'+','.join('0x%02X'%b for b in data)+'}')
text='// Host-only supplied payload excerpts; all transport envelopes in tests are synthetic.\n#pragma once\n#include <cstdint>\ninline constexpr uint8_t stefanSlowPayloads[][14]={'+','.join(rows)+'};\n'
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
with tempfile.NamedTemporaryFile(mode='w',dir=out.parent,delete=False) as f:f.write(text);name=f.name
os.replace(name,out)
