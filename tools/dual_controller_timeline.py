#!/usr/bin/env python3
"""Read-only analysis of provenance-tagged captures; never sends RF or assumes auth."""
import argparse,csv,json,re,statistics
from pathlib import Path

def fields(line):return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)",line))
def number(s,base=10):return int(s,base)
def timeline(lines,gateway='E4E61D'):
 result={'gateway':gateway.upper(),'samples':[],'filter_epochs':[],'ring_dropped_events':0,'filtered':False,'full_capture_seen':False,'local_denials':0,'reporting71':'not_observed','crc_or_parse_rejected':0,'discovery_requests':{},'unmatched_captured_polls':0,'missed_rf_samples':'unknown'}
 pending={};actors={};last_ms=None;wrap=0
 for line in lines:
  f=fields(line)
  if 'IOHC_CAPTURE_FILTER ' in line:
   result['filter_epochs'].append({k:v for k,v in f.items() if k in ['epoch','enabled','node','command','replacement','additive']})
   if f.get('enabled')=='1':
    if f.get('command')!='FF' or f.get('node')!='000000':result['filtered']=True
    else:result['full_capture_seen']=True
  if 'IOHC_VERIFY ' in line:
   denied=f.get('result','') in ['tx_denied_duty_budget','tx_denied_cca','identity_collision','driver_error']
   if denied:
    result['local_denials']+=1
    if f.get('peer_no_reply')=='1':raise ValueError('Local verification denial mislabeled as peer silence')
  if 'IOHC_DISCOVERY_WINDOW ' in line:result['crc_or_parse_rejected']+=int(f.get('crc_rejected',0))+int(f.get('parse_rejected',0))
  if 'IOHC_CAPTURE ' not in line:continue
  result['ring_dropped_events']=max(result['ring_dropped_events'],int(f.get('dropped_total',0)))
  event=f.get('event');phy=f.get('is_phy_rx')=='1';tx=f.get('is_local_tx')=='1' and event=='tx_started'
  if event=='rx_ready' and phy:raise ValueError('radio rearm is not physical RX')
  if event=='tx_started' and f.get('disposition')=='foreign_destination_passive_only':raise ValueError('local TX mislabeled as passive RX')
  if not (phy and event=='rx_observed') and not tx:continue
  try:cmd=int(f['cmd'],16);ms=int(f['ms']);src=f['src'].upper();dst=f['dst'].upper()
  except (KeyError,ValueError):continue
  if last_ms is not None and ms<last_ms and last_ms-ms>0x80000000:wrap+=0x100000000
  last_ms=ms;ms+=wrap
  if cmd==0x71:result['reporting71']='observed_headers_only_auth_and_routing_require_separate_evidence'
  if tx and cmd in [0x28,0x2A]:result['discovery_requests'][f['cmd']]=result['discovery_requests'].get(f['cmd'],0)+1
  raw=f.get('payload','')
  try:body=bytes.fromhex(raw)
  except ValueError:continue # redacted authentication bodies stay absent
  if cmd==0:actors[dst]=src
  if cmd==3 and body==bytes([3,0,0]):
   pending[(src,dst)]=ms;result['unmatched_captured_polls']+=1
  if cmd!=4 or not phy:continue
  request=pending.pop((dst,src),None)
  if request is None or not 0<=ms-request<=2000 or len(body)<8:continue
  result['unmatched_captured_polls']-=1
  target=int.from_bytes(body[2:4],'big');current=int.from_bytes(body[4:6],'big');remaining=int.from_bytes(body[6:8],'big')
  last=int.from_bytes(body[8:11],'big') if len(body)>=12 else 0
  result['samples'].append({'ms':ms,'node':src,'transport_src':src,'transport_dst':dst,'target_raw':target,'current_raw':current,
   'target_percent':target/512 if target<=0xC800 else None,'current_percent':current/512 if current<=0xC800 else None,
   'remaining_raw':remaining,'remaining_known':0<remaining<0xFFFE,'stopped':bool(body[0]&1),
   'last_command_controller':('%06X'%last) if last else None,'originator':body[11] if last else None,
   'last_observed_execute_controller':actors.get(src),'authenticated_local_command':'unknown',
   'status_source':'foreign_passive' if dst!=gateway.upper() else 'local_correlated_candidate_not_auth_proof',
   'crc_checked':f.get('crc_checked')=='1','crc_valid':f.get('crc_valid')=='1','frequency_hz':int(f.get('RF',0))})
 return result

def metrics(data,node,low,high):
 if not data['full_capture_seen'] or data['filtered'] or data['ring_dropped_events'] or data['local_denials']:
  raise ValueError('Comparison needs equivalent full, unfiltered, loss-free, admitted captures')
 samples=[s for s in data['samples'] if s['node']==node.upper() and s['current_percent'] is not None]
 actors={s['last_observed_execute_controller'] for s in samples if s['last_observed_execute_controller']}
 if len(actors)!=1:raise ValueError('Need one identified Execute controller per isolated run')
 if any(s['last_command_controller'] and s['last_command_controller'] not in actors for s in samples):raise ValueError('Observed Execute and last-command attribution differ; isolate the run')
 if len(samples)<2 or not all(s['crc_checked'] and s['crc_valid'] for s in samples):raise ValueError('Need at least two CRC-qualified status samples')
 if len({s['target_raw'] for s in samples})!=1:raise ValueError('Target changed: choose a separate fixed-target run')
 def crossing(value):
  for a,b in zip(samples,samples[1:]):
   p,q=a['current_percent'],b['current_percent']
   if p==value:return a['ms']
   if min(p,q)<=value<=max(p,q) and p!=q:return a['ms']+(value-p)/(q-p)*(b['ms']-a['ms'])
  if samples[-1]['current_percent']==value:return samples[-1]['ms']
  raise ValueError('Samples do not bracket the requested position interval')
 start,end=crossing(low),crossing(high)
 if end<=start:raise ValueError('Requested travel direction/time is inconsistent')
 relevant=[s for s in samples if start<=s['ms']<=end];vel=[]
 for a,b in zip(samples,samples[1:]):
  if b['ms']<=a['ms']:raise ValueError('Non-monotonic samples; split boot/run timelines')
  if a['ms']<=end and b['ms']>=start:vel.append(abs(b['current_percent']-a['current_percent'])/((b['ms']-a['ms'])/1000))
 errors=[abs(s['remaining_raw']-(end-s['ms'])/1000) for s in relevant if s['remaining_known'] and s['target_percent'] is not None and abs(s['target_percent']-high)<0.01]
 return {'node':node.upper(),'from_percent':low,'to_percent':high,'elapsed_seconds':(end-start)/1000,
  'seconds_per_10_percent':(end-start)/1000*10/abs(high-low),'median_percent_per_second':statistics.median(vel),
  'first_measured_percent':samples[0]['current_percent'],'last_measured_percent':samples[-1]['current_percent'],
  'remaining_median_absolute_error_seconds':statistics.median(errors) if errors else None,
  'sample_gaps_ms':[b['ms']-a['ms'] for a,b in zip(samples,samples[1:])],
  'last_observed_execute_controllers':sorted({s['last_observed_execute_controller'] for s in samples if s['last_observed_execute_controller']}),
  'last_command_controllers':sorted({s['last_command_controller'] for s in samples if s['last_command_controller']}),
  'missed_rf_samples':'unknown','unmatched_captured_polls':data['unmatched_captured_polls']}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--compare',type=Path);p.add_argument('--gateway',default='E4E61D');p.add_argument('--node',default='155D81');p.add_argument('--from-percent',type=float,default=20);p.add_argument('--to-percent',type=float,default=80);p.add_argument('--csv',type=Path);a=p.parse_args()
 if not (0<=a.from_percent<=100 and 0<=a.to_percent<=100 and a.from_percent!=a.to_percent):p.error('Invalid position interval')
 with a.log.open() as f:d=timeline(f,a.gateway)
 if a.csv and d['samples']:
  with a.csv.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(d['samples'][0]));w.writeheader();w.writerows(d['samples'])
 if a.compare:
  with a.compare.open() as f:other=timeline(f,a.gateway)
  d={'first':metrics(d,a.node,a.from_percent,a.to_percent),'second':metrics(other,a.node,a.from_percent,a.to_percent),'qualification':'observational speed comparison; RF/auth/hardware acceptance remains external'}
 print(json.dumps(d,indent=2))
if __name__=='__main__':main()
