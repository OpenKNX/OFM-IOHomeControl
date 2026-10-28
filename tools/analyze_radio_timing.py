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
          "payload_isr", "packet_read", "packet_parsed"]
INTERVALS = {"isr_to_observer_us": ("tx_done_isr", "controller_done"),
             "observer_to_rx_write_us": ("controller_done", "rx_mode_written"),
             "isr_to_rx_write_us": ("tx_done_isr", "rx_mode_written"),
             "poll_to_rx_write_us": ("tx_done_poll", "rx_mode_written"),
             "standby_us": ("standby_begin", "standby_end"),
             "rx_setup_us": ("rx_setup_begin", "rx_return")}


def analyze(lines):
    groups, segment, drops = {}, 0, 0
    for line in lines:
        if "IOHC_TIMING compiled=" in line:
            segment += 1
            match = re.search(r"dropped=(\d+)", line)
            drops += int(match[1]) if match else 0
        if "IOHC_TIMING_CSV " not in line:
            continue
        values = [int(v) for v in line.split("IOHC_TIMING_CSV ", 1)[1].strip().split(",")]
        if len(values) not in (8, 9) or not 0 <= values[4] < len(EVENTS):
            raise ValueError("invalid timing row")
        us, transaction, tx_id, attempt, event, tx_hz, rx_hz, state = values[:8]
        key = (segment, transaction, tx_id, attempt)
        group = groups.setdefault(key, {"segment": segment, "transaction": transaction,
            "tx_id": tx_id, "attempt": attempt, "events": {}, "tx_hz": tx_hz})
        group["events"].setdefault(EVENTS[event], us)
    rows, samples = [], {name: [] for name in INTERVALS}
    for group in groups.values():
        events = group.pop("events")
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
    return {"time_source": "MCU micros; ISR entry is not physical pin edge",
        "physical_edge_to_isr": "unavailable; requires synchronized hardware capture",
        "packet_association": "observational receive window, not peer/authentication proof",
        "trace_drops": drops, "attempts": rows, "summary": summary}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    args = parser.parse_args()
    with open(args.capture) as stream:
        print(json.dumps(analyze(stream), indent=2))
