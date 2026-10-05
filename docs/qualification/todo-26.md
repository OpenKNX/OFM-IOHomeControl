# TODO 26: Semtech prefix and framing qualification

Status: **not physically performed**. Required equipment/application access is not available in this session.

Recovered STM software-prefix byte counts are not Semtech register values. Retain RF bitstream and register dump together; do not adjust production mapping based on a numerical resemblance.

- [ ] `short_2w` — Measure on-air short 2W prefix and compare with recovered 10 software bytes.
- [ ] `long_2w` — Measure on-air long/LPM prefix and compare with recovered 54 software bytes.
- [ ] `oneway_wake` — Measure on-air 1W wake prefix and compare with recovered 1960 software bytes.
- [ ] `sync` — Verify actual 55/FF sync handling with peer capture.
- [ ] `no_double_framing` — Verify 57 FD 99/UART observations are not added again as software framing.
- [ ] `trailer` — Capture original-peer acceptance with and without optional 30 trailer.

Evidence record: `semtech_prefix_mapping` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
