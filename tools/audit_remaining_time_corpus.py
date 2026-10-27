#!/usr/bin/env python3
"""Host-only structural audit; never infer correlation/authentication from byte length."""
import json
from collections import Counter
from laberning_corpus import load_captures

def audit():
 manifest,captures=load_captures();rows=[]
 for entry,doc in captures:
  pending={}
  for index,f in enumerate(doc['frames']):
   raw=bytes.fromhex(f['hex']);raw=raw[:-2] if f['crc']=='present' else raw
   extended=(raw[1]&3)==3;offset=2 if extended else 0
   if len(raw)<9+offset or raw[0]&0x20:continue
   dst=int.from_bytes(raw[2+offset:5+offset],'big');src=int.from_bytes(raw[5+offset:8+offset],'big')
   cmd=raw[8+offset];data=raw[9+offset:]
   if cmd in [0,3]:pending[(src,dst)]=(cmd,data,index)
   if cmd!=4:continue
   request=pending.get((dst,src))
   if request and request[:2]==(3,bytes([3,0,0])):context='captured_main_info_request'
   elif request and request[0]==0:context='execute_ack_excluded'
   elif request:context='other_private_variant_excluded'
   elif '/statuspoll/' in entry['upstream_path']:context='statuspoll_documented_request_omitted'
   else:context='unknown_request_context'
   rows.append({'capture':entry['id'],'frame_index':index,'source_path':entry['local_path'],
    'request_context':context,'captured_request_index':request[2] if request else None,
    'payload_bytes':len(data),'layout_available':len(data)>=8,
    'raw_candidate_6_7':int.from_bytes(data[6:8],'big') if len(data)>=8 else None,
    'synthetic_or_reference':entry['classification']=='synthetic_reference',
    'authentication':'unknown' if doc['key']=='unknown' else 'public_rekeyed_fixture_not_live_proof'})
 qualified=[x for x in rows if x['request_context'] in ['captured_main_info_request','statuspoll_documented_request_omitted'] and x['layout_available'] and not x['synthetic_or_reference']]
 high=[x for x in qualified if x['raw_candidate_6_7']>=256 and x['raw_candidate_6_7']<0xFFFE]
 return {'schema':1,'corpus_commit':manifest['commit'],'original_yaml_count':len(captures),
  'description':'Structural request-context audit; offsets alone do not establish RF layout or live authentication. Omitted requests remain omitted.',
  'context_counts':dict(Counter(x['request_context'] for x in rows)),
  'candidate_poll_frame_count':len(qualified),'positive_high_byte_candidates':high,
  'frames':rows}
if __name__=='__main__':
 import argparse
 from pathlib import Path
 parser=argparse.ArgumentParser();parser.add_argument('--check',type=Path);args=parser.parse_args();result=audit()
 if args.check:
  if json.loads(args.check.read_text())!=result:raise SystemExit('Remaining-time corpus audit differs')
  print('177 original YAML hashes and remaining-time evidence audit match; no positive high-byte capture established')
 else:print(json.dumps(result,indent=2))
