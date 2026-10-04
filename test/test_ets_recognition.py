"""Run the actual ETS JavaScript with Node.js or `pip install quickjs`."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent
source = (root.parent / "src/IoHomecontrol.script.js").read_text()
source += "\n" + (root / "test_ets_recognition.js").read_text()
if shutil.which("node"):
    subprocess.run(["node", "-e", source + '\nconsole.log(testsPassed + " ETS recognition tests passed");'], check=True)
else:
    import quickjs
    context = quickjs.Context()
    context.eval(source)
    print(f"{context.eval('testsPassed')} ETS recognition tests passed")
