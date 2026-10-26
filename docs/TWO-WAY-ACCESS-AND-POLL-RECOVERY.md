# Two-way transmit admission and status recovery

2W START frames and unqualified non-START frames now enter one controller
admission path. Parsed frame bounds, durable identity, radio readiness and
per-band duty accounting apply to every send. Carrier sense measures the tuned
TX channel in receive state. Busy or invalid RSSI defers another sample by 5 ms;
five failed samples reject the candidate without transmitting. The existing
queued exchange deadline also bounds admission. A single 1 ms RX settling wait
is retained; this heuristic is not a qualified polite-access implementation.

A validated receive handler can grant one immediate reply for the same local
identity, peer, session and RX/TX frequency, for at most 25 ms from MCU packet
read. An accepted transmission consumes the grant; another packet invalidates
it. Validated ordinary and Atlantic authentication, pairing key transfer,
pull-key challenges and key-recovery replies use this path. START=0 or a 0x3D
opcode alone grants nothing. Qualified replies skip the extra RX restart and
carrier sense, retain duty admission and charge accepted airtime. Their trace
says `qualified_reply`, separately from bench bypass. The freshness bound is
local application policy, not a universal peer deadline or legal exception.

Tracked cover status requests have a concrete channel, key snapshot, identity
revision and fixed tracking deadline. Only an exchange that actually transmitted
can report a peer failure to that owner. A canceled identity or tracking epoch,
queue rejection, busy admission and duty deferral cannot advance a peer streak.
Manual reads and ETS periodic refresh do not enter the movement recovery ladder.
Duplicate destination node IDs do not transfer ownership to the first channel.

The first challenged-but-unanswered read retries after 5 seconds, then 30, 120
and 300 seconds. Silent misses use 5, 15, 30, 60 and 300 seconds. The last delay
repeats; counters saturate at 255. Switching failure class clears the other
streak. Valid correlated status clears both. Sleeping profiles retain their
10-second minimum. The original ten-minute tracking deadline is never renewed
by failure, and a retry at or beyond that deadline is suppressed. Duty deferrals
and queue pressure remain staggered across channels. STOP settling retains its
separate exchange budget. No EXECUTE command is replayed to read position.

`status_poll_interval` / the ETS polling interval controls normal cadence;
recovery delays apply only after a failed tracked read. Neither challenge nor
successful 0x3D transmission confirms movement or position. Uncorrelated 0x04
frames cannot publish position or reset the recovery history. Authenticated
spontaneous 0x71 updates keep their existing independent path.

Field provenance: [laberning #178](https://github.com/laberning/home_io_control/issues/178),
[#179](https://github.com/laberning/home_io_control/pull/179) and
[#180](https://github.com/laberning/home_io_control/pull/180), specifically
392e32a4219cb9bc65bbf516893568735f3ddea8. The observed OXIMO 50 io recovery
motivates the application ladder; it is not universal VELUX or Atlantic timing.
[Velocet link layer](https://github.com/Velocet/iown-homecontrol/blob/b7a526d07506f5192548aadfecf68d0f6e5497fa/docs/linklayer.md)
provides consistent session/authentication semantics, not that retry policy.

## Physical qualification: not_run

Use `two_way_access_recovery_campaign` in the release-evidence template. Retain
firmware SHA, board/radio, peer profile, redacted RF/logic traces and operator.
Test SX1276 with OXIMO 50 io and another Somfy motor, SX1262 with a compatible
2W peer, sleeping VELUX, and Atlantic pull/readback/Home-Away. Induce occupied
RF, strong/weak RX, missing final 0x04 and repeated wrong keys. Measure RX done,
packet read, TX request/start/done, RX ready and final reply, and verify physical
motion and position. MCU timestamps alone do not prove on-air turnaround.
Verify 5-second recovery, repeat ladders, multi-device fairness and per-band
airtime with debug/bench bypass off. Keep 1W, power-cut/storage and ETS upgrade
gates open until their physical campaigns pass.

The shipped duty-limited policy remains in force; no qualified polite-access
mode is enabled by this change. Applicable spectrum-access regime, chip settle
and CCA timing, short-reply conditions and strong-RX false-busy cause still
require conducted measurements and expert sign-off. No root cause or regulatory
compliance is inferred from synthetic tests. ETS application 4.7 and all
parameter and KO offsets are unchanged.

The trace ring defaults to 24 entries. `IOHC_TWO_WAY_TRACE_CAPACITY` can reduce
only diagnostic history (8–64 entries); request queues, protocol timing and
accounting are unaffected. OAM classic ESP32 IP release uses 16 entries because
the current dependency set already exceeds that board's static DRAM segment
with the unchanged OFM baseline. Compare actual linker margins in OAM validation.

Inbound 0x71 authentication and verified 0x72 replies now have separate,
identity-bound producer roles. See [Inbound status replies](INBOUND-STATUS-REPLIES.md)
for nonce lifetime, receive-channel-first ACK copies, mutation coverage and the
still-open RF capture/qualification gate.
