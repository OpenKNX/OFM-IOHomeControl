# RF/battery TODO implementation status — 2026-10-11

Source: supplied `todo (8).md`. Branches: v1dev-KLFDocumentation.
Pre-existing README/battery/wake and recovery documents were preserved.

## Implemented

- Central access policy separate from accounting; explicit DutyLimited,
  QualifiedPoliteAccess (unavailable), BenchUnlimited and SimulationUnlimited.
- Simulation restricted to TEST_NATIVE/RadioTestStub. Bench restricted to a
  separate debug build, local explicit confirmation, five-minute expiry,
  no persistence/web console, identity/error revocation and reset/off without
  ledger refunds. Ordinary firmware cannot select unlimited modes.
- All controller TX plus seven formerly direct module diagnostic calls use
  central admission and charge-on-accept. Existing LBT/authentication/frequency/
  persistence/ownership guards remain. Bypass TX logs airtime and session.
- Conservative production boot/brownout recovery: full 3636000 ms quiet period.
  No ordinary/ACK/pairing startup exceptions. RX remains available.
- Per-band used/limit/current/previous/next airtime, blocked central-attempt
  counters, reserved=0 and conservative earliest-eligible delay diagnostics.
- Battery queue/RF completion/reply distinctions and terminal duty results with
  needed/remaining airtime, retry delay, last attempt and identity-scoped state.
- Individual maintenance object states, manufacturer-specific sequence,
  deterministic stopping on exhausted quota and explicit partial-exchange state.
- Tracked/periodic status-poll backoff before enqueue/logging, staggered across
  channels, retaining STOP/motion/authentication and historical battery rules.
- Existing one-shot battery commands work with cyclic ETS polling disabled.
- Regression checks for bands, windows, rollover, uint32 wrap, saturation,
  recovery, production/debug gates, repeated simulated accepts, no refund,
  duty-blocked probes/objects, identity invalidation and actual poll scheduling.
- Separate SX1262/SX1276 bench environments and application dependency pin.

## Verified in software

Native suite: 712 cases. Policy wrapper compiles production, simulation and bench
variants and rejects production/web-console bypass builds. Existing protocol,
controller, exchange, radio, wake, UI, battery and import identity checks pass.
Firmware compiled and linked for normal SX1262, production SX1276 and both
bench profiles. No firmware was uploaded and no repository was pushed.

## Deliberately pending external qualification

The higher QualifiedPoliteAccess mode and 1% CH2 fairness target **are not enabled**.
Existing helper LBT is not proof that all direct/queued/time-critical paths meet
a polite-access scheme. Selection fails closed to the unchanged normal policy.

Pending work needs an identified current device/region/category and approved
access requirements: CCA threshold/minimum listen time, sensor-failure behavior,
busy backoff, occupancy/TX-time limits, channel agility/AFA, power/ERP and exact
response-timing constraints. The supplied TODO does not provide a qualified
profile that can safely be implemented as a production default.

Hardware work remains: SX1276/SX1262 airtime and long-preamble/burst measurements,
spectrum/antenna power, CCA qualification for every TX type, auth/ACK turnaround,
physical three-channel polling/battery trials, shielded bench approval and
Austrian/regulatory release review. Native tests cannot replace these checks.
See [RF policy](rf-access-policy.md) and [battery outcomes](battery-rf-outcomes.md).

Receive faults also revoke temporary RF bypasses; ordinary radio Busy responses retain them. A native regression verifies that charged airtime remains intact.
