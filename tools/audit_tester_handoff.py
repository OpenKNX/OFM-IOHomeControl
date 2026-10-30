#!/usr/bin/env python3
"""Read the 2026-10-30 tester handoff; emit metadata only, never raw RF secrets."""
import argparse
import hashlib
import json
import re
import zipfile


def execute_summary(text):
    sessions, target, active, role = {}, None, None, None
    for number, line in enumerate(text.splitlines(), 1):
        match = re.search(r"(?:iohc02 send |IoHC\[2\]:\s+Send position )(\d+)", line)
        if match:
            target = int(match[1])
        match = re.search(r"RF admission frame_role=(\d+)", line)
        if match:
            role = int(match[1])
        match = re.search(r"2W accepted localTX=(\d+) session=(\d+) attempt=(\d+)", line)
        if match and target is not None:
            tx, active, attempt = map(int, match.groups())
            session = sessions.setdefault(active, {"session": active, "target_percent": target,
                "request_attempts": [], "answer_attempts": [], "complete": False, "lines": []})
            attempts = session["answer_attempts"] if role == 3 else session["request_attempts"]
            if attempt not in attempts:
                attempts.append(attempt)
                session["lines"].append(number)
        if active in sessions and "2W result cmd=0x00 result=transport_complete" in line and all(
                token in line for token in ("authenticated_exchange=yes", "challenge_response=yes", "final_response=yes")):
            sessions[active]["complete"] = True
            sessions[active]["completed_attempt"] = attempt
            sessions[active]["lines"].append(number)
    rows = list(sessions.values())
    return {"user_exchanges": len(rows), "complete": sum(r["complete"] for r in rows),
        "first_attempt_complete": sum(r.get("completed_attempt") == 1 for r in rows),
        "physical_movement": "user observations remain separate; not inferred", "sessions": rows}


def audit(path):
    with zipfile.ZipFile(path) as archive:
        identities = [{"path": n, "sha256": hashlib.sha256(archive.read(n)).hexdigest(),
            "bytes": len(archive.read(n))} for n in archive.namelist() if not n.endswith("/")]
        logs = {n: archive.read("tester-logs/"+n).decode("utf-8", errors="replace")
            for n in ("1_neuefirmware.txt", "2_neuefirmware.txt", "3_neuefirmware.txt", "IoHomecontrol (3).txt")}
    rows = [list(map(int, l.split("IOHC_TIMING_CSV ",1)[1].split(",")))
        for l in logs["3_neuefirmware.txt"].splitlines() if "IOHC_TIMING_CSV " in l]
    drops = re.search(r"IOHC_TIMING compiled=.*?dropped=(\d+)", logs["3_neuefirmware.txt"])
    return {"identities": identities, "execute_155D81_new_build": execute_summary(logs["2_neuefirmware.txt"]),
        "timing_scan_flood": {"rows": len(rows), "all_unowned": all(r[1:4] == [0,0,0] for r in rows),
            "events": sorted({r[4] for r in rows}), "dropped": int(drops[1]) if drops else None,
            "retained_span_us": rows[-1][0]-rows[0][0] if rows else None,
            "physical_tx_to_rx_latency": "unavailable"},
        "historical_log": "IoHomecontrol (3).txt: E50470/older build, excluded from new-build totals"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    print(json.dumps(audit(parser.parse_args().archive), indent=2))
