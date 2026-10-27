#!/usr/bin/env python3
"""Offline, host-only verification and bounded native vectors from original YAML.
Source inventory: laberning/home_io_control c82d38f47901b71c1c63413e638241dbe1aa2d06.
Upstream classification/outcome values describe upstream transport tests, not actuation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys
try:
    import yaml
except ImportError:
    raise SystemExit("Install test/requirements-corpus.txt with the Python used by make (PYTHON=...).")

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "test/corpus/laberning-manifest.json"
VENDOR = ROOT / "test/corpus/laberning_tools"
COMMIT = "c82d38f47901b71c1c63413e638241dbe1aa2d06"
MAX_NATIVE_FRAMES = 32
ALLOWED_STATES = {"original_imported", "derived_equivalent", "pending", "not_applicable", "synthetic_reference"}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def git_blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def local_path(root, value):
    path = (root / value).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("fixture path escapes repository")
    return path

def check_inventory(manifest):
    rows = manifest["entries"]
    paths = [row["upstream_path"] for row in rows]
    ids = [row["id"] for row in rows]
    if paths != sorted(paths) or len(set(ids)) != len(ids) or len(set(paths)) != len(paths):
        raise ValueError("duplicate or unordered inventory")
    identity = "\n".join(row["upstream_path"] + " " + row["git_blob_sha1"] for row in rows).encode()
    if digest(identity) != manifest["inventory_sha256"]:
        raise ValueError("pinned inventory regressed; review intentional inventory changes")
    if manifest["schema"] != 1 or manifest["commit"] != COMMIT:
        raise ValueError("unexpected manifest schema or source revision")

def validate_document(document, path):
    allowed = {"id", "description", "source", "key", "frames", "expect", "node_map"}
    if set(document) - allowed:
        raise ValueError("unreviewed capture fields (possible credentials/key material)")
    if document.get("key") not in {"unknown", "corpus"}:
        raise ValueError("only unknown or explicitly public corpus keys are allowed")
    import validate  # vendored original schema, CRC and recoverable-key safety gate
    validate.validate_capture(document, path)

def read_capture(root, entry):
    if entry["classification"] not in ALLOWED_STATES:
        raise ValueError("unknown capture classification")
    data = local_path(root, entry["local_path"]).read_bytes()
    if digest(data) != entry["sha256"] or git_blob(data) != entry["git_blob_sha1"]:
        raise ValueError(entry["id"] + ": original YAML hash changed")
    document = yaml.safe_load(data)
    if document["id"] != entry["id"] or len(document["frames"]) != entry["frame_count"]:
        raise ValueError("capture identity/frame count changed")
    if entry["key_mode"]!=document["key"] or entry["origin"]!=document["source"]["origin"] or entry["radio"]!=document["source"]["captured_with"]:
        raise ValueError("source provenance changed in manifest")
    validate_document(document, entry["local_path"])
    return document

def load_captures(root=ROOT, manifest=None):
    manifest = manifest or json.loads((root / "test/corpus/laberning-manifest.json").read_text())
    check_inventory(manifest)
    # Verify executable upstream helpers before importing them.
    for helper in manifest["helper_sources"]:
        if digest(local_path(root, helper["local_path"]).read_bytes()) != helper["sha256"]:
            raise ValueError("vendored upstream helper changed")
    sys.path.insert(0, str(root / "test/corpus/laberning_tools"))
    return manifest, [(entry, read_capture(root, entry)) for entry in manifest["entries"]]

def sample_indices(frames):
    if len(frames) <= MAX_NATIVE_FRAMES:
        return list(range(len(frames)))
    # Preserve node/frequency/length coverage and endpoints of long observer logs.
    selected = {0, len(frames)-1}
    seen = set()
    for i, frame in enumerate(frames):
        raw = bytes.fromhex(frame["hex"])
        signature = (raw[8], frame.get("freq", 0), raw[5:8], len(raw))
        if signature not in seen and len(selected) < MAX_NATIVE_FRAMES:
            selected.add(i)
            seen.add(signature)
    for i in range(MAX_NATIVE_FRAMES):
        if len(selected) >= MAX_NATIVE_FRAMES:
            break
        selected.add(i*(len(frames)-1)//(MAX_NATIVE_FRAMES-1))
    return sorted(selected)

def preamble_from_note(note):
    match = re.search(r"\b(32|1024)-(?:byte|symbol) (?:wake-up |start )?preamble", note)
    return int(match[1]) if match else 0

def compare_upstream(manifest, repository):
    result = subprocess.check_output(["git", "-C", str(repository), "ls-tree", "-r", "HEAD", "--", manifest["upstream_capture_root"]], text=True)
    observed = {line.split("\t", 1)[1] for line in result.splitlines() if line.endswith(".yaml")}
    expected = {row["upstream_path"] for row in manifest["entries"]}
    return {"new_upstream_files": sorted(observed-expected), "upstream_removed_files": sorted(expected-observed)}

def array(data):
    return "{" + ",".join("0x%02X" % value for value in data) + "}"

def generate(captures, output):
    import protolib
    lines = ["// Generated host-only test data; original YAML is the source of truth.", "#pragma once", "#include <cstdint>", "#include <cstddef>", "namespace LaberningOriginal {",
        "struct Frame { const char *capture; uint16_t index; bool tx,hasTime; uint32_t timeMs, frequency; uint16_t preamble; bool crc; int expectedCommand; uint8_t flagMask,flags; const uint8_t *bytes; uint8_t length; };",
        "struct Capture { const char *id,*radio,*origin,*keyMode,*upstreamOutcome; uint16_t totalFrames,nativeFrames; };",
        "struct Hmac { const char *capture; uint8_t transcript[32],length,challenge[6],mac[6]; };",
        "inline constexpr uint8_t publicCorpusKey[16]=" + array(protolib.CORPUS_SYSTEM_KEY) + ";"]
    rows, summaries, hmacs = [], [], []
    for entry, document in captures:
        identity = document["id"]
        frames = document["frames"]
        selected = sample_indices(frames)
        expectations = (document.get("expect") or {}).get("frames", [])
        for index in selected:
            frame = frames[index]
            raw = bytes.fromhex(frame["hex"])
            name = "wire_%s_%u" % (identity, index)
            lines.append("inline constexpr uint8_t %s[]=%s;" % (name, array(raw)))
            expected = expectations[index] if index < len(expectations) else {}
            mask = 0xE0 if {"start","end","protocol"} <= set(expected) else 0
            flags = (0x40 if expected.get("start") else 0) | (0x80 if expected.get("end") else 0) | (0x20 if expected.get("protocol")=="1w" else 0)
            rows.append("{%s,%u,%s,%s,%uu,%uu,%u,%s,%d,%u,%u,%s,sizeof(%s)}" % (json.dumps(identity), index, str(frame["dir"]=="tx").lower(), str("t_ms" in frame).lower(), frame.get("t_ms",0), frame.get("freq",0), preamble_from_note(frame.get("note","")), str(frame["crc"]=="present").lower(), expected.get("cmd",-1), mask, flags, name, name))
        outcome = ((document.get("expect") or {}).get("exchange") or {}).get("outcome", "unspecified")
        summaries.append("{%s,%s,%s,%s,%s,%u,%u}" % tuple([json.dumps(v) for v in [identity,entry["radio"],entry["origin"],document["key"],outcome]]+[len(frames),len(selected)]))
        if document["key"] == "corpus":
            original = [protolib.RawFrame(f["dir"], (bytes.fromhex(f["hex"])[:-2] if f["crc"]=="present" else bytes.fromhex(f["hex"])).hex()) for f in frames]
            parts = [protolib.hmac_parts(*triple) for triple in protolib.find_challenge_response_triples(original)]
            parts += [protolib.HmacParts(*protolib.self_auth_parts(f)) for f in protolib.find_self_authenticated_frames(original)]
            for part in parts:
                if len(part.transcript)>32:
                    raise ValueError("public HMAC transcript exceeds native test bound")
                hmacs.append("{%s,%s,%u,%s,%s}" % (json.dumps(identity),array(part.transcript),len(part.transcript),array(part.challenge),array(part.hmac)))
    lines += ["inline constexpr Frame frames[]={" + ",\n".join(rows) + "};", "inline constexpr Capture captures[]={" + ",\n".join(summaries) + "};", "inline constexpr Hmac publicHmacs[]={" + ",\n".join(hmacs) + "};", "}"]
    data = "\n".join(lines) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != data:
        output.write_text(data)
    return len(rows),len(hmacs)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", type=Path)
    parser.add_argument("--compare-upstream", type=Path)
    args = parser.parse_args()
    manifest, captures = load_captures()
    print("Pinned laberning corpus: %u original YAMLs, %u frames; hashes/schema/CRC/public-key safety passed" % (len(captures),sum(len(d["frames"]) for _,d in captures)))
    if args.compare_upstream:
        print(json.dumps(compare_upstream(manifest,args.compare_upstream),sort_keys=True))
    if args.generate:
        count,hmacs = generate(captures,args.generate)
        print("Host-only native vectors: %u sampled frames, %u independently verified public HMAC cases" % (count,hmacs))

if __name__ == "__main__":
    main()
