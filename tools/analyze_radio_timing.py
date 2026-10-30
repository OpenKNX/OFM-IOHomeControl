#!/usr/bin/env python3
"""Summarize MCU-only IOHC_TIMING_CSV captures. Never infer peer reception."""
import argparse
import json
import re
import statistics

EVENTS = ["tx_start", "first_poll", "tx_done_isr", "tx_done_poll", "completion_entry",
          "irq_read", "standby_begin", "standby_end", "completion_return", "controller_check",
          "controller_done", "controller_rx_begin", "rx_setup_begin", "rx_registers_done",
          "rx_mode_written", "rx_return", "controller_rx_return", "preamble_isr", "sync_isr",
          "payload_isr", "packet_read", "packet_parsed", "no_3c_after_00",
          "3c_seen_3d_sent_no_04", "04_received", "rx_crc_error", "rx_unmatched",
          "rf_admission_deferred", "tx_or_rx_reentry_error", "attempt_end", "capture_expired",
          "peer_rejected", "response_timeout", "capture_stopped"]
INTERVALS = {"isr_to_observer_us": ("tx_done_isr", "controller_done"),
             "observer_to_rx_write_us": ("controller_done", "rx_mode_written"),
             "isr_to_rx_write_us": ("tx_done_isr", "rx_mode_written"),
             "poll_to_rx_write_us": ("tx_done_poll", "rx_mode_written"),
             "standby_us": ("standby_begin", "standby_end"),
             "rx_setup_us": ("rx_setup_begin", "rx_return")}


def analyze(lines):
    groups, segment, drops, unowned, ignored = {}, 0, 0, 0, 0
    capabilities, segment_drops = {}, {}
    for line in lines:
        if "IOHC_TIMING compiled=" in line:
            segment += 1
            match = re.search(r"dropped=(\d+)", line)
            segment_drops[segment] = int(match[1]) if match else 0
            drops += segment_drops[segment]
            match = re.search(r"ignored_scan_events=(\d+)", line)
            ignored += int(match[1]) if match else 0
            capabilities[segment] = {name: (re.search(name+r"=(\S+)", line)[1] if re.search(name+r"=(\S+)", line) else "unknown")
                for name in ("preamble_isr", "sync_isr", "capture_state")}
        if "IOHC_TIMING_CSV " not in line:
            continue
        values = [int(v) for v in line.split("IOHC_TIMING_CSV ", 1)[1].strip().split(",")]
        if len(values) not in (8, 9) or not 0 <= values[4] < len(EVENTS):
            raise ValueError("invalid timing row")
        us, transaction, tx_id, attempt, event, tx_hz, rx_hz, state = values[:8]
        if tx_id == 0:
            unowned += 1
            continue
        key = (segment, transaction, tx_id, attempt)
        group = groups.setdefault(key, {"segment": segment, "transaction": transaction,
            "tx_id": tx_id, "attempt": attempt, "events": {}, "tx_hz": tx_hz})
        group["events"].setdefault(EVENTS[event], us)
    rows, samples, captures = [], {name: [] for name in INTERVALS}, {}
    ids = {}
    for group in groups.values():
        events = group.pop("events")
        terminal = [name for name in EVENTS[22:] if name in events]
        known = [name for name in terminal if name not in ("rx_crc_error", "rx_unmatched", "attempt_end", "capture_expired", "capture_stopped")]
        group["outcomes"] = known
        group["capture_end"] = terminal[-1] if terminal else "unavailable"
        group["logical_context"] = "known" if group["transaction"] and group["attempt"] else "unknown"
        group["missing_phases"] = [name for name in ("tx_start","completion_return","rx_mode_written") if name not in events]
        if not ("tx_done_isr" in events or "tx_done_poll" in events):group["missing_phases"].append("tx_done_evidence")
        group["record_status"] = "complete" if not group["missing_phases"] else "partial_or_unmatched"
        owner=(group["segment"],group["transaction"],group["attempt"])
        owners=ids.setdefault((group["segment"],group["tx_id"]),set());owners.add(owner)
        cap=captures.setdefault(owner,{"segment":group["segment"],"transaction":group["transaction"],"attempt":group["attempt"],"tx_ids":[],"outcomes":[],"missing_phases":False,"unknown_owner":not(group["transaction"] and group["attempt"])})
        cap["tx_ids"].append(group["tx_id"]);cap["outcomes"].extend(known)
        cap["missing_phases"] |= bool(group["missing_phases"])
        group["preamble_timestamp"] = "observed" if "preamble_isr" in events else "unavailable"
        group["sync_timestamp"] = "observed" if "sync_isr" in events else "unavailable"
        group["physical_authorization_or_actuation"] = "unknown"
        group["completion_source"] = "dio0_isr_entry" if "tx_done_isr" in events else (
            "irq_poll_only" if "tx_done_poll" in events else "unavailable")
        for name, (start, end) in INTERVALS.items():
            delta = ((events[end] - events[start]) & 0xFFFFFFFF) if start in events and end in events else None
            if delta is not None and delta >= 0x80000000:
                delta = None  # wrong ordering or ambiguous clock interval
            group[name] = delta
            if delta is not None:
                samples[name].append(delta)
        rows.append(group)
    summary = {}
    for name, values in samples.items():
        if not values:
            summary[name] = {"count": 0, "status": "unavailable"}
            continue
        values.sort()
        summary[name] = {"count": len(values), "min": values[0],
            "median": statistics.median(values), "max": values[-1]}
        if len(values) >= 20:
            summary[name]["p95"] = values[(95 * len(values) + 99) // 100 - 1]
    conflicts=[{"segment":key[0],"tx_id":key[1]} for key,owners in ids.items() if len(owners)>1]
    for cap in captures.values():
        cap["status"]="complete" if cap["outcomes"] and not cap["missing_phases"] and not cap["unknown_owner"] and not segment_drops.get(cap["segment"],0) and not any(c["segment"]==cap["segment"] for c in conflicts) else "incomplete"
    return {"time_source": "MCU micros; ISR entry is not physical pin edge",
        "physical_edge_to_isr": "unavailable; requires synchronized hardware capture",
        "packet_association": "observational receive window, not peer/authentication proof",
        "trace_drops": drops, "ignored_scan_events": ignored, "unowned_rows": unowned,
        "capture_status": "no_owned_tx" if not rows else "incomplete" if any(c["status"] != "complete" for c in captures.values()) else "complete",
        "id_conflicts":conflicts,"captures":list(captures.values()),
        "pin_capabilities": capabilities, "attempts": rows, "summary": summary}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    args = parser.parse_args()
    with open(args.capture) as stream:
        print(json.dumps(analyze(stream), indent=2))
