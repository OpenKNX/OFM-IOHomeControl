# RF access policy and qualification gates

Implemented software policy, 2026-10-11. Hardware and regulatory qualification
are pending. This document does not certify a higher production RF allowance.

## Modes and accounting

`DutyLimited` is the production default. Existing allowances remain 3600 ms
for 868.7–869.2 MHz and 36000 ms for each other supported band (868.0–868.6 and
869.7–870 MHz). Unsupported frequencies and packets over 36000 ms are rejected.
All channels in one sub-band share one ledger. Two aligned buckets deliberately
retain more history than an exact rolling hour. Every accepted TX is charged
once, including retries, ACKs, authentication replies and RF diagnostics; no
refund is made for aborts or lost TX_DONE. Counters saturate instead of wrapping.

Production boot/reinitialization starts a **3636000 ms quiet recovery period**
(one hour plus maximum packet tail). RX still operates. This conservative uptime
policy covers unknown pre-reset RF history without flash wear or trusted wall
clock; reboot/brownout starts another full wait. It affects pairing and normal
commands as well as background polls. There is no startup exception for ACKs.

`QualifiedPoliteAccess` is represented but **cannot be selected**. The prospective
36000 ms CH2 fairness target is not an enabled/legal duty limit. The console
refuses qualification requests and retains DutyLimited. There is no free-form
percentage, ETS setting or persistence switch to enable a higher allowance.

`SimulationUnlimited` exists only with `TEST_NATIVE`, whose Radio type is the
non-emitting RadioTestStub. Admission can exceed quota while the same ledger
continues charging. Normal firmware cannot select this mode.

`BenchUnlimited` requires IOHC_ENABLE_RF_BENCH_UNLIMITED in a separate
OPENKNX_DEBUG build. Compilation fails in production or with OPENKNX_WEBCONSOLE.
Use only a shielded/conducted authorized RF lab setup. It is disabled at boot,
not saved, expires after 300000 ms (wrap-safe), and is revoked on identity/key
change, collision and observed radio error. Watchdog reboot also disables it.
Authentication, key ownership, configured frequency, radio power/setup, existing
LBT checks and timeout/correlation rules are retained. Accounting is never reset.

Local console only (KNX diagnostic commands cannot enable it):

```
iohc duty status
iohc duty limits
iohc duty mode status
iohc duty debug on CONFIRM_SHIELDED_BENCH
iohc duty debug off
iohc duty reset-debug
```

Bare `debug on` refuses until explicit confirmation. The status reports mode,
used/limit/current/previous bucket milliseconds, blocked count, latest required
TX airtime, reservedMs=0, session ID and conservative earliestEligibleMs as a
**delay**, including recovery. These are estimates, not promises of exact rolling
hour availability. There are no reservations: acceptance is authoritative.
Bypassed transmissions log frequency, preamble, size, airtime and charged total,
benchBypass/simulationBypass and session ID. Off/reset-debug retain all history.

## TX access audit and remaining qualification

All hardware TX calls in module diagnostics now go through
startDiagnosticTransmit -> startControllerTransmit. Queued 1W/2W, discovery,
pairing/key extraction, ACK/status responses and challenge 0x3D also use the
central admission/accounting point. isDutyCycleOk prechecks use the same policy.
No direct radio TX is left in IoHomecontrol.cpp.

Existing startRadioTransmit helper paths run their LBT checks. Queued/direct
startControllerTransmit paths, immediate 2W/auth responses, blocking discovery
and module diagnostic TX do **not** establish a qualified access scheme merely
by sharing the budget gate. Existing RSSI-unavailable/host-force behavior is
not sufficient qualification evidence. Neither SX1276 nor SX1262 has qualified
polite access in this change. Retain the conservative production fallback.

Before implementing/enabling the higher mode, identify the applicable device
class, Austrian implementation and current harmonized standard. Record required
CCA threshold/listen duration, valid sensor behavior, busy backoff, occupancy and
TX-time limits, AFA/channel-hopping requirements, ERP/antenna constraints and
response-timing effects for every TX category. Invalid/missing CCA must deny the
higher mode. A partial LBT call or firmware flag cannot substitute for evidence.

Measure SX1276 and SX1262 PHY airtime against IoHomeAirtime with SDR/analyzer for
32-byte ACKs, 1024/1536/2048-byte wake preambles and bursts; verify power/spectrum
and timed challenge/ACK turnaround. These physical tests cannot be performed
by native tests. Higher production default activation remains explicitly pending.

Source references (not a certification):
- [EU Decision 2025/105](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32025D0105)
- [ETSI EN 300 220-2 V3.2.1](https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf)
- [CEPT reference from supplied TODO](https://docdb.cept.org/download/4820)

## Regression checks

From OFM: `python3 test/test_rf_access_policy.py` checks production/simulation/
bench gates, no web console, expiry, uint32 wrap, saturation and reboot recovery.
The native controller suite checks duty admission for direct/queued/1W/ACK/
auth/discovery paths, LBT behavior, no refund on lost TX_DONE and simulated bypass.
Hardware acceptance remains pending; only ordinary builds may leave the lab.
