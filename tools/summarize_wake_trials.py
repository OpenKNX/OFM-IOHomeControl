#!/usr/bin/env python3
"""Summarize one row per transmitted RF attempt; never attribute unrelated status to a poll."""
import argparse
import csv
import json
from collections import defaultdict

FIELDS = ('trial_id,tx_id,date,firmware_sha,radio_chip,device_node,device_state,'
          'other_sender_active,ctrl0,ctrl1,command,preamble_bytes,try_index,frequency_hz,'
          'tx_start_us,tx_done_us,rx_ready_us,preamble_seen,sync_seen,rx_done_us,rx_rssi_dbm,'
          'challenge_seen,final_response,response_correlated,status_received,poll_completed,failure_reason').split(',')

def flag(row, field):
    value = row[field].strip().lower()
    if value in ('1','yes','true'): return True
    if value in ('0','no','false'): return False
    raise ValueError(f'{field} must explicitly be yes/no, not inferred from a timestamp')

def elapsed(row, start, end):
    if not row[start] or not row[end]: return None
    values = [int(row[start]), int(row[end])]
    if any(not 0 <= value <= 0xFFFFFFFF for value in values):
        raise ValueError('timestamps must be unsigned MCU microseconds')
    return (values[1]-values[0]) & 0xFFFFFFFF

def summarize(rows):
    groups = defaultdict(lambda: dict(attempts=0,challenge_replies=0,final_replies=0,
        completed_polls=0,unrelated_status=0,failures={},tx_duration_us=[],tx_to_rx_ready_us=[]))
    seen = set()
    for row in rows:
        missing = set(FIELDS)-row.keys()
        if missing: raise ValueError('missing columns: '+','.join(sorted(missing)))
        identity = (row['firmware_sha'],row['trial_id'],row['tx_id'])
        if not all(identity) or identity in seen: raise ValueError('missing/duplicate RF attempt identity')
        seen.add(identity)
        if row['device_state'] not in ('resting','moving','unknown'):
            raise ValueError('device_state must be observed resting/moving or unknown')
        if row['radio_chip'] not in ('SX1262','SX1276'): raise ValueError('unsupported radio_chip')
        preamble = int(row['preamble_bytes'])
        if not 1 <= preamble <= (8191 if row['radio_chip']=='SX1262' else 65535):
            raise ValueError('preamble exceeds chip representation')
        if not row['tx_start_us']: raise ValueError('row is not a transmitted attempt')
        if int(row['try_index']) < 0: raise ValueError('try_index is zero based')
        correlated, challenge, final, complete = (flag(row,n) for n in
            ('response_correlated','challenge_seen','final_response','poll_completed'))
        if (complete and not (correlated and final)) or (challenge and not correlated):
            raise ValueError('poll completion/challenge must belong to the open request')
        key = (row['firmware_sha'],row['radio_chip'],row['device_node'],row['device_state'],
               preamble,int(row['frequency_hz']),flag(row,'other_sender_active'))
        group = groups[key];group['attempts'] += 1
        group['challenge_replies'] += correlated and challenge
        group['final_replies'] += correlated and final
        group['completed_polls'] += complete
        group['unrelated_status'] += flag(row,'status_received') and not complete
        failure = row['failure_reason']
        if failure and failure != 'none': group['failures'][failure]=group['failures'].get(failure,0)+1
        for name,start,end in [('tx_duration_us','tx_start_us','tx_done_us'),
                               ('tx_to_rx_ready_us','tx_done_us','rx_ready_us')]:
            value=elapsed(row,start,end)
            if value is not None:group[name].append(value)
    return [dict(firmware_sha=k[0],radio_chip=k[1],device_node=k[2],device_state=k[3],
        preamble_bytes=k[4],frequency_hz=k[5],other_sender_active=k[6],**v)
        for k,v in sorted(groups.items())]

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv',nargs='?');parser.add_argument('--template',action='store_true');args=parser.parse_args()
    if args.template: print(','.join(FIELDS))
    elif args.csv:
        with open(args.csv,newline='') as stream:print(json.dumps(summarize(csv.DictReader(stream)),indent=2))
    else:parser.error('provide a CSV or --template')
