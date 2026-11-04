#!/usr/bin/env python3
"""Offline extraction of public Atlantic reply observations; never imports auth bodies."""
import argparse,hashlib,json,re
from pathlib import Path

def digest(b):return hashlib.sha256(b).hexdigest()
def extract(raw,url,comment=None):
    source=json.loads(raw)
    if comment is not None:source=next(x for x in source if x['id']==comment)
    body=source['body'];rows=[];family=None
    for line,text in enumerate(body.splitlines(),1):
        sweep=re.search(r'Sweep "register_0([01])"',text)
        if sweep:family=int(sweep[1])
        index=re.search(r'index=0x([0-9A-Fa-f]{2}):',text)
        match=re.search(r'cmd=0x21 hex=((?:[0-9A-Fa-f]{2}(?:\s+|$))+)',text)
        if match:
            wire=bytes.fromhex(match[1])
            if len(wire)<13 or len(wire)>32 or wire[0]&0x20 or wire[1]!=0 or (wire[0]&31)+1!=len(wire) or wire[8:11]!=b'\x21\x0c\x60' or wire[11]>1:
                raise ValueError(f'line {line}: unreviewed or malformed reply')
            if index and int(index[1],16)!=wire[12]:raise ValueError('echo/index mismatch')
            rows.append(dict(line=line,kind='rf_rx',register=int.from_bytes(wire[11:13],'big'),source=int.from_bytes(wire[5:8],'big'),destination=int.from_bytes(wire[2:5],'big'),hex=wire.hex(' ').upper(),value_hex=wire[13:].hex(' ').upper(),timestamp=(re.search(r'\[(\d\d:\d\d:\d\d\.\d{3})\]',text) or ['',''])[1],frequency_hz=None,crc='not_logged',proof='not_included_in_reply_observation'))
        elif index and family is not None:
            outcome='unconfirmed' if 'accepted the probe request but sent no response' in text else 'no_reply' if 'no reply after' in text else None
            if outcome:rows.append(dict(line=line,kind='exchange_summary',register=(family<<8)|int(index[1],16),outcome=outcome))
    return dict(source_url=url,source_json_sha256=digest(raw),body_sha256=digest(body.encode()),published_at=source['created_at'],observations=rows)
def header(documents):
    out=['#pragma once','#include <cstdint>','namespace AtlanticRegisterCaptures {','struct Frame {uint16_t reg;uint8_t length;uint8_t wire[32];};','inline constexpr Frame frames[]={']
    for d in documents:
        for row in d['observations']:
            if row['kind']=='rf_rx':
                wire=bytes.fromhex(row['hex']);out.append('{0x%04X,%d,{%s}},'%(row['register'],len(wire),','.join('0x%02X'%b for b in wire)))
    return '\n'.join(out+['};','}'])+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--url',required=True);p.add_argument('--comment',type=int);a=p.parse_args()
    a.output.write_text(json.dumps(extract(a.input.read_bytes(),a.url,a.comment),indent=2)+'\n')
