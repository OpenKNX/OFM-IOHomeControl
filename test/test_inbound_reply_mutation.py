"""Prove each inbound fast-reply regression rejects the former CCA behavior.
Only a temporary controller copy is mutated; no production switches or edits.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
controller = (root / "src/controller/IoHomeController.cpp").read_text()
marker = "const bool ccaRequired=twoWay && intent!=IoHomeTransmitIntent::ImmediateReply;"
assert controller.count(marker) == 1
mutation = """const bool ccaRequired=twoWay && (intent!=IoHomeTransmitIntent::ImmediateReply ||
    (std::strcmp(std::getenv("IOHC_MUTATION_FORCE"),"challenge")==0 ?
        allowedRole==IoHomeReplyRole::InboundChallenge : allowedRole==IoHomeReplyRole::VerifiedStatusAck));"""
fixture = "controller_inbound_status_busy_handshake_and_same_channel_ack_one_shot"
with tempfile.TemporaryDirectory(prefix="iohc-inbound-mutation-") as temporary:
    temporary = Path(temporary)
    source = temporary / "controller.cpp"
    source.write_text("#include <cstdlib>\n" + controller.replace(marker, mutation))
    binary = temporary / "regression"
    command = shlex.split(os.environ.get("CXX", "clang++")) + [
        "-std=c++17", "-DTEST_NATIVE", "-I" + str(root / "src"),
        "-I" + str(root / "src/controller"), "-I" + str(root / "test"),
        "-o", str(binary), str(root / "test/test_protocol.cpp"),
        str(root / "test/test_suite_main.cpp"), str(source),
    ] + [str(root / path) for path in [
        "src/protocol/IoHomeCrypto.cpp", "src/protocol/IoHomeFrame.cpp",
        "src/protocol/IoHomeProfileRegistry.cpp", "src/IoHomeRemoteMap.cpp",
    ]]
    subprocess.run(command, check=True, capture_output=True, text=True)
    for role in ("challenge", "ack"):
        environment = dict(os.environ, IOHC_MUTATION_FORCE=role)
        result = subprocess.run([str(binary), "--filter", fixture], env=environment,
                                capture_output=True, text=True)
        assert result.returncode == 1 and "0 passed, 1 failed" in result.stdout, result.stdout + result.stderr
        print("Forced CCA on inbound " + role + ": regression rejected as expected")
