# Battery requests, actual RF outcomes and polling backoff

Implemented 2026-10-11. See [RF access policy](rf-access-policy.md) for modes,
boot recovery and outstanding physical qualification.

`iohc battery CH probe`, `probe09` and `objects` remain explicit one-shot
read-only diagnostics with the existing ETS Extended battery-diagnostics,
paired-channel, identity/key/revision and resource guards. They do not require
periodic ETS status polling. They do not infer battery percentage from raw data.

`pending` means the bounded module-level deferred request (maximum 10000 ms).
`submitted`/`queued_not_transmitted` means only an entry in the controller queue.
It is never a TX-success indication. Console events then report `rf_tx_started`
(accepted by radio), observed `rf_tx_completed`, correlated `reply_received`,
and terminal result. Each channel's last request is scoped to its bound node,
key and product-context revision; changing identity hides stale diagnostics.

`iohc battery CH status` prints historical coarse evidence/age alongside
lastProbeResult, pending, lastProbeRfAttempt, rfStarted/rfCompleted/replyReceived,
accepted RF count, result age and retryAfterMs. Old coarse=full stays historical,
percent=unknown. RF completion is not inferred from enqueue or a TX timeout.

Duty rejection reports battery_probe06_duty_blocked, battery_probe09_duty_blocked
or battery_objects_duty_blocked, required airtime, remaining quota and conservative
retry delay. If nothing was accepted by radio, terminal result is
`not_transmitted_duty_budget`. A partially sent exchange instead reports
`exchange_duty_budget`; it is never claimed to have sent no RF.

Object status is reported individually for A601/A607/A60E/0009/4003, including
queued, unsupported, timeout, duty_blocked and complete. For non-Somfy/VELUX
manufacturer contexts the first three are `not_in_manufacturer_sequence`; only
0009/4003 are requested. Completed/rejected/timed-out reads can advance the
sequence. Duty rejection stops it and marks remaining reads duty_blocked, avoiding
repeated sends with no allowance. No A607 key bytes are exposed by this tracker.

Both tracked and periodic status polling consult conservative RF eligibility
before enqueue/logging. Each channel staggers its next eligible time by 250 ms;
while waiting there is no repeated Request status log or RF retry. Existing
motion/STOP retry semantics remain; duty backoff does not masquerade as an
actuator failure. Existing coherent/authenticated status processing can clear
redundant tracked polling; no authentication or freshness checks were relaxed.

Tests: native controller battery regressions cover probes 06/09 and object reads
at used=3404, required>196, limit=3600 with zero radio accepts, preserved history,
RF phase transitions and identity change. The production scheduler extraction
test exercises requestStatus's actual backoff/stagger logic. RF laboratory,
three-physical-channel traffic and VELUX battery captures remain hardware work.
