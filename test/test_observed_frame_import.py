from pathlib import Path
import json,sys,unittest
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
import import_observed_frames as imp
class Observations(unittest.TestCase):
 def test_reviewed_header_is_reproducible(self):
  p=root/'test/corpus/recent_observed_frames.json';d=json.loads(p.read_text());self.assertEqual(imp.header(d),p.with_suffix('.h').read_text())
  rows=[x for s in d['sources'] for x in s['observations'] if 'hex' in x]
  self.assertTrue(all(bytes.fromhex(x['hex'])[8] not in (0x31,0x32,0x3C,0x3D,0x38) for x in rows))
  self.assertTrue(any(x.get('stage')=='probe_rx' and x['frequency_hz']==868950000 for x in rows))
 def test_auth_keys_and_unknown_block_bodies_are_excluded(self):
  raw=('key=private\n[10:00:00.001][D][io_capture:1]: phase=component stage=parse_ok freq=868950000 ts=5 len=15 cmd=0x3D src=112233 dst=445566 payload=8E 00 44 55 66 11 22 33 3D AA BB CC DD EE FF\n'
       '[10:00:00.002][D][io_capture:1]: phase=component stage=parse_ok freq=868950000 ts=6 len=14 cmd=0x4B src=112233 dst=445566 payload=8D 00 44 55 66 11 22 33 4B 01 00 01 AA BB\n')
  d=imp.extract(raw.encode(),'private.txt','https://example.invalid')
  self.assertEqual(d['name'],'private.txt');self.assertTrue(all('hex' not in x for x in d['observations']))
  self.assertEqual(d['observations'][1]['transport'],dict(handle=1,word=1,value_bytes=2))
  self.assertNotIn('AA BB',json.dumps(d));self.assertNotIn('key=private',json.dumps(d))
 def test_printed_header_mismatch_cannot_become_capture(self):
  raw='phase=component stage=probe_rx freq=868950000 ts=1 len=10 cmd=0xFE src=112233 dst=445566 payload=89 00 44 55 66 11 22 33 FE 70'
  self.assertIn('hex',imp.extract(raw.encode(),'x','https://example.invalid')['observations'][0])
  with self.assertRaises(ValueError):imp.extract(raw.replace('src=112233','src=112234').encode(),'x','https://example.invalid')
  self.assertNotIn('hex',imp.extract(raw.replace('len=10','len=11').encode(),'x','https://example.invalid')['observations'][0])
if __name__=='__main__':unittest.main()
