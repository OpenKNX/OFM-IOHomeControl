#!/usr/bin/env python3
"""Verify the pinned public Sunea capture and emit functional-only native replay data."""
import hashlib,json,sys
from pathlib import Path
import yaml
ROOT=Path(__file__).resolve().parents[1]
DIRECTORY=ROOT/'test/corpus/alignment-20261113'
SOURCE=DIRECTORY/'somfy_awning_statuspoll_close_with_remaining_time_predicted_end_read_sx1262.yaml'
HEADER=DIRECTORY/'sunea_functional.h'
def render():
    manifest=json.loads((DIRECTORY/'manifest.json').read_text());raw=SOURCE.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==manifest['sha256']
    assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==manifest['git_blob_sha1']
    # Reuse the original corpus's public-key/recoverable-key safety validation.
    import laberning_corpus
    laberning_corpus.load_captures()
    document=yaml.safe_load(raw);laberning_corpus.validate_document(document,SOURCE)
    assert document['key']=='corpus' and document['source']['origin']=='own-hardware'
    rows=[];last=-1
    for frame in document['frames']:
        wire=bytes.fromhex(frame['hex']);assert frame['t_ms']>=last;last=frame['t_ms']
        assert frame['freq']==manifest['frequency_hz'] and frame['crc']=='absent'
        # Native header never contains challenge, MAC or key-transfer bodies.
        if wire[8] not in (3,4):continue
        assert len(wire)<=32 and (wire[0]&31)+1==len(wire)
        rows.append('{%d,%d,{%s}},'%(frame['t_ms'],len(wire),','.join('0x%02X'%b for b in wire)))
    return '\n'.join(['#pragma once','#include <cstdint>','namespace SuneaNov13 {',
        'struct Frame {uint32_t ms;uint8_t length,wire[32];};','inline constexpr Frame frames[]={']+rows+['};','}'])+'\n'
if __name__=='__main__':
    result=render()
    if '--check' in sys.argv:assert HEADER.read_text()==result,'Regenerate reviewed functional replay data'
    else:HEADER.write_text(result)
    print('Pinned Sunea source, public-corpus safety and functional-only replay verified')
