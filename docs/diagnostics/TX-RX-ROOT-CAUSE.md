# TX_DONE / RX root-cause status

**Still unknown on hardware.** This patch adds measurement support; no RF timing,
request bytes, HMAC, retry, preamble, timeout, identity, duty or LBT policy was changed.
No real measurements CSV is supplied because no radio/analyzer was operated.

Confirmed in source/native tests: SX1276 PacketSent requires software completion
servicing and explicit RX reopening. The old RX-ready delay starts at software
observation and cannot bound earlier ISR/scheduler delay. New events separate
ISR entry, polling fallback, driver transitions, controller observation and RX
setup. ISR entry is not physical pin-edge time. Native delayed-service fixtures
are synthetic and must not be reported as hardware measurements.

Validation: native protocol/controller/exchange suite, actual SX1276 adapter and
timing gate on/off tests, SX1262 PHY/error tests, offline timing analysis, wake/UI
checks and pinned RF corpus. Isolated ESP32-S3 module integration builds cover
instrumented SX1276, timing-disabled release and SX1262. The unmodified full OAM
integration is independently blocked by its existing LogicModule/knxprod version
mismatch. The disposable harness excludes Logic/FunctionBlocks and its generated
holiday hook; those validation-only edits are not production changes. Resolve
the full-app dependency mismatch before publishing/releasing a new OAM pin.

Follow [the capture procedure](SX1276-TX-RX-TIMING.md). Required decision evidence:

- Physical edge -> ISR entry: synchronized analyzer/MCU evidence; currently unavailable.
- ISR -> controller: recorder plus raw outcome traces, including missing ISR/drop counts.
- Controller -> RX mode write: recorder and decoded SPI capture.
- RX mode write -> actual acquisition: RF/preamble evidence, not software return success.
- Request heard and peer response present: independent RF observation; currently unavailable.

If a repeatable delay overlaps a real peer reply, qualify a minimal bounded
servicing fix. If reentry is timely but the peer reply is absent, investigate
sleep/motion acquisition and RF coexistence. If a valid reply is present but
discarded, investigate filtering/correlation. None is asserted as proven here.
