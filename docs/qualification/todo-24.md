# TODO 24: SX1276 peer and waveform qualification

Status: **not physically performed**. Required equipment/application access is not available in this session.

Use RF capture plus logic-analyzer TX/IRQ/SPI edges. Record preamble/sync/airtime, TX-end to first/final response, RSSI, AFC/frequency error, scan revisit, missed packets and IRQ latency. Test 3/4/5 ms scan and 3/5 s supervision separately; choose production values only from measurements.

- [ ] `normal_2w` — Send a normal command to an original actuator; correlate authenticated reply and physical action.
- [ ] `low_power_wake` — Repeat from sleeping low-power state; retain wake-prefix and response traces.
- [ ] `actuator_discovery` — Capture 28/29 with at least three responders; retain all source identities and duplicates.
- [ ] `spe_discovery` — Capture 2A/2B with enrolled peers and nonresponding known nodes; verify known nodes survive.
- [ ] `group_timing` — Measure from actual TX-end: 1353 ms normal and 3000 ms low-power windows.
- [ ] `directed_timing` — Exercise class-2 811 ms and class-3 1011 ms replies near the deadline.
- [ ] `challenge_key` — Capture 3C/3D and 31/32/33; retain redacted transcript and independent key-match result.
- [ ] `version3` — Exercise the version-3 0B 01 path on a confirmed version-3 original peer.
- [ ] `oneway` — Exercise command, enrollment and optional trailer MAC with an original 1W receiver.
- [ ] `crc_fifo` — Inject bad CRC and FIFO overrun; verify rejected packets and recovery without state loss.
- [ ] `scan_hold` — Drive preamble/sync during retune; measure successful captures versus generated frames.
- [ ] `interference_lbt` — Apply controlled interference, record channel occupancy, deferral and retries.
- [ ] `fault_restore` — Force radio fault/reset; verify RX restoration and unchanged keys/counters; no command replay.

Evidence record: `sx1276_peer_campaign` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
