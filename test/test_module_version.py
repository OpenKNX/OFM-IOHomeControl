"""Verify the actual module version() body with generated, explicit and absent metadata."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
body = re.search(r"const std::string IoHomecontrol::version\(\)\s*\{[^}]+\}",
                 (root / "src/IoHomecontrol.cpp").read_text()).group(0)
cases = [
    ('#define MODULE_IOHomeControl_Version "0.9.3+test-sha"', '0.9.3+test-sha'),
    ('#define MODULE_IoHomecontrol_Version "explicit"', 'explicit'),
    ('#define MODULE_IOHomeControl_Version "generated"\n#define MODULE_IoHomecontrol_Version "explicit"', 'explicit'),
    ('', '0.1.0-dev'),
]
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    for defines, expected in cases:
        source = defines + '\n#include "ModuleVersionCheck.h"\n#include <string>\n#include <cassert>\n'
        source += 'class IoHomecontrol { public: const std::string version(); };\n' + body
        source += f'\nint main() {{ IoHomecontrol module; assert(module.version()=="{expected}"); }}\n'
        (path / 'version.cpp').write_text(source)
        subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-I'+str(root/'src'),
                        str(path/'version.cpp'), '-o', str(path/'version')], check=True)
        subprocess.run([str(path/'version')], check=True)
print('Production module version lookup passed: generated metadata, explicit override and fallback')
