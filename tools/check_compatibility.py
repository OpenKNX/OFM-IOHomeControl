#!/usr/bin/env python3
"""Check preserved legacy parameter/KO layout and checked generator output."""
import argparse,json,re,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--header',type=Path,required=True);a=p.parse_args()
expected=json.loads((root/'test/compatibility-baseline.json').read_text())
actual={n:re.sub(r'\s+',' ',v.split('//')[0].strip()) for n,v in re.findall(r'^#define\s+(\w+)\s+([^\n]+)',a.header.read_text(),re.M)}
errors=[f'{n}: expected {v}, found {actual.get(n)}' for n,v in expected.items() if actual.get(n)!=v]
assert not errors,'Compatibility changed:\n'+'\n'.join(errors)
for script in ('generate_recognition.py','generate_semantics.py'):
 subprocess.run([sys.executable,str(root/'tools'/script),'--check'],check=True)
print(f'{len(expected)} preserved memory/KO definitions and both generators verified')
