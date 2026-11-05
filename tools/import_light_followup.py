#!/usr/bin/env python3
"""Import reviewed follow-up evidence without exporting authentication or class2 bodies."""
import argparse, hashlib, json, re
from pathlib import Path
from import_observed_frames import extract, header, safe
from import_issue156_logs import import_log, SUMMARY, DUMP

SOURCES = ('somfy-log.txt', 'rs100-log-8.txt') + tuple(f'garage-{n}.txt' for n in range(34433992,34434000))
EVENT = re.compile(r"Last Device Event.*?'#(\d+) t=(\d+) ([0-9A-Fa-f]{6}) 0x71 ([0-9A-Fa-f ]+)'")

def object13(raw):
    """Private reconstruction; return no bodies to the public report."""
    lines=raw.decode().splitlines(); result=[]
    for i,line in enumerate(lines):
        m=SUMMARY.search(line)
        if not m or int(m[2],16)!=0xA601 or int(m[3] or 0)!=2:continue
        body=bytearray()
        for following in lines[i+1:]:
            d=DUMP.search(following)
            if not d:break
            if int(d[1],16)!=len(body):raise ValueError('object13 offset mismatch')
            body.extend(bytes.fromhex(d[2]))
        if len(body)!=int(m[4]) or len(body)!=int(m[5]):raise ValueError('incomplete private object13')
        result.append((i+1,bytes(body)))
    return result

def build(directory):
    records=json.loads((directory/'downloads.json').read_text()); manifest={r['name']:r for r in records}
    sources=[]; objects=[]; app=[]; timelines=[]
    for name in SOURCES:
        meta=manifest[name];raw=(directory/name).read_bytes()
        if meta['status']!=0 or hashlib.sha256(raw).hexdigest()!=meta['sha256'] or len(raw)!=meta['bytes']:raise ValueError('original integrity: '+name)
        item=extract(raw,name,meta['url']);item['schema_version']=1;item['extracted_at']='2026-11-05'
        item['source_attachment_url']=meta['url'];item['source_sha256']=item['sha256']
        item['source_issue_comment_url']='https://github.com/laberning/home_io_control/issues/'+('156#issuecomment-6665992911' if name=='somfy-log.txt' else '156#issuecomment-6640070627' if name=='rs100-log-8.txt' else '163#issuecomment-6662545548')
        for row in item['observations']:
            row['capture_class']='B' if row['kind']=='reported_reply' else 'A' if 'hex' in row else 'C'
            row['confidence']='complete_observed_bytes_no_authentication_claim' if row['capture_class']=='A' else 'application_summary' if row['capture_class']=='B' else 'metadata_only'
        sources.append(item)
        if name in SOURCES[:2]:
            imported=import_log(raw,name,meta['url']);timelines.append(imported)
            for line,s in enumerate(raw.decode().splitlines(),1):
                match=EVENT.search(s)
                if match:
                    sequence,uptime,node,payload=match.groups();body=bytes.fromhex(payload)
                    if len(body)<5 or body[4]+5!=len(body):raise ValueError('application envelope mismatch')
                    app.append(dict(source=name,line=line,sequence=int(sequence),uptime=int(uptime),node=node.upper(),payload_hex=body.hex(' ').upper(),capture_class='B',unit='UNKNOWN'))
            for line,body in object13(raw):objects.append((name,line,body))
    correlation=[]
    for (name,line,body),(other,otherline,second) in zip(objects,objects[1:]):
        correlation.append(dict(first_source=name,first_line=line,second_source=other,second_line=otherline,
            selector=2,object_id='A601',length=len(body),changed_offsets=[i for i,(a,b) in enumerate(zip(body,second)) if a!=b],
            confidence='byte_difference_only',body_omitted='unreviewed_class2',unit='UNKNOWN'))
    doc=dict(schema_version=1,sources=sources,application_events=app,private_object13_differences=correlation,
        calibration='not_established; external lux is a different sensor; no interpolation or scaling')
    return doc,timelines

def generated(doc):
    # Application summaries are kept separate even when they print a full envelope.
    selected=dict(sources=[dict(s,observations=[r for r in s['observations'] if r['capture_class']=='A' and 'hex' in r]) for s in doc['sources']])
    return header(selected).replace('namespace ObservedFrames','namespace LightFollowup')

def validate(doc):
    for source in doc['sources']:
        if not re.fullmatch('[0-9a-f]{64}',source['source_sha256']):raise ValueError('missing provenance digest')
        for row in source['observations']:
            if 'hex' not in row:continue
            wire=bytes.fromhex(row['hex'])
            if not safe(wire) or (wire[0]&31)+1!=len(wire):raise ValueError('unsafe public bytes')
            if row['kind']=='reported_reply' and row['capture_class']!='B':raise ValueError('summary promoted to RF')
    for delta in doc['private_object13_differences']:
        if delta.get('hex') or delta['selector']!=2 or delta['object_id']!='A601':raise ValueError('unreviewed object body')
    return True

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('path',type=Path);p.add_argument('--output',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    if a.check:
        doc=json.loads(a.path.read_text());validate(doc)
        if a.path.with_suffix('.h').read_text()!=generated(doc):raise SystemExit('follow-up header mismatch')
    else:
        doc,timelines=build(a.path);validate(doc)
        if not a.output:p.error('--output required')
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(doc,indent=2)+'\n');a.output.with_suffix('.h').write_text(generated(doc))
        for name,timeline in zip(SOURCES,timelines):a.output.with_name(name.removesuffix('.txt')+'-timeline.json').write_text(json.dumps(timeline,indent=2)+'\n')
if __name__=='__main__':main()
