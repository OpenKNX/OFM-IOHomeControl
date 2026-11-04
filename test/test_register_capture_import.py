from pathlib import Path
import sys,json,unittest,hashlib
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
import import_register_observations as imp
import import_issue156_logs as rs
class CaptureTests(unittest.TestCase):
 def test_generated_public_header_is_reproducible(self):
  p=root/'test/corpus/atlantic_register_captures.json';docs=json.loads(p.read_text())
  self.assertEqual(imp.header(docs),p.with_suffix('.h').read_text())
  frames=[x for d in docs for x in d['observations'] if x['kind']=='rf_rx']
  self.assertGreater(len(frames),200)
  self.assertTrue(any(x['kind']=='exchange_summary' and x['outcome']=='unconfirmed' for d in docs for x in d['observations']))
  self.assertTrue(any(not x['value_hex'] for x in frames))
 def test_register_echo_and_length_fail_closed(self):
  body='index=0x15: cmd=0x21 hex=8E 00 C0 FF EE BB 0B 50 21 0C 60 01 15 E9 07  [value: E9 07]'
  raw=lambda b:json.dumps(dict(body=b,created_at='2026-11-04T00:00:00Z')).encode()
  self.assertEqual(imp.extract(raw(body),'https://example.invalid')['observations'][0]['register'],0x115)
  for bad in [body.replace('index=0x15','index=0x16'),body.replace('8E 00','8D 00'),body.replace('0C 60','0C 61')]:
   with self.assertRaises(ValueError):imp.extract(raw(bad),'https://example.invalid')
  self.assertEqual(imp.extract(raw('key=private\ncmd=0x3D hex=00 11'),'https://example.invalid')['observations'],[])
 def test_new_rs100_lengths_and_class_redaction(self):
  for n in [4,5]:
   d=json.loads((root/f'test/corpus/issue156-rs100/log-{n}-timeline.json').read_text())
   state=next(o for o in d['objects'] if o['object_id']==0x8103)
   self.assertEqual((state['object_class'],state['available_length'],state['declared_length'],state['complete']),(0,67,67,True))
   light=next(o for o in d['objects'] if o['object_id']==0x8005)
   self.assertEqual(bytes.fromhex(light['hex']),bytes([6])+bytes(15))
   self.assertTrue(all('hex' not in o for o in d['objects'] if o['object_class']!=0))
  text='read_object 24635F object 0x8103 class 2: 1 of 1 bytes\n[home:1]: +000: AA\n'
  self.assertNotIn('hex',rs.import_log(text.encode(),'x','https://example.invalid')['objects'][0])
if __name__=='__main__':unittest.main()
