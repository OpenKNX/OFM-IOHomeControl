#!/usr/bin/env python3
"""Generate detailed ETS presets while retaining legacy values 0..15 and memory ABI."""
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
 body=re.sub(r'\s*<!-- BEGIN DETAILED SELECTIONS -->.*?<!-- END DETAILED SELECTIONS -->','',match[2],flags=re.S)
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

# Copy existing type-specific branches for detailed presets. These choices remain
# directly driven by Selection, avoiding delayed ETS DeviceType propagation.
p=ROOT/'src/IoHomecontrol.templ.xml';s=p.read_text();s=re.sub(r'\n\s*<!-- BEGIN DETAILED BRANCHES -->.*?<!-- END DETAILED BRANCHES -->','',s,flags=re.S)
s=re.sub(r'<!-- DETAILED GROUP: ([0-9 ]+) -->\s*<when test="[0-9 ]+">', lambda m:'<when test="'+m[1]+'">',s)
selector='<choose ParamRefId="%AID%_P-%TT%%CC%096_R-%TT%%CC%09601">'
starts=[m.start() for m in re.finditer(re.escape(selector),s)]
for start in reversed(starts):
 depth=0;end=None
 for token in re.finditer(r'<choose\b[^>]*>|</choose>',s[start:]):
  depth+= -1 if token[0].startswith('</') else 1
  if depth==0:end=start+token.start();break
 assert end is not None
 fragment=s[start+len(selector):end];branches={};depth=0;branch_start=None;value=None
 for token in re.finditer(r'<when\b[^>]*>|</when>',fragment):
  if token[0].startswith('</'):
   depth-=1
   if depth==0 and value is not None:branches[value]=fragment[branch_start:token.end()]
  else:
   if depth==0:
    match=re.search(r'test="(\d+)"',token[0]);value=int(match[1]) if match else None;branch_start=token.start()
   depth+=1
 copies=[]
 for row in rows:
  branch=branches.get(row['type']+1) if row['control'] else None
  if branch:copies.append(re.sub(r'test="\d+"','test="'+str(row['value'])+'"',branch,count=1))
 if copies:s=s[:end].rstrip()+'\n<!-- BEGIN DETAILED BRANCHES -->\n'+'\n'.join(copies)+'\n<!-- END DETAILED BRANCHES -->\n'+s[end:]
# Multi-category branches share one tab/control. Extend their condition rather
# than cloning whole tabs, keeping each visible only once per channel.
def extend_group(match):
 legacy=[int(v) for v in match[2].split()]
 values=legacy+[r['value'] for r in rows if r['control'] and r['type']+1 in legacy]
 return match[1]+'<!-- DETAILED GROUP: '+match[2]+' -->\n<when test="'+' '.join(map(str,values))+'">'
s=re.sub('('+re.escape(selector)+r'\s*)<when test="([0-9]+(?: [0-9]+)+)">',extend_group,s)
save(p,s)
print('Detailed selections:',len(rows),'(30 supported profiles, 16 diagnosis-only families)')
