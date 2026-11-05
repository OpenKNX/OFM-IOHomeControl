"""Validate provenance boundaries and actual supplied application observations."""
import importlib.util,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
import import_light_followup as importer
p=root/'test/corpus/light-followup/evidence.json';doc=json.loads(p.read_text());assert importer.validate(doc)
assert p.with_suffix('.h').read_text()==importer.generated(doc)
provided=json.loads((root/'test/corpus/rs100-light-event-vectors-2026-11-05.json').read_text())
actual=[r for r in doc['application_events'] if r['source']=='somfy-log.txt']
assert len(actual)==len(provided['observations'])==15
for recorded,expected in zip(actual,provided['observations']):
    assert recorded['payload_hex']==expected['payload_hex'] and recorded['capture_class']=='B'
    body=bytes.fromhex(recorded['payload_hex']);assert int.from_bytes(body[7:9],'big')==expected['sensor_value_raw_be16']
assert provided['observations'][7]['external_lux_near_time'] is None
assert doc['application_events'][-1]['payload_hex']=='05 60 20 01 01 64' # Battery is a separate event.
for filename in ['somfy-log-timeline.json','rs100-log-8-timeline.json']:
    timeline=json.loads(p.with_name(filename).read_text())
    for obj in timeline['objects']:
        if obj['object_class']==2:assert 'hex' not in obj and obj['body_omitted']=='unreviewed_object'
# Neither authentication nor unreviewed object bytes can be exported as safe RF.
for command,data in [(0x3C,b'123456'),(0x3D,b'123456'),(0x4B,bytes(range(21))),(0x73,b'\x01\x20\x05')]:
    frame=bytes([8+len(data),0,0,0,1,0,0,2,command])+data
    assert not importer.safe(frame)
print('Light follow-up: 15 exact class-B payloads, separate battery event, source digests, private-object redaction and generated RF header passed')
