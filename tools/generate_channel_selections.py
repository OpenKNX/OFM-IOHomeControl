#!/usr/bin/env python3
"""Generate detailed ETS presets for exact device types."""
import argparse, json, re
from pathlib import Path
from xml.sax.saxutils import escape
ROOT=Path(__file__).resolve().parents[1]
rows=json.loads((ROOT/'src/protocol/channel-selections.json').read_text())
assert [r['value'] for r in rows]==list(range(16,16+len(rows)))
assert len({r['packed'] for r in rows if r['control']})==30
recognition={r['packed']:r for r in json.loads((ROOT/'src/protocol/recognition.json').read_text())}
for row in rows:
 if row['control']:assert (row['type'],row['flags'])==(recognition[row['packed']]['type'],recognition[row['packed']]['flags'])
 else:assert row['type']>=15 and row['packed']==0
args=argparse.ArgumentParser();args.add_argument('--check',action='store_true');check=args.parse_args().check

def save(path,text):
 if check:assert path.read_text()==text,f'stale generated selection: {path}'
 else:path.write_text(text)

p=ROOT/'src/IoHomecontrol.share.xml';s=p.read_text()
for name,items in [('IOHCChannelSelection',rows),('IOHCDeviceType',[r for r in rows if not r['control']])]:
 pattern=r'(<ParameterType[^>]+Name="'+name+r'">.*?<TypeRestriction[^>]*>)(.*?)(</TypeRestriction>)'
 match=re.search(pattern,s,re.S);assert match
 if name=='IOHCChannelSelection':
  match_body=re.sub(r'\s*<Enumeration[^>]*Value="(?:[2-5]|[7-9]|1[0-5])"[^>]*/>', '', match[2])
 else:match_body=match[2]
 body=re.sub(r'\s*<!-- BEGIN DETAILED SELECTIONS -->.*?<!-- END DETAILED SELECTIONS -->','',match_body,flags=re.S)
 entries='\n                  <!-- BEGIN DETAILED SELECTIONS -->\n'
 entries+=''.join('                  <Enumeration Text="'+escape(r['label'],{'"':'&quot;'})+'" Value="'+str(r['value'] if name=='IOHCChannelSelection' else r['type'])+'" Id="%ENID%" />\n' for r in items)
 entries+='                  <!-- END DETAILED SELECTIONS -->\n                '
 s=s[:match.start(2)]+body.rstrip()+entries+s[match.end(2):]
s=s.replace('ETS channel selector: 0 disables, other values map to IOHCDeviceType + 1.', 'ETS selector: values 0..15 retain legacy meanings; detailed presets use channel-selections.json.')
save(p,s)

p=ROOT/'src/IoHomecontrol.script.js';s=p.read_text();block='// BEGIN GENERATED CHANNEL SELECTIONS\nvar IOHC_CHANNEL_SELECTIONS = '+json.dumps(rows,ensure_ascii=False,separators=(',',':'))+';\n// END GENERATED CHANNEL SELECTIONS\n'
if '// BEGIN GENERATED CHANNEL SELECTIONS' in s:s=re.sub(r'// BEGIN GENERATED CHANNEL SELECTIONS.*?// END GENERATED CHANNEL SELECTIONS\n',lambda _:block,s,flags=re.S)
else:s=s.replace('function IOHC_syncChannelSelection(',block+'\nfunction IOHC_syncChannelSelection(')
save(p,s)

print('Detailed selections:',len(rows),'(30 supported profiles, 16 diagnosis-only families)')
