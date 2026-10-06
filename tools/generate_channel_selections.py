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

# Keep KO visibility independent of hidden ParameterCalculation outputs for manual presets.
# Discovery retains its category/feature conditions; object IDs/numbers remain unchanged.
def core_kos(kind,flags):
 position=(0,1,2,4,5,10)
 if kind==1:return position+(19,)
 if kind==2:return position+(11,)
 if kind in (3,9,10):return position
 if kind in (4,7):return (3,6) if flags&2 else position
 if kind==5:return (20,22,23,24)
 if kind==6:return (0,3,6) if flags&4 and not flags&2 else (3,6)
 if kind==8:return (3,7)
 if kind in (11,13):return (0,)
 if kind==12:return (3,6)
 if kind==14:return (3,6) if flags&2 else (3,)
 raise AssertionError(f'unsupported manual category: {kind}')

def ko_refs(numbers):
 return ''.join('<ComObjectRefRef RefId="%AID%_O-%TT%%CC%'+f'{number:03d}'+
                '_R-%TT%%CC%'+f'{number:03d}'+'01" />' for number in numbers)

p=ROOT/'src/IoHomecontrol.templ.xml';s=p.read_text();groups={}
for row in rows:
 if row['control']:groups.setdefault(core_kos(row['type'],row['flags']),[]).append(row['value'])
groups.setdefault(core_kos(5,0),[]).append(6) # Cozy legacy protocol variant remains selectable.
for name,items in [('CORE',groups.items()),('WEATHER',[((18,),[r['value'] for r in rows if r['control'] and r['type'] in (1,2,3)])])]:
 block='<!-- BEGIN GENERATED MANUAL '+name+' KOS -->\n'
 for numbers,selections in items:
  block+='                          <when test="'+' '.join(map(str,sorted(selections)))+'">'+ko_refs(numbers)+'</when>\n'
 block+='                          <!-- END GENERATED MANUAL '+name+' KOS -->'
 pattern=r'<!-- BEGIN GENERATED MANUAL '+name+r' KOS -->.*?<!-- END GENERATED MANUAL '+name+r' KOS -->'
 s,count=re.subn(pattern,lambda _:block,s,flags=re.S);assert count==1
save(p,s)

print('Detailed selections:',len(rows),'(30 supported profiles, 16 diagnosis-only families)')
