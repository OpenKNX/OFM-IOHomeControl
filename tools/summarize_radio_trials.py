#!/usr/bin/env python3
"""Summarize recorded peer trials. Never infers acceptance from RSSI or TX_DONE.
CSV fields: trial,peer,rx_hz,afc_hz,preamble_bytes,outcome,crc_failures,overruns,truncations
outcome: correlated_authenticated_reply,peer_observed_acceptance,unknown,rejected
One row per attempted transaction, counters must be per-trial deltas.
"""
import argparse, csv, json
from collections import defaultdict

def summarize(rows):
    groups=defaultdict(lambda: dict(attempts=0,authenticated_replies=0,peer_acceptances=0,unknown=0,rejected=0,crc_failures=0,overruns=0,truncations=0))
    seen=set()
    for row in rows:
        identity=(row['peer'],row['trial'])
        if not all(identity) or identity in seen: raise ValueError('missing/duplicate peer trial identity')
        seen.add(identity)
        key=(row['peer'],int(row['rx_hz']),int(row['afc_hz']),int(row['preamble_bytes']))
        if key[1] not in (41667,50000,62500,83333,100000) or key[2] not in (41667,50000,62500,83333,100000) or not 1<=key[3]<=65535:
            raise ValueError('unsupported tuning setting')
        outcome={'correlated_authenticated_reply':'authenticated_replies','peer_observed_acceptance':'peer_acceptances','unknown':'unknown','rejected':'rejected'}.get(row['outcome'])
        if outcome is None: raise ValueError('unsupported outcome; TX_DONE is not peer acceptance')
        g=groups[key]; g['attempts']+=1;g[outcome]+=1
        for name in ('crc_failures','overruns','truncations'):
            value=int(row[name])
            if value<0: raise ValueError('counter delta must be nonnegative')
            g[name]+=value
    return [dict(peer=k[0],rx_hz=k[1],afc_hz=k[2],preamble_bytes=k[3],**v) for k,v in sorted(groups.items())]

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('csv');args=parser.parse_args()
    with open(args.csv,newline='') as stream: print(json.dumps(summarize(csv.DictReader(stream)),indent=2))
