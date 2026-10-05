# TODO 30: Real ETS application migration qualification

Status: **not physically performed**. Required equipment/application access is not available in this session.

Retain signed artifact hash, previous/current application revisions, exported before/after project evidence and download/reboot logs. XML/generator tests do not qualify ETS migration; preserve manual overrides and group addresses.

- [ ] `signed_import` — Generate signed knxprod and import into the recorded current ETS version.
- [ ] `new_device` — Create a device and verify all channel parameters and object banks.
- [ ] `upgrade` — Update an older project/device; compare KO numbers, parameter memory and group addresses.
- [ ] `reopen` — Save/reopen project; verify parameters, recognition and overrides survive.
- [ ] `download` — Perform full and supported partial/application downloads.
- [ ] `reboot` — Reboot after download; verify firmware configuration and commissioning status/history.

Evidence record: `ets_application_campaign` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
