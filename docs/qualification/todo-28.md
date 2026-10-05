# TODO 28: 1W reservation physical power-cut campaign

Status: **not physically performed**. Required equipment/application access is not available in this session.

Keep sender durable high-water and separately observed receiver acceptance in the report. The receiver delta>0 and delta<=100 window limits recovery after unused reservations; persistence correctness does not prove indefinite peer resynchronization.

- [ ] `before_commit` — Cut before reservation commit; verify no RF uses uncommitted values.
- [ ] `inactive_write` — Cut during inactive slot write; verify previous durable floor survives.
- [ ] `lost_ack` — Cut after data write before acknowledgement; verify complete reservation is recovered.
- [ ] `before_rf` — Cut after durable commit before RF; verify next boot skips the reserved window.
- [ ] `no_reuse` — Compare actual captured rolling codes across every reboot; no reuse.
- [ ] `identity_mismatch` — Restore under changed controller identity/key; verify fail closed.
- [ ] `wrap` — Exercise FFFF to 0000 and peer freshness across wrap.
- [ ] `repeated_reboots` — Repeat cuts without delivery to peer; expose possible desynchronization.

Evidence record: `reservation_journal_powercut_campaign` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
