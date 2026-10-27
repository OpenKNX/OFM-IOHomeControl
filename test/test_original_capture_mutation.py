"""Original SSL observations must reject the two previous production behaviors."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="iohc-original-motion-") as directory:
    directory=Path(directory)
    source=(root/"src/controller/IoHomeController.cpp").read_text()
    marker="const bool lMoving = !iStopped;"
    assert source.count(marker)==1
    old="""bool lMoving = !iStopped;
        if(lMoving && lHasCurrentPosition && lHasTargetPosition && lTargetRaw<=IOHC_POSITION_MAX &&
           (lCurrentRaw>lTargetRaw?lCurrentRaw-lTargetRaw:lTargetRaw-lCurrentRaw)<=100)lMoving=false;"""
    mutated=directory/"controller.cpp";mutated.write_text(source.replace(marker,old))
    binary=directory/"regression"
    command=shlex.split(os.environ.get("CXX","clang++"))+[
        "-std=c++17","-DTEST_NATIVE","-I"+str(root/"src"),"-I"+str(root/"src/controller"),"-I"+str(root/"test"),
        "-o",str(binary),str(root/"test/test_protocol.cpp"),str(root/"test/test_suite_main.cpp"),str(mutated),
    ]+[str(root/p) for p in ["src/protocol/IoHomeCrypto.cpp","src/protocol/IoHomeFrame.cpp","src/protocol/IoHomeProfileRegistry.cpp","src/IoHomeRemoteMap.cpp"]]
    subprocess.run(command,check=True,capture_output=True,text=True)
    result=subprocess.run([str(binary),"--filter","controller_original_ssl_stopped_off_target_poll_keeps_current_target_and_motion_distinct"],capture_output=True,text=True)
    assert result.returncode==1 and "0 passed, 1 failed" in result.stdout,result.stdout+result.stderr
    print("Original SSL capture rejected the previous near-target motion override as expected")

    # Mutate the extracted production channel method, preserving the actual
    # stopTravelEstimation implementation and original YAML-derived values.
    script=(root/"test/test_wake_production.py").read_text()
    script=script.replace("root=Path(__file__).resolve().parents[1]","root=Path("+repr(str(root))+")",1)
    new="""        // Current and target have just been decoded independently. Do not snap
        // the measured current or replace a distinct reported target on STOPPED.
        mTravelDurationMs = 0;
        mTravelStartTime = 0;
        mTravelStartPosition = mCurrentPosition;"""
    production=(root/"src/IoHomecontrolChannel.cpp").read_text()
    assert new in production
    statement="channel=(root/'src/IoHomecontrolChannel.cpp').read_text()"
    assert statement in script
    script=script.replace(statement,statement+"\nchannel=channel.replace("+repr(new)+","+repr("        stopTravelEstimation(false);")+")",1)
    harness=directory/"prior_channel.py";harness.write_text(script)
    result=subprocess.run([os.sys.executable,str(harness)],capture_output=True,text=True)
    assert result.returncode!=0 and ("Assertion failed" in result.stderr or "Assertion" in result.stderr),result.stdout+result.stderr
    print("Original SSL values rejected the previous stopped-position/target rewrite as expected")
