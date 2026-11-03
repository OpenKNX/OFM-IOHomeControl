#!/usr/bin/env python3
"""Provenance, byte-preservation and authentication-body exclusion regressions."""
from pathlib import Path
import hashlib
import json
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import import_issue156_logs as importer

URL='https://example.invalid/original-capture.txt'
def frame_line(wire, frequency=868950000, **fields):
    command=fields.get('command',wire[8]);source=fields.get('source',int.from_bytes(wire[5:8],'big'))
    destination=fields.get('destination',int.from_bytes(wire[2:5],'big'))
    return f'[20:00:00.123][D][io_capture:106]: chip=sx1262 phase=component stage=parse_ok freq={frequency} ts=123 len={len(wire)} cmd=0x{command:02X} src={source:06X} dst={destination:06X} payload={wire.hex(" ").upper()}\n'
def imported(text):
    return importer.import_log(text.encode(),'original.txt',URL,'2026-11-01T09:33:01Z')
def dump(summary, data, key=0x8103):
    lines=[f'[10:00:00.000][I][home:1]: read_object 24635F object 0x{key:04X}: {summary[0]} of {summary[1]} bytes'+(' (incomplete)' if summary[0]<summary[1] else '')+'\n']
    for offset in range(0,len(data),24):
        lines.append(f'[10:00:00.001][I][home:2]:   +{offset:03X}: {data[offset:offset+24].hex(" ").upper()}\n')
    return ''.join(lines)

class ImportTests(unittest.TestCase):
    def setUp(self):
        self.wire=bytes.fromhex('92 00 A1 B2 C3 24 63 5F 71 05 60 20 05 05 06 00 00 00 00')
    def test_exact_provenance_and_wire_bytes(self):
        text=frame_line(self.wire);doc=imported(text);row=doc['timeline'][0]
        self.assertEqual(doc['source']['sha256'],hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(row['hex'],self.wire.hex(' ').upper());self.assertEqual(row['line_start'],1)
        self.assertEqual(row['parser_class'],'device_event');self.assertEqual(row['uptime_ms'],123)
        self.assertIsNone(doc['source']['capture_date']);self.assertEqual(row['crc'],'not_present_in_log')
    def test_duplicate_bytes_different_frequencies_are_distinct_observations(self):
        doc=imported(frame_line(self.wire,868250000)+frame_line(self.wire,869850000))
        self.assertEqual([r['frequency_hz'] for r in doc['timeline']],[868250000,869850000])
        self.assertEqual([r['line_start'] for r in doc['timeline']],[1,2])
    def test_auth_selfauth_and_unassociated_chunks_are_never_published(self):
        # Entirely synthetic credential marker; never import live secrets into tests.
        marker=bytes.fromhex('DE AD BE EF CA FE')
        proof=bytes.fromhex('8E 00 A1 B2 C3 24 63 5F 3D')+marker
        one_way=bytes.fromhex('AE 00 A1 B2 C3 24 63 5F 71')+marker
        chunk=bytes.fromhex('8E 00 A1 B2 C3 24 63 5F 4B')+marker
        doc=imported(frame_line(proof)+frame_line(one_way)+frame_line(chunk)+'system_key=not-a-real-key hmac=not-a-real-proof\n')
        self.assertTrue(all('hex' not in r for r in doc['timeline']))
        self.assertNotIn(marker.hex(' ').upper(),json.dumps(doc));self.assertNotIn('not-a-real',json.dumps(doc))
        self.assertEqual(doc['privacy']['credential_label_lines'],[4])
        self.assertEqual(doc['privacy']['body_omissions']['unreviewed_transfer_chunk_body'],1)
    def test_masked_payload_and_parse_failure_cannot_become_packets(self):
        text=frame_line(self.wire).replace('len=19','len=20')
        doc=imported(text);self.assertNotIn('hex',doc['timeline'][0])
        text=frame_line(self.wire).replace('stage=parse_ok','stage=parse_fail')
        self.assertEqual(imported(text)['timeline'][0]['body_omitted'],'parse_failure_unreviewed_body')
        flagged=bytearray(self.wire);flagged[1]=0x20
        self.assertEqual(imported(frame_line(flagged))['timeline'][0]['body_omitted'],'unreviewed_control_flags_body')
    def test_inconsistent_wire_metadata_is_rejected(self):
        with self.assertRaises(ValueError):imported(frame_line(self.wire,command=0x55))
        with self.assertRaises(ValueError):imported(frame_line(self.wire,source=0x111111))
        with self.assertRaises(ValueError):imported(frame_line(bytes([0x91])+self.wire[1:]))
    def test_complete_partial_objects_are_counted_independently(self):
        data=bytes(range(54));doc=imported(dump((54,67),data));obj=doc['objects'][0]
        self.assertEqual((obj['available_length'],obj['declared_length'],obj['complete']),(54,67,False))
        self.assertEqual(bytes.fromhex(obj['hex']),data);self.assertEqual(obj['line_end'],4)
        obj=imported(dump((54,54),data))['objects'][0];self.assertTrue(obj['complete'])
    def test_gap_duplicate_out_of_order_and_false_summary_are_rejected(self):
        text=dump((54,67),bytes(range(54)))
        for replacement in ['+017:','+000:','+030:']:
            with self.assertRaises(ValueError):imported(text.replace('+018:',replacement))
        with self.assertRaises(ValueError):imported(text.replace('54 of 67','53 of 67'))
    def test_unreviewed_object_data_is_excluded(self):
        doc=imported(dump((16,16),bytes(range(16)),0xA607));obj=doc['objects'][0]
        self.assertNotIn('hex',obj);self.assertEqual(obj['body_omitted'],'unreviewed_object')
    def test_retries_remain_message_level_not_invented_packets(self):
        line='[10:00:00.000][I][exchange:1]: Try 2 answered: cmd=UNKNOWN4A_REQ(0x4A) wait_ms=239 preamble=1024 age_ms=22\n'
        row=imported(line)['timeline'][0]
        self.assertEqual(row['kind'],'exchange_summary');self.assertEqual(row['wait_ms'],239)
        self.assertEqual(row['command'],0x4A);self.assertNotIn('hex',row)
    def test_real_fixture_header_is_reproducible_and_completion_exact(self):
        path=ROOT/'test/corpus/issue156_rs100_fixtures.json';doc=json.loads(path.read_text())
        header=importer.generate_header(doc)
        self.assertEqual(header,(path.with_suffix('.h')).read_text())
        self.assertEqual(len(doc['frames']),9);self.assertEqual(len(doc['payloads']),1)
        self.assertFalse(doc['payloads'][0]['full_packet_present']);self.assertIsNone(doc['payloads'][0]['frequency_hz'])
        self.assertEqual([(o['object_id'],o['available_length'],o['declared_length'],o['complete']) for o in doc['objects']],
                         [(0x8100,150,150,True),(0x8103,54,67,False),(0x8001,14,14,True),(0x8002,16,16,True)])
        self.assertTrue(all(not f['command'] in [0x3C,0x3D,0x32] for f in doc['frames']))
        doc['frames'][0]['command']=0x3D
        with self.assertRaises(ValueError):importer.generate_header(doc)
    def test_input_limit(self):
        with self.assertRaises(ValueError):importer.import_log(b' '*(8*1024*1024+1),'huge.log',URL)

if __name__=='__main__':unittest.main()
