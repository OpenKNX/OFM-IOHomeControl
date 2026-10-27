#!/usr/bin/env python3
"""Emit only the controller identity/radio/polling allowlist; never output a key."""
import argparse,json,re,hmac,math
from pathlib import Path
import yaml
class Loader(yaml.SafeLoader):pass
Loader.add_constructor(None,lambda loader,node:'<redacted_tag>')
def node_id(value):
 if type(value) is int and 0<value<=0xFFFFFF:return '%06X'%value
 if isinstance(value,str) and re.fullmatch(r'(?:0x)?[0-9A-Fa-f]{6}',value):return value.removeprefix('0x').upper()
 return 'unknown'
def key_bytes(value):
 try:
  if isinstance(value,list) and len(value)==16 and all(isinstance(x,int) and 0<=x<=255 for x in value):return bytes(value)
  if isinstance(value,str) and re.fullmatch(r'[0-9a-fA-F ]{32,47}',value):
   result=bytes.fromhex(value);return result if len(result)==16 else None
 except ValueError:pass
 return None
def load(path):
 try:
  with Path(path).open() as f:result=yaml.load(f,Loader=Loader)
 except yaml.YAMLError:raise ValueError('Configuration parse failed; details suppressed to protect secrets') from None
 if not isinstance(result,dict):raise ValueError('Expected configuration mapping')
 return result
def sanitize(document):
 h=document.get('home_io_control',{}) if isinstance(document,dict) else {};h=h if isinstance(h,dict) else {}
 result={'schema':1,'provenance':'user_supplied_configuration_not_on_air_identity_proof','node_id':node_id(h.get('node_id')),'system_key_present':'system_key' in h,'system_key':'redacted','radio_type':h.get('radio_type') if h.get('radio_type') in ['sx1262','sx1276','lr1121'] else 'unknown','pins':{},'actuators':[]}
 for name in ['cs_pin','rst_pin','dio0_pin','dio1_pin','busy_pin']:
  value=h.get(name);value=value.get('number') if isinstance(value,dict) else value
  if isinstance(value,int) and 0<=value<=64:result['pins'][name]=value
 covers=document.get('cover',[]) if isinstance(document,dict) else []
 if isinstance(covers,list):
  for c in covers:
   if not isinstance(c,dict) or c.get('platform')!='home_io_control':continue
   interval=c.get('status_poll_interval');interval=interval if isinstance(interval,(int,float)) or isinstance(interval,str) and re.fullmatch(r'[0-9.]+(?:ms|s|m)',interval) else 'unknown'
   result['actuators'].append({'node_id':node_id(c.get('io_device_id')),'status_poll_interval':interval,'low_power':c.get('low_power') if isinstance(c.get('low_power'),bool) else 'unknown'})
 return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('config',type=Path);p.add_argument('--compare-keys',type=Path);p.add_argument('--firmware-sha');a=p.parse_args();d=load(a.config);out=sanitize(d)
 if a.firmware_sha:
  if not re.fullmatch(r'[0-9a-fA-F]{7,40}',a.firmware_sha):p.error('Invalid SHA')
  out['firmware_sha']=a.firmware_sha
 if a.compare_keys:
  other=load(a.compare_keys)
  def configured_key(document):
   controller=document.get('home_io_control');return key_bytes(controller.get('system_key')) if isinstance(controller,dict) else None
  left=configured_key(d);right=configured_key(other)
  out['local_key_equivalence']='unknown' if left is None or right is None else 'same' if hmac.compare_digest(left,right) else 'different'
 print(json.dumps(out,indent=2))
if __name__=='__main__':
 try:main()
 except ValueError as e:raise SystemExit(str(e)) from None
