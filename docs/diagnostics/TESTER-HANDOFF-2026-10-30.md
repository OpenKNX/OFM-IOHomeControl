# Tester handoff: verified results and corrections

This pass uses the five files in `OFM-Tester-Agent-Handoff-2026-10-30.zip`.
The raw logs are not committed: the [metadata audit](evidence/tester-handoff-2026-10-30.json)
contains file identities, source line references and sanitized transaction counts.
Reproduce it with `python3 tools/audit_tester_handoff.py /path/to/handoff.zip`.
The audit never emits keys, challenges, MACs or complete RF bodies.

## Evidence checked

- New firmware log2 contains eight user Execute exchanges for155D81. Six log a
  transport-complete result with challenge answer and final response; three
  complete on the first accepted RF attempt. These are protocol outcomes,
  separately from the tester's physical movement observations.
- Session4's first75% action has three accepted requests without a challenge.
  Session7's25% action sends an answer, lacks a final reply, then has one silent
  targeted resend. Those are distinct failure families. Missing04 proves neither
  refusal nor successful motor operation.
- Log3's64timing rows all have transaction/TX/attempt zero and events12–15.
  They span80,593microseconds and report24,649dropped events: a confirmed
  background-scan flood, not a measurement of TX_DONE/RX latency.
- New log1's foreign-read/qualified-write/coalescing counters support the earlier
  filtering correction in the observed run. They do not prove every HA action
  was transmitted or heard. The historical `IoHomecontrol (3).txt` is a different
  shutter/build (E50470) and is excluded from the new155D81totals.
- The files are separate captures. Their differing RF modes do not establish
  that `duty debug off` silently reversed in one boot. The user explicitly requires unlimited RF admission throughout debug builds;
  differences between captures do not establish a within-boot mode reversal.

## Implemented

1. **Bounded timing ownership.** Armed scanning consumes no entries; the next
   accepted TX opens one RF-attempt capture. Its same-owner/same-attempt3D may
   add a second TX ID. A terminal observation freezes it, as does a new unrelated
   TX or10-second diagnostic cap. Later retries/transactions cannot overwrite it.
   Scan exclusions and in-scope drops are separate. ISR/task RAM writes retain
   the existing portMUX; ignored scans do not enter that lock or read the clock.
2. **Build RF policy.** Per the user's explicit correction, OPENKNX_DEBUG builds
   always bypass RF duty, packet-airtime, CCA/LBT and application quotas, including
   after off/reset/reboot, identity changes and radio faults. Release builds retain
   DutyLimited admission. Accounting, authentication, collisions, hardware errors
   and protocol/session bounds remain enforced. Simulation alone does not grant
   release builds the debug exemption.
3. **Stage telemetry.** `IOHC_ATTEMPT` and retained trace outcomes distinguish
   no-correlated3C, answer-sent/missing04, correlated04, unmatched/CRC observations,
   local admission and radio errors. Transaction/attempt/TX ID, request identity,
   preamble/frequency, IRQ/state and challenge/answer flags accompany observations.
   Existing request snapshots retain control bytes, wake belief and deadline.
   Physical peer transmission/movement stays unknown without independent evidence.
4. **Paused scheduler.** Tracked/periodic channel polling stops before the request
   path while paused, preserving due times and failure streaks. Expiry/ownership
   checks still run. Explicit manual polls retain their existing path; STOP-settle
   retry policy remains unchanged and ordinary background pause still applies.
5. **Parser.** Unowned scan rows cannot become latency statistics. Conflicting
   IDs, missing phases, absent outcomes and interrupted captures are partial.
   Request/auth TX IDs group under their logical owner, and missing physical/
   optional-pin evidence remains unavailable rather than zero.

No RF waveform, request body, cryptography, preamble, numeric timeout or retry
budget was changed. The existing one-resend policy after an ambiguous Execute
still requires a known confirming peer, a repeatable target and the bounded
session budget. New source regressions retain unknown device acceptance when
the final reply is missing. The original-controller evidence does not establish
VELUX actuator refusal or safe universal replay; no new protocol patch is made
on that basis. Further REA/physical work is needed only if corrected captures
expose a protocol anomaly.

## Validation and integration

Native coverage includes65seconds of5ms scans before TX and after the outcome,
auth follow-on IDs, repeated edges/polls, IRQ-only completion, missing optional
pins, wrap, exhaustion, ownership rollover, diagnostic expiry and a serialized
ISR-writer/task-reader model. The latter checks the portMUX contract; it does
not emulate real interrupt latency. Admission tests cover boot/reset/reboot/off,
identity/collision paths and changed build flags without ledger refunds. Real channel
methods are extracted for pause/manual/resume regression, alongside the existing
controller queue/epoch tests and pinned RF corpus.

Full OAM integration uses the actual manifest pins, including Logic6fe479e and
Common3e481ed. The prior shared checkout versions were newer and mismatched
knxprod.h; no Logic/FunctionBlocks exclusion or generated holiday-hook workaround
is required with the pinned dependencies. Validate timing-enabled SX1276,
timing-disabled release SX1276 and SX1262 separately. ETS4.9, flash schema18,
parameters/KO layout and pairing persistence remain unchanged.

No firmware was flashed and no new RF/analyzer measurement was performed.
Physical edge-to-ISR delay, RX acquisition, peer silence versus missed reply,
motor acceptance and instrumentation overhead remain unqualified. Do not infer
those values from the historical scan-only trace or synthetic timing tests.

## Next tester command sheet

Use OAM environment `timing_OpenKNX_ESP32_S3_DEV_SX1276_IP` and confirm its OFM pin
in the boot diagnostic. Verify the actual board's wiring. Debug builds remain unlimited even after
`iohc duty debug off`; the command retains accounting and does not enable limits.
Then use one owned shutter155D81/channel2, with laberning off initially:

```
iohc keyimport diag
iohc duty debug off
iohc duty status
iohc polls pause 300
iohc radio timing on
```

Perform one already authorized status read through a supported ETS/KNX path,
or one safely supervised Execute such as `iohc02 send 75`. `iohc02 status` only
displays state. Do not create a high-volume loop or use bench bypass outdoors.
The recorder captures the first RF attempt and its auth answer, not every retry.
When the controller is idle:

```
iohc radio timing dump
iohc duty status
```

Save CSV and outcome logs. Expect nonzero TX IDs, explicit outcome, and zero
in-scope drops; ignored scan counts may be large. Re-arm separately for another
attempt, with supervised pacing. Independent DIO0/SPI and RF capture is
needed to distinguish a delayed MCU transition from a silent/missed peer reply.
Finish with `iohc radio timing off`, `iohc keyimport capture off`,
`iohc polls resume` and `iohc duty status`. No2E02,6F, re-pairing or key changes
are part of this test.
