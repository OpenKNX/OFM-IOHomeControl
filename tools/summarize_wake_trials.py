#!/usr/bin/env python3
"""Summarize one row per transmitted RF attempt; never attribute unrelated status to a poll."""
import argparse
import csv
import json
import re
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

def console_rows(text, *, trial_prefix, firmware_sha, radio_chip, date,
                 device_state, other_sender_active):
    """Import bounded console CSV snapshots; deduplicate dumps by accepted TX ID.

    Context is supplied by the tester. A missing/overwritten TX start cannot be
    reconstructed. The same-peer association ambiguity remains in extra columns.
    """
    header = None
    snapshots = {}
    stable = ('session_id','tx_id','try_index','device_node','ctrl0','ctrl1',
              'command','preamble_bytes','frequency_hz','tx_start_us')
    for line in text.splitlines():
        match = re.search(r'2WAttemptCSV (.*)', line)
        if not match: continue
        cells = next(csv.reader([match.group(1)]))
        if cells[0] == 'session_id':
            header = cells
            continue
        if header is None or len(cells) != len(header):
            raise ValueError('missing or inconsistent 2WAttemptCSV header')
        row = dict(zip(header,cells))
        key = row['tx_id']
        if key in snapshots and any(row[k] != snapshots[key][k] for k in stable):
            raise ValueError('TX ID reused across boots/captures; split the log')
        snapshots[key] = row # latest snapshot contains the later result
    if header is None: raise ValueError('no 2WAttemptCSV export found')
    result = []
    for raw in sorted(snapshots.values(), key=lambda row:int(row['tx_id'])):
        row = dict.fromkeys(FIELDS,'')
        row.update({key:raw[key] for key in stable if key in row})
        row.update(trial_id=f"{trial_prefix}:{raw['session_id']}",
            date=date,firmware_sha=firmware_sha,radio_chip=radio_chip,
            device_state=device_state,other_sender_active=other_sender_active,
            tx_done_us=raw['tx_done_us'] if raw['tx_done_valid']=='1' else '',
            rx_ready_us=raw['rx_ready_us'] if raw['rx_ready_valid']=='1' else '',
            challenge_seen=raw['challenge_seen'],
            final_response=str(int(raw['exchange_completed']=='1' or raw['poll_completed']=='1')),
            response_correlated=raw['response_correlated'],poll_completed=raw['poll_completed'],
            status_received=raw['poll_completed'],failure_reason=raw['failure_reason'],
            session_id=raw['session_id'],association_ambiguous=raw['association_ambiguous'])
        result.append(row)
    summarize(result) # run the same identity and correlation checks as manual CSV
    return result

def summarize_requests(rows):
    requests = defaultdict(list)
    def completed(row): return flag(row,'response_correlated') and flag(row,'final_response')
    for row in rows:
        requests[(row['firmware_sha'],row['trial_id'])].append(row)
    return dict(requests=len(requests),rf_transmissions=len(rows),
        completed_requests=sum(any(completed(r) for r in rs)
                               for rs in requests.values()),
        first_attempt_completed_requests=sum(any(int(r['try_index'])==0 and completed(r)
                                                for r in rs) for rs in requests.values()),
        retry_completed_requests=sum(any(completed(r) for r in rs) and
                                     not any(int(r['try_index'])==0 and completed(r) for r in rs)
                                     for rs in requests.values()),
        timeout_transmissions=sum(r['failure_reason']=='response_timeout' for r in rows))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv',nargs='?');parser.add_argument('--template',action='store_true')
    parser.add_argument('--console-log');parser.add_argument('--firmware-sha');parser.add_argument('--date')
    parser.add_argument('--trial-prefix');parser.add_argument('--radio-chip',choices=('SX1276','SX1262'))
    parser.add_argument('--device-state',choices=('resting','moving','unknown'))
    parser.add_argument('--other-sender-active',choices=('yes','no'))
    parser.add_argument('--csv-output')
    args=parser.parse_args()
    if args.template: print(','.join(FIELDS))
    elif args.console_log:
        context = {k:getattr(args,k) for k in ('trial_prefix','firmware_sha','radio_chip','date','device_state','other_sender_active')}
        if not all(context.values()):parser.error('console import requires all tester context options')
        with open(args.console_log) as stream: rows=console_rows(stream.read(),**context)
        if args.csv_output:
            with open(args.csv_output,'w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=FIELDS+['session_id','association_ambiguous'])
                writer.writeheader();writer.writerows(rows)
        print(json.dumps(dict(groups=summarize(rows),request_summary=summarize_requests(rows)),indent=2))
    elif args.csv:
        with open(args.csv,newline='') as stream: rows=list(csv.DictReader(stream))
        print(json.dumps(dict(groups=summarize(rows),request_summary=summarize_requests(rows)),indent=2))
    else:parser.error('provide a CSV, --console-log or --template')
