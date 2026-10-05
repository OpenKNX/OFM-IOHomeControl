# TODO 25: Optional DIO timestamp qualification

Status: **not physically performed**. Required equipment/application access is not available in this session.

Record board pin mapping, logic-analyzer sample rate and latency distribution. MCU ISR time is not RF time until offset/jitter is measured; missing wiring must never fabricate an edge.

- [ ] `dio4_present` — Wire DIO4 and measure preamble ISR offset/jitter against the RF edge.
- [ ] `dio2_present` — Wire optional DIO2 SyncAddress and measure sync ISR offset/jitter.
- [ ] `dio4_absent` — Disconnect DIO4; verify packet reception works and preambleTimestampValid stays false.
- [ ] `dio2_absent` — Disconnect DIO2; verify packet reception works and syncTimestampValid stays false.
- [ ] `edges_after_restart` — Repeat across receive restart, scanning and forced recovery; reject old-edge timestamps.

Evidence record: `sx1276_edge_wiring` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
