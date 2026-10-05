import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('evidence',Path(__file__).resolve().parents[1]/'tools/check_release_evidence.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class EvidenceTest(unittest.TestCase):
    def test_unrun_template_never_passes(self):
        import json
        path=Path(__file__).resolve().parents[1]/'docs/release-evidence.template.json'
        result=module.check(json.loads(path.read_text()),path.parent,'rgb')
        self.assertTrue(all(not r['complete'] for r in result))

    def test_model_and_changed_or_external_artifacts_cannot_fill_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);artifact=root/'capture.txt';artifact.write_text('retained observation')
            record=dict(id='controller_version3',kind='model',outcome='passed',ofm_commit='a'*40,oam_commit='b'*40,
                        operator='tester',date='2026-10-05',setup='board',expected='reply',observed='reply',peer_identity='peer',
                        artifacts=[dict(path='capture.txt',sha256=hashlib.sha256(artifact.read_bytes()).hexdigest())])
            def gate():
                return next(r for r in module.check(dict(schema=1,records=[record]),root) if r['id']==record['id'])
            self.assertFalse(gate()['complete']);record['kind']='physical';self.assertTrue(gate()['complete'])
            artifact.write_text('modified');self.assertFalse(gate()['complete'])
            record['artifacts'][0]['path']='../capture.txt';self.assertFalse(gate()['complete'])
            record['artifacts'][0]['path']=str(artifact);self.assertFalse(gate()['complete'])

    def test_sensor_feature_requires_its_own_physical_evidence(self):
        import json
        path=Path(__file__).resolve().parents[1]/'docs/release-evidence.template.json'
        result=module.check(json.loads(path.read_text()),path.parent,'sensor')
        gate=next(r for r in result if r['id']=='sensor_status_default_subscription_polling')
        self.assertFalse(gate['complete'])

    def test_duplicate_records_are_rejected(self):
        with self.assertRaises(ValueError):module.check(dict(schema=1,records=[dict(id='x'),dict(id='x')]),'.')

if __name__=='__main__':unittest.main()
