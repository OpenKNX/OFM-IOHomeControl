"""Synthetic trace-envelope and configuration privacy tests; no live RF/CRC claim."""
from pathlib import Path
import importlib.util,json,unittest
root=Path(__file__).resolve().parents[1]
def module(name):
 spec=importlib.util.spec_from_file_location(name,root/'tools'/str(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
trace=module('dual_controller_timeline');config=module('sanitize_peer_config')
def frame(ms,src,dst,cmd,payload,phy=True):
 return f'IOHC_CAPTURE filter_epoch=1 dropped_total=0 event={"rx_observed" if phy else "tx_started"} ms={ms} RF=868950000 src={src} dst={dst} cmd={cmd} payload={payload} is_phy_rx={int(phy)} is_local_tx={int(not phy)} crc_checked=1 crc_valid=1 disposition={"foreign_destination_passive_only" if phy and dst!="E4E61D" else "observation_only" if phy else "local_tx"}'
def run(duration):
 lines=['IOHC_CAPTURE_FILTER epoch=1 enabled=1 node=000000 command=FF replacement=1 additive=0',frame(0,'AABBCC','155D81','00','01639E0080000000')]
 for i,p in enumerate([20,50,80]):
  ms=int(duration*i/2);body=bytearray.fromhex('0460A00000000000AABBCC010000');body[4:6]=int(p*512).to_bytes(2,'big');body[6:8]=int((duration-ms)/1000).to_bytes(2,'big')
  lines += [frame(ms,'AABBCC','155D81','03','030000'),frame(ms,'155D81','AABBCC','04',body.hex())]
 return trace.timeline(lines)
class Tools(unittest.TestCase):
 def test_supplied_payload_sequence_remains_observational(self):
  fixture=json.loads((root/'test/corpus/stefan-dual-controller-20261027.json').read_text());lines=[]
  for i,s in enumerate(fixture['samples']):lines += [frame(i*3000,'AABBCC','155D81','03','030000'),frame(i*3000,'155D81','AABBCC','04',s['data_hex'])]
  result=trace.timeline(lines);self.assertEqual([s['remaining_raw'] for s in result['samples']],[64,61,52]);self.assertTrue(all(s['last_command_controller']=='AABBCC' and s['status_source']=='foreign_passive' for s in result['samples']))
 def test_fixed_interval_speed_comparison(self):
  slow=trace.metrics(run(60000),'155D81',20,80);normal=trace.metrics(run(30000),'155D81',20,80)
  self.assertEqual(slow['seconds_per_10_percent'],10);self.assertEqual(normal['seconds_per_10_percent'],5);self.assertEqual(slow['remaining_median_absolute_error_seconds'],0)
 def test_filters_cannot_be_called_full_speed_evidence(self):
  d=run(60000);d['filtered']=True
  with self.assertRaises(ValueError):trace.metrics(d,'155D81',20,80)
 def test_loss_or_denial_or_midrun_target_change_reject_comparison(self):
  for kind in ['ring_dropped_events','local_denials','target']:
   d=run(60000)
   if kind=='target':d['samples'][1]['target_raw']=0xC800
   else:d[kind]=1
   with self.assertRaises(ValueError):trace.metrics(d,'155D81',20,80)
 def test_rearm_is_never_physical_rx(self):
  with self.assertRaises(ValueError):trace.timeline(['IOHC_CAPTURE event=rx_ready is_phy_rx=1'])
 def test_local_denial_is_never_peer_silence(self):
  with self.assertRaises(ValueError):trace.timeline(['IOHC_VERIFY result=tx_denied_duty_budget peer_no_reply=1'])
 def test_71_absence_stays_unknown_not_unsupported(self):
  self.assertEqual(run(30000)['reporting71'],'not_observed')
 def test_local_read_keeps_other_last_controller(self):
  lines=[frame(0,'E4E61D','155D81','03','030000',False),frame(10,'155D81','E4E61D','04','04609E00067C0040AABBCC010000')]
  sample=trace.timeline(lines)['samples'][0];self.assertEqual(sample['last_command_controller'],'AABBCC');self.assertEqual(sample['transport_dst'],'E4E61D')
 def test_sanitizer_never_prints_system_or_unrelated_keys(self):
  secret='0123456789abcdef0123456789abcdef';d={'home_io_control':{'node_id':'AABBCC','system_key':secret,'radio_type':'sx1276','cs_pin':18},'wifi':{'password':'private-password'},'cover':[{'platform':'home_io_control','io_device_id':'155D81','status_poll_interval':'3s','low_power':True}]}
  result=config.sanitize(d);text=json.dumps(result);self.assertNotIn(secret,text);self.assertNotIn('private-password',text);self.assertEqual(result['node_id'],'AABBCC');self.assertEqual(result['actuators'][0]['node_id'],'155D81')
 def test_secret_reference_cannot_be_resolved_or_leaked(self):
  import yaml
  d=yaml.load('home_io_control:\n  node_id: !secret other_node\n  system_key: !secret live_system_key\n',Loader=config.Loader)
  result=config.sanitize(d);self.assertEqual(result['node_id'],'unknown');self.assertTrue(result['system_key_present']);self.assertNotIn('live_system_key',json.dumps(result))
if __name__=='__main__':unittest.main()
