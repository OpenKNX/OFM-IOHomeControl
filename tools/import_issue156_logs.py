#!/usr/bin/env python3
"""Offline RS100 log import: preserve provenance, never publish auth/key bodies.

The source file is hashed byte-for-byte. This tool neither edits nor downloads it.
Its JSON is a derivative: only reviewed non-authenticated 2W response commands and
four reviewed identity/capability objects retain bytes. Metadata-only rows preserve
observations of omitted bodies. No packets, timestamps or missing chunks are inferred.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

SAFE_COMMANDS = {0x47, 0x55, 0x57, 0x71}
SAFE_OBJECTS = {0x8001, 0x8002, 0x8100, 0x8103}
TIME = re.compile(r"\[(\d\d:\d\d:\d\d\.\d{3})\]")
CAPTURE = re.compile(r"\bio_capture:\d+\].*?\bstage=(\w+).*?\bfreq=(\d+).*?\bts=(\d+).*?\blen=(\d+)(.*)")
FIELD = re.compile(r"\b(cmd|src|dst)=((?:0x)?[0-9A-Fa-f]+)\b")
HEX = re.compile(r"\bpayload=((?:[0-9A-Fa-f]{2}(?:\s+|$))+)" )
SUMMARY = re.compile(r"read_object ([0-9A-Fa-f]{6}) object 0x([0-9A-Fa-f]{4}): (\d+) of (\d+) bytes(.*)")
DUMP = re.compile(r":\s+\+([0-9A-Fa-f]{3,4}):\s+((?:[0-9A-Fa-f]{2}(?:\s+|$))+)")
ATTEMPT = re.compile(r"Try (\d+) (answered|ended):.*?cmd=[A-Z0-9_]+\(0x([0-9A-Fa-f]{2})\)(.*)")
CHALLENGE = re.compile(r"Auth challenge try=(\d+).*?req_cmd=0x([0-9A-Fa-f]{2})")
SENSITIVE_TEXT = re.compile(r"(?i)(?:system.?key|encryption.?key|enc.?key|(?<!\w)key\s*[:=]|hmac\s*[:=]|password\s*[:=]|token\s*[:=])")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def timestamp(line):
    found = TIME.search(line)
    return found.group(1) if found else None


def classification(command, data):
    if command != 0x71:
        return {0x47: "transfer_open", 0x4B: "transfer_chunk", 0x55: "identity_info1", 0x57: "identity_info2"}.get(command, "opaque")
    if len(data) < 5 or data[4] != len(data) - 5:
        return "invalid_event_envelope"
    return "position_event" if data[2:4] == b"\x10\x0a" and len(data) >= 11 else "device_event" if data[2:4] != b"\x10\x0a" else "invalid_event_envelope"


def import_log(raw, filename, url, published_at=None, capture_date=None):
    if len(raw) > 8 * 1024 * 1024:
        raise ValueError("input exceeds the bounded eight-MiB importer limit")
    lines = raw.decode("utf-8", errors="strict").splitlines()
    output = {"schema": 1, "source": {"filename": filename, "url": url,
        "sha256": sha256(raw), "bytes": len(raw), "line_count": len(lines),
        "published_at": published_at, "capture_date": capture_date,
        "clock_note": "Log clock and uptime retained literally; no date/timebase inference."},
        "privacy": {"original_retained_separately": True, "body_omissions": {}, "credential_label_lines": []},
        "timeline": [], "objects": []}
    omitted = output["privacy"]["body_omissions"]
    def omission(reason):
        omitted[reason] = omitted.get(reason, 0) + 1
    for number, line in enumerate(lines, 1):
        if SENSITIVE_TEXT.search(line):
            output["privacy"]["credential_label_lines"].append(number)
        common = {"line_start": number, "line_end": number, "clock": timestamp(line)}
        capture = CAPTURE.search(line)
        if capture:
            stage, frequency, uptime, reported_length, tail = capture.groups()
            row = dict(common, kind="frame", direction="rx_reported", stage=stage,
                       frequency_hz=int(frequency), uptime_ms=int(uptime), reported_length=int(reported_length),
                       crc="not_present_in_log", confidence="captured_log_observation")
            fields = {key: int(value, 16) for key, value in FIELD.findall(tail)}
            for key, value in fields.items():
                row[{"cmd": "command", "src": "source", "dst": "destination"}[key]] = value
            found = HEX.search(tail)
            wire = bytes.fromhex(found.group(1)) if found else b""
            reason = None
            if stage == "parse_fail": reason = "parse_failure_unreviewed_body"
            elif len(wire) < 9 or len(wire) != int(reported_length): reason = "masked_or_incomplete_payload"
            elif wire[0] & 0x20: reason = "one_way_authentication_body"
            elif wire[1] != 0: reason = "unreviewed_control_flags_body"
            elif wire[8] == 0x4B: reason = "unreviewed_transfer_chunk_body"
            elif wire[8] not in SAFE_COMMANDS: reason = "authentication_key_or_unreviewed_command_body"
            elif not {"cmd", "src", "dst"} <= fields.keys(): reason = "incomplete_metadata"
            elif fields["cmd"] != wire[8] or fields["src"] != int.from_bytes(wire[5:8], "big") or fields["dst"] != int.from_bytes(wire[2:5], "big"):
                raise ValueError(f"line {number}: wire and printed metadata disagree")
            elif (wire[0] & 0x1F) + 1 != len(wire):
                raise ValueError(f"line {number}: declared wire length disagrees")
            if reason:
                row["body_omitted"] = reason
                omission(reason)
            else:
                row["hex"] = wire.hex(" ").upper()
                row["payload_hex"] = wire[9:].hex(" ").upper()
                row["parser_class"] = classification(wire[8], wire[9:])
            output["timeline"].append(row)
            continue
        summary = SUMMARY.search(line)
        if summary:
            node, key, reported, declared, suffix = summary.groups()
            key, reported, declared = int(key, 16), int(reported), int(declared)
            data, last = bytearray(), number
            for offset in range(number, len(lines)):
                dump = DUMP.search(lines[offset])
                if not dump: break
                start, chunk = int(dump.group(1), 16), bytes.fromhex(dump.group(2))
                if start != len(data):
                    raise ValueError(f"line {offset + 1}: object dump gap, duplicate or out-of-order offset")
                data.extend(chunk); last = offset + 1
            if len(data) != reported or len(data) > declared:
                raise ValueError(f"line {number}: object dump byte count disagrees with summary")
            obj = dict(common, line_end=last, node=int(node, 16), object_id=key,
                       reported_length=reported, declared_length=declared, available_length=len(data),
                       complete=(len(data) == declared and "incomplete" not in suffix.lower()),
                       confidence="captured_object_dump_no_chunk_packets_inferred")
            if key in SAFE_OBJECTS:
                obj["hex"] = bytes(data).hex(" ").upper()
                obj["sha256"] = sha256(data)
            else:
                obj["body_omitted"] = "unreviewed_object"; omission("unreviewed_object")
            output["objects"].append(obj)
            output["timeline"].append(dict(common, kind="object_summary", object_id=key,
                                          declared_length=declared, available_length=len(data), complete=obj["complete"]))
            continue
        attempt = ATTEMPT.search(line)
        challenge = CHALLENGE.search(line)
        if attempt or challenge:
            if attempt:
                attempt_number, outcome, command, _ = attempt.groups()
                row = dict(common, kind="exchange_summary", command=int(command, 16),
                           attempt=int(attempt_number), outcome=outcome, confidence="message_level_not_packet")
            else:
                attempt_number, command = challenge.groups()
                row = dict(common, kind="exchange_summary", command=int(command, 16),
                           attempt=int(attempt_number), outcome="challenge_reported", confidence="message_level_not_packet")
            for name in ["wait_ms", "preamble", "age_ms", "req_len"]:
                found = re.search(r"\b" + name + r"=(\d+)", line)
                if found: row[name] = int(found.group(1))
            found = re.search(r"within (\d+) ms", line)
            if found: row["timeout_ms"] = int(found.group(1))
            output["timeline"].append(row)
    return output


def generate_header(document):
    def array(name, value):
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]{0,63}", name):
            raise ValueError("invalid generated C++ symbol")
        wire = bytes.fromhex(value)
        return "inline constexpr uint8_t " + name + "[]={" + ",".join(f"0x{x:02X}" for x in wire) + "};"
    for frame in document["frames"]:
        wire = bytes.fromhex(frame["hex"])
        if len(wire) < 9 or wire[0] & 0x20 or wire[1] != 0 or wire[8] not in SAFE_COMMANDS or (wire[0] & 31) + 1 != len(wire):
            raise ValueError("unreviewed/authentication frame cannot enter generated corpus")
        if frame["command"] != wire[8] or frame["source"] != int.from_bytes(wire[5:8], "big") or frame["destination"] != int.from_bytes(wire[2:5], "big"):
            raise ValueError("fixture metadata does not match captured bytes")
        if frame.get("reported_length", len(wire)) != len(wire) or frame.get("parser_class", classification(wire[8], wire[9:])) != classification(wire[8], wire[9:]):
            raise ValueError("fixture length/classification does not match captured bytes")
    for payload in document["payloads"]:
        data = bytes.fromhex(payload["hex"])
        if payload["command"] != 0x71 or classification(0x71, data) not in {"device_event", "position_event"}:
            raise ValueError("unreviewed DATA-only payload")
    for obj in document["objects"]:
        data = bytes.fromhex(obj["hex"])
        if obj["object_id"] not in SAFE_OBJECTS or len(data) != obj["available_length"] or len(data) > obj["declared_length"] or len(data) > 1024:
            raise ValueError("unreviewed or malformed object fixture")
        if obj["complete"] != (len(data) == obj["declared_length"]):
            raise ValueError("fixture completion must reflect actual bytes")
        if obj.get("sha256", sha256(data)) != sha256(data):
            raise ValueError("object fixture byte digest changed")
    rows = ["// Generated from issue156_rs100_fixtures.json; no authentication material.", "#pragma once", "#include <cstdint>", "namespace Issue156RS100 {",
            "struct Frame {const char *id;const uint8_t *wire;uint8_t length;uint32_t frequency;uint8_t command;uint32_t source,destination;};",
            "struct Object {uint16_t id,declaredLength,availableLength;bool complete;const uint8_t *data;};"]
    for frame in document["frames"]: rows.append(array(frame["symbol"], frame["hex"]))
    for payload in document["payloads"]: rows.append(array(payload["symbol"], payload["hex"]))
    for obj in document["objects"]: rows.append(array(obj["symbol"], obj["hex"]))
    rows.append("inline constexpr Frame frames[]={")
    for f in document["frames"]:
        rows.append("{" + json.dumps(f["id"]) + f",{f['symbol']},sizeof({f['symbol']}),{f['frequency_hz']}U,0x{f['command']:02X},0x{f['source']:06X},0x{f['destination']:06X}" + "},")
    rows += ["};", "inline constexpr Object objects[]={"]
    for obj in document["objects"]:
        rows.append(f"{{0x{obj['object_id']:04X},{obj['declared_length']},{obj['available_length']},{str(obj['complete']).lower()},{obj['symbol']}}},")
    return "\n".join(rows + ["};", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--source-url")
    parser.add_argument("--published-at")
    parser.add_argument("--capture-date")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generate-header", action="store_true", help="input is the reviewed selected fixture JSON")
    args = parser.parse_args()
    if args.generate_header:
        args.output.write_text(generate_header(json.loads(args.input.read_text())))
    else:
        if not args.source_url: parser.error("--source-url is required for log import")
        document = import_log(args.input.read_bytes(), args.input.name, args.source_url, args.published_at, args.capture_date)
        args.output.write_text(json.dumps(document, indent=2) + "\n")

if __name__ == "__main__":
    main()
