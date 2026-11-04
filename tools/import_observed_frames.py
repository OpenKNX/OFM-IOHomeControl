#!/usr/bin/env python3
"""Extract reviewed complete public 2W observations; never export auth/key bodies."""
import argparse,hashlib,json,re
from pathlib import Path
HEX=r'((?:[0-9A-Fa-f]{2}(?:\s+|$))+)'
CAPTURE=re.compile(r'stage=(?:parse_ok|probe_rx) freq=(\d+) ts=(\d+) len=(\d+) cmd=0x([0-9A-Fa-f]{2}).*?payload='+HEX)
EXCHANGE=re.compile(r'(TX|RX) \[(\d+) bytes\] freq=(\d+).*?cmd=\w+[^:]*:\s*'+HEX)
REPLY=re.compile(r'probe "([a-z_0-9]+)" reply cmd=0x([0-9A-Fa-f]{2}).*?hex='+HEX)
def safe(w):
    if len(w)<9 or len(w)>32 or w[0]&0x20 or w[1] not in (0,0x20):return False
    cmd=w[8];d=w[9:]
    if cmd==0:return len(d)==6
    if cmd==3:return d==b'\x03\x00\x00'
    if cmd==4:return 6<=len(d)<=23
    if cmd==0x0C:return d==b'\xd8\x09\x00\x00'
    if cmd==0x0D:return len(d)==5
    if cmd==0x71:return len(d)>=5 and d[4]+5==len(d) and d[2:4] in (b'\x10\x0a',b'\x20\x05')
    if cmd==0xFE:return len(d)==1
    if cmd in (0x46,0x48):return len(d)==9 and d[2]==0
    if cmd==0x47:return len(d)==4
    if cmd in (0x4A,0x4B):return len(d)==3
    return False

def extract(raw,name,url,body=None,published_at=None):
    text=body if body is not None else raw.decode();rows=[]
    for line,s in enumerate(text.splitlines(),1):
        s=s.replace('\\r\\n','');m=CAPTURE.search(s);kind='io_capture';freq=uptime=None
        if m:
            freq,uptime,length,cmd,wire=m.groups();freq=int(freq);uptime=int(uptime)
        else:
            m=EXCHANGE.search(s)
            if m:direction,length,freq,wire=m.groups();kind='exchange_frame';freq=int(freq);cmd=None
            else:
                m=REPLY.search(s)
                if not m:continue
                probe,cmd,wire=m.groups();length=None;kind='reported_reply'
        w=bytes.fromhex(wire)
        row=dict(line=line,kind=kind,direction=direction if kind=='exchange_frame' else 'RX',frequency_hz=freq,uptime_ms=uptime)
        clock=re.search(r'\[(\d\d:\d\d:\d\d\.\d{3})\]',s);row['timestamp']=clock[1] if clock else None
        if len(w)<9 or (length is not None and len(w)!=int(length)):
            row['body_omitted']='masked_or_incomplete';rows.append(row);continue
        if (w[0]&31)+1!=len(w):
            row['body_omitted']='declared_length_or_unreviewed_trailer';rows.append(row);continue
        if cmd is not None and int(cmd,16)!=w[8]:raise ValueError(f'line {line}: opcode mismatch')
        if kind=='io_capture':
            row['phase']=re.search(r'phase=(\w+)',s)[1]
            row['stage']=re.search(r'stage=(\w+)',s)[1]
            for field,offset in [('src',5),('dst',2)]:
                reported=re.search(r'\b'+field+r'=([0-9A-Fa-f]{6})',s)
                if reported and int(reported[1],16)!=int.from_bytes(w[offset:offset+3],'big'):raise ValueError(f'line {line}: node mismatch')
        row.update(command=w[8],source=int.from_bytes(w[5:8],'big'),destination=int.from_bytes(w[2:5],'big'),length=len(w),crc='not_logged',proof='not_established_by_observation')
        if safe(w):row['hex']=w.hex(' ').upper()
        else:row['body_omitted']='authentication_or_unreviewed_object_body'
        if w[8] in (0x4A,0x4B) and len(w)>=12:
            row['transport']={'handle':w[9],'word':int.from_bytes(w[10:12],'big'),'value_bytes':len(w)-12}
        rows.append(row)
    return dict(name=name,source_url=url,sha256=hashlib.sha256(raw).hexdigest(),body_sha256=hashlib.sha256(text.encode()).hexdigest(),published_at=published_at,capture_date=None,observations=rows)
def header(doc):
    out=['#pragma once','#include <cstdint>','namespace ObservedFrames {','struct Frame {uint8_t sourceIndex,length;uint32_t frequency;uint32_t line;uint8_t wire[32];};','inline constexpr Frame frames[]={']
    for i,s in enumerate(doc['sources']):
        for row in s['observations']:
            if 'hex' not in row:continue
            w=bytes.fromhex(row['hex']);assert safe(w)
            out.append('{%d,%d,%d,%d,{%s}},'%(i,len(w),row['frequency_hz'] or 0,row['line'],','.join('0x%02X'%x for x in w)))
    return '\n'.join(out+['};','}'])+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('json',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args();out=a.json.with_suffix('.h');s=header(json.loads(a.json.read_text()))
    if a.check:
        if out.read_text()!=s:raise SystemExit('Header mismatch')
    else:out.write_text(s)
