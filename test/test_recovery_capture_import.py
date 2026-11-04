from pathlib import Path
import sys,json,unittest
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
import import_recovery_traces as imp
class RecoveryTests(unittest.TestCase):
 def test_header_reproduces_reviewed_wire_only(self):
  p=root/'test/corpus/recovery_trace_captures.json';d=json.loads(p.read_text())
  self.assertEqual(imp.header(d),p.with_suffix('.h').read_text())
  self.assertEqual(len(d['sources']),3)
  self.assertTrue(all(f['command'] in [0,3,4] for s in d['sources'] for f in s['frames']))
 def test_auth_and_keys_cannot_enter_derivative(self):
  s='key=private\nTX [15 bytes] freq=868950000 cmd=CHALLENGE_RESP: 0E 00 01 02 03 04 05 06 3D AA BB CC DD EE FF\n'
  self.assertEqual(imp.extract(s.encode(),'x','https://example.invalid')['frames'],[])
 def test_mismatched_length_is_rejected(self):
  s='TX [13 bytes] freq=868950000 cmd=PRIVATE: 4B 00 71 71 22 CA 57 1E 03 03 00 00\n'
  with self.assertRaises(ValueError):imp.extract(s.encode(),'x','https://example.invalid')
if __name__=='__main__':unittest.main()
