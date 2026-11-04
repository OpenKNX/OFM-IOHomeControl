#!/usr/bin/env python3
"""Review-only importer for complete 2W Execute/Private frames; auth excluded."""
from pathlib import Path
import argparse,hashlib,json,re

COMMANDS={'EXECUTE':0x00,'PRIVATE':0x03,'PRIVATE_RESP':0x04}
PATTERN=re.compile(r'(TX|RX) \[(\d+) bytes\] freq=(\d+)(.*?)cmd=(EXECUTE|PRIVATE|PRIVATE_RESP)\b[^:]*:\s*((?:[0-9A-Fa-f]{2}(?:\s+|$))+)')
def extract(raw,name,url,body=None):
 text=body if body is not None else raw.decode();rows=[]
 for line,s in enumerate(text.splitlines(),1):
  # Literal CRLF escapes in attachments are transport text, not frame bytes.
  m=PATTERN.search(s.replace('\\r\\n',''))
  if not m:continue
  direction,length,freq,meta,cmd,payload=m.groups();wire=bytes.fromhex(payload)
  if len(wire)<9 or wire[0]&0x20:continue
  if len(wire)!=int(length) or (wire[0]&31)+1!=len(wire) or wire[8]!=COMMANDS[cmd]:raise ValueError(f'line {line}: inconsistent complete frame')
  if wire[8]==0 and len(wire)!=15:continue
  if wire[8]==3 and (len(wire)!=12 or wire[9:]!=b'\x03\x00\x00'):continue
  if wire[8]==4 and not 8<=len(wire)-9<=23 and len(wire)-9!=6:continue
  clock=re.search(r'\[(\d\d:\d\d:\d\d\.\d{3})\]',s)
  preamble=re.search(r'preamble=(\d+)',meta)
  rows.append(dict(line=line,direction=direction,timestamp=clock[1] if clock else None,frequency_hz=int(freq),preamble_symbols=int(preamble[1]) if preamble else None,hex=wire.hex(' ').upper(),command=wire[8],source=int.from_bytes(wire[5:8],'big'),destination=int.from_bytes(wire[2:5],'big'),crc='not_logged',authentication='not_reconstructed'))
 return dict(name=name,source_url=url,sha256=hashlib.sha256(raw).hexdigest(),body_sha256=hashlib.sha256(text.encode()).hexdigest(),frames=rows)
def header(doc):
 lines=['#pragma once','#include <cstdint>','namespace RecoveryTraceCaptures {','struct Frame {uint8_t sourceIndex,length;uint8_t wire[32];};','inline constexpr Frame frames[]={']
 for i,source in enumerate(doc['sources']):
  for f in source['frames']:
   wire=bytes.fromhex(f['hex']);lines.append('{%d,%d,{%s}},'%(i,len(wire),','.join('0x%02X'%v for v in wire)))
 return '\n'.join(lines+['};','}'])+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('json',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args();target=a.json.with_suffix('.h');text=header(json.loads(a.json.read_text()))
 if a.check:
  if target.read_text()!=text:raise SystemExit('Header differs from reviewed JSON')
 else:target.write_text(text)
