# 2W service diagnosis

## Identity and result

`iohc status` / `iohc01 status` show the gateway sender, imported hub and
extraction identity separately. See [identity and migration](two-way-controller-identity.md).
`iohc 2wtrace` dumps the most recent 24 structured trace points (older points are
replaced). Every controller-path 2W TX records the actual parsed source/destination,
command, CTRL0/CTRL1, gateway, RF frequency, MCU time, local TX ID, session start,
attempt and state. Local TX IDs are diagnostic counters, not on-air sequence IDs.
Dump after each test; do not print during the response window. No live console
printing was added to the TX/RX path, so diagnostic formatting does not delay RX.

`rx_observed` only means a parsed packet was seen. `rx_correlated` is recorded
only after the existing peer/destination and expected service/response checks.
A status outside those checks must not prove completion of the open transaction.
Challenge and ChallengeResponse payloads, HMACs, key transfers and object chunks
are redacted. Only Execute, status, PrivateResponse and explicit error payloads
are retained; full wire authentication material is never copied into the ring.

`transport_complete` means a closing exchange, not a moving actuator.
`authenticated_exchange` reports a challenge plus sent response. It does not
prove authorization for every function. `device_accepted`, `actuation_started`
and `target_reached` remain **unknown** without independent protocol/device evidence.
The public `lastTwoWayOutcome()` API exposes these independent evidence fields.
The internal `Completed` callback preserves its transport contract; it must not
be presented as actuation success. An explicit rejection remains an error.
No-response and authenticated-unconfirmed/no-closing-reply outcomes remain separate;
no new retries or automatic timeout/preamble changes are introduced.

## Timing and power

Trace preamble units are eight-bit symbols/bytes; bits and estimated preamble
microseconds are printed separately. TX-done time is when the MCU observes TX
completion, not an RF analyzer measurement. SX1276 RX read/IRQ/CRC and activity
ISR timestamps are retained where available. Other targets explicitly report
missing timestamp/CRC evidence, never fabricate RF timestamps. CRC statistics
and radio IRQ counters remain available through `iohc radio`. A parsed missing
response cannot by itself distinguish RF silence from a lost RX window.
`PreamblePlan` and `TimeoutSelector` identify their component to avoid counting
two different diagnostic lines as two physical starts.

`iohc 2wdiag power auto|always|low` controls the outgoing LOW_POWER class bit;
`iohc 2wdiag preamble auto|1024` controls the start preamble; `iohc 2wdiag wake on|off`
controls the runtime wake-belief optimization. None changes the actuator's own
energy-saving setting. The present console has no verified device power-save
setter: unsupported/unverified, no inferred PRIVATE opcode and no fabricated ACK.
The learned power class is evidence, not a mistake when an override is used.
Always-alive devices keep their short start profile; low-power devices keep their
explicit wake profile and long restarts. Serial number/device brand alone does
not prove mains versus solar power. Follow observed MIB/device metadata.

The 1011 ms deadline is selected by the existing MIB/CTRL1 timeout table. It is
not interpreted as a bare 40 ms/40 s MIB field; do not enlarge it without evidence.

## Battery

`iohc battery 1 status` separates raw ETS mode, diagnostics, effective monitor,
channel active/suspended/paired/protocol and internal bank flag. Modes 0/1/2 are
off/status-only/extended; there is no automatic battery polling. The visible
per-channel selection governs the mode, not the internal BAT module checkbox.
`probe`, `probe09`, and `objects` issue existing bounded, authenticated reads;
support remains unverified until a valid response arrives. VELUX GenericProfile
or unmatched family alone does not establish unsupported battery functionality.
Missing battery evidence is unknown, never 0%, never a communication failure.
A mains product may not expose battery evidence at all.

Named guards: diagnostics, not_2w, not_paired, channel_disabled_or_suspended,
identity_collision, commissioning, radio_diagnostic, metadata, radio. Temporary
resource contention defers one manual request for at most 10 seconds; checking
again every 250 ms never interrupts pairing, extraction or scanning. Disabling
mode or changing node/context cancels it. A second pending request is rejected.
Successful queueing only confirms submission, not device capability or execution.

## Required hardware qualification

On the same VELUX 562292, start with a working KLR operation and independent
position before/after. Capture its source NID and exact working short/extended
Execute payload. Compare ACEI, originator, selector, function/profile byte and
trailer with OFM; do not assume `0163C80080D80600` works because authentication
closes. The reported 82.7% idle status and three closed attempts proved no movement.

Run at least 10 controlled trials, including short/long idle. Change one variable
at a time: wake on/off, preamble auto/1024, power auto/low. Record initial/final
position, actual movement, starts, RF frequency, response latency, total outcome,
LBT, trace points and separate sniffer timestamps. Test KLR between OFM trials.
Check whether unsuccessful first/second starts already caused any side effect.
Restore the working baseline before each comparison. Repeat after reboot.

Also qualify full/partial ETS downloads and old-project import, all battery modes,
Somfy/VELUX mains/solar, idle/busy resources, and normal 1W cloning on both radios.
Software tests cannot claim these physical results. No additional firmware timeout,
scan-time or SPE policy is justified solely by the current non-movement report.
