#!/usr/bin/env python3
"""Check acceptance-record completeness and artifact integrity, not RF truth.
Never enables firmware permissions. Software/model results cannot fill physical gates.
"""
import argparse
import hashlib
import json
from pathlib import Path

CORE = {
    'ets_application_campaign': 'ets',
    'storage_wear_filesystem_campaign': 'physical',
    'reservation_journal_powercut_campaign': 'physical',
    'network_journal_powercut_campaign': 'physical',
    'semtech_prefix_mapping': 'physical',
    'sx1276_edge_wiring': 'physical',
    'sx1276_peer_campaign': 'physical',
    'sx1276_waveform_crc_fifo': 'physical',
    'sx1276_timing_discovery_low_power': 'physical',
    'peer_authentication_retry_stop': 'physical',
    'controller_version3': 'physical',
    'oneway_peer_recovery': 'physical',
    'oneway_powercut_corruption_exhaustion': 'physical',
    'network_assignment_powercut': 'physical',
    'ets_import_reopen_download_resume_upgrade': 'ets',
}
QUALIFICATION_CASES = {
    'ets_application_campaign': ('signed_import', 'new_device', 'upgrade', 'reopen', 'download', 'reboot'),
    'storage_wear_filesystem_campaign': ('esp_network_rate', 'esp_receipt_rate', 'esp_metadata_rate', 'esp_reservation_rate', 'nvs_margin', 'littlefs_partitions', 'littlefs_mount', 'littlefs_atomicity', 'littlefs_wear'),
    'reservation_journal_powercut_campaign': ('before_commit', 'inactive_write', 'lost_ack', 'before_rf', 'no_reuse', 'identity_mismatch', 'wrap', 'repeated_reboots'),
    'network_journal_powercut_campaign': ('each_write_phase', 'old_record', 'lost_ack', 'equal_generation_conflict', 'corruption', 'binding_mismatch', 'tombstone', 'wrap'),
    'semtech_prefix_mapping': ('short_2w', 'long_2w', 'oneway_wake', 'sync', 'no_double_framing', 'trailer'),
    'sx1276_edge_wiring': ('dio4_present', 'dio2_present', 'dio4_absent', 'dio2_absent', 'edges_after_restart'),
    'sx1276_peer_campaign': ('normal_2w', 'low_power_wake', 'actuator_discovery', 'spe_discovery', 'group_timing', 'directed_timing', 'challenge_key', 'version3', 'oneway', 'crc_fifo', 'scan_hold', 'interference_lbt', 'fault_restore'),}

PRODUCT = {'rgb': 'rgb_binding_read_write_state', 'white': 'white_binding_read_write_state',
           'sensor': 'sensor_status_default_subscription_polling'}

def check(document, root, product=None):
    if not isinstance(document, dict) or document.get('schema') != 1:
        raise ValueError('unsupported evidence schema')
    required = dict(CORE)
    if product:
        required[PRODUCT[product]] = 'physical'
    records = document.get('records')
    if not isinstance(records, list):
        raise ValueError('records must be a list')
    indexed = {}
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get('id'), str) or record['id'] in indexed:
            raise ValueError('invalid or duplicate evidence record')
        indexed[record['id']] = record
    root = Path(root).resolve()
    result = []
    for identifier, kind in required.items():
        record = indexed.get(identifier, {})
        problems = []
        if record.get('outcome') != 'passed':
            problems.append('unrun, failed or missing')
        else:
            for case in QUALIFICATION_CASES.get(identifier, ()):
                if record.get('cases', {}).get(case) != 'passed':
                    problems.append('missing passed case: ' + case)
            if record.get('kind') != kind:
                problems.append('wrong evidence kind; model/build tests are insufficient')
            for field in ('ofm_commit', 'oam_commit'):
                value = record.get(field, '')
                if not isinstance(value, str) or len(value) != 40 or any(c not in '0123456789abcdef' for c in value):
                    problems.append('missing exact revision')
            for field in ('operator', 'date', 'setup', 'expected', 'observed'):
                if not isinstance(record.get(field), str) or not record[field].strip():
                    problems.append('missing context or observation')
                    break
            if kind == 'physical' and (not isinstance(record.get('peer_identity'), str) or not record['peer_identity'].strip()):
                problems.append('missing peer identity evidence')
            if kind == 'ets' and (not isinstance(record.get('ets_version'), str) or not record['ets_version'].strip()):
                problems.append('missing ETS version')
            artifacts = record.get('artifacts')
            if not isinstance(artifacts, list) or not artifacts:
                problems.append('missing retained evidence artifacts')
            else:
                for artifact in artifacts:
                    if not isinstance(artifact, dict) or not isinstance(artifact.get('path'), str):
                        problems.append('invalid artifact declaration')
                        continue
                    candidate = (root / artifact['path']).resolve()
                    if Path(artifact['path']).is_absolute() or not candidate.is_relative_to(root):
                        problems.append('artifact outside evidence directory')
                        continue
                    if not candidate.is_file() or not candidate.stat().st_size:
                        problems.append('missing or empty artifact')
                        continue
                    if hashlib.sha256(candidate.read_bytes()).hexdigest() != artifact.get('sha256'):
                        problems.append('artifact digest mismatch')
        result.append(dict(id=identifier, complete=not problems, problems=problems))
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('--product', choices=PRODUCT)
    args = parser.parse_args()
    try:
        results = check(json.loads(args.record.read_text()), args.record.parent, args.product)
        print(json.dumps({'complete': all(r['complete'] for r in results), 'gates': results}, indent=2))
        raise SystemExit(0 if all(r['complete'] for r in results) else 2)
    except (ValueError, OSError):
        parser.exit(1, 'Invalid or unreadable evidence record/artifact.\n')
