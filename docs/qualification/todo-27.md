# TODO 27: 2W journal physical power-cut campaign

Status: **not physically performed**. Required equipment/application access is not available in this session.

Use a controlled power switch and phase-trigger GPIO or storage trace. Record both slots before/after every reboot, restore decision and actual RF silence/traffic. Native injected faults do not replace this NVS experiment.

- [ ] `each_write_phase` — Cut power at erase, inactive-record write, data completion and acknowledgement boundaries.
- [ ] `old_record` — Verify partial new record preserves the complete old record.
- [ ] `lost_ack` — Verify complete new record loads after lost write acknowledgement.
- [ ] `equal_generation_conflict` — Inject equal-generation different records; verify fail-closed loading.
- [ ] `corruption` — Corrupt retained record; verify fail-closed behavior.
- [ ] `binding_mismatch` — Change key/binding context; verify 2W TX is blocked.
- [ ] `tombstone` — Power-cut unpair tombstone writes; verify no deleted binding reappears.
- [ ] `wrap` — Exercise generation wrap policy; never select ambiguous/newer-by-guess records.

Evidence record: `network_journal_powercut_campaign` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
