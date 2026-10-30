from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
    for name,flags in [("production",[]),("simulation",["-DTEST_NATIVE"]),("debug",["-DOPENKNX_DEBUG"]),("debug_native",["-DOPENKNX_DEBUG","-DTEST_NATIVE"]),("debug_web",["-DOPENKNX_DEBUG","-DOPENKNX_WEBCONSOLE"]),("bench",["-DOPENKNX_DEBUG","-DIOHC_ENABLE_RF_BENCH_UNLIMITED"])]:
        binary=Path(d)/name
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++17',*flags,str(root/'test_rf_access_policy.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    for flags in [["-DIOHC_ENABLE_RF_BENCH_UNLIMITED"],["-DIOHC_ENABLE_RF_BENCH_UNLIMITED","-DOPENKNX_WEBCONSOLE"]]:
        result=subprocess.run([os.environ.get('CXX','c++'),'-std=c++17',*flags,str(root/'test_rf_access_policy.cpp'),'-o',str(Path(d)/'denied')],capture_output=True)
        assert result.returncode and b'error:' in result.stderr
print('Debug/debug-web always unlimited after off/reset/reboot; release limits and compile gates passed')
