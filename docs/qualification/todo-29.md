# TODO 29: Storage wear and filesystem qualification

Status: **not physically performed**. Required equipment/application access is not available in this session.

Retain target, flash part, partition map, backend configuration, workload duration and physical byte/write counts. Do not estimate NVS or LittleFS lifetime from RAM-level save-call counts alone.

- [ ] `esp_network_rate` — Measure network-journal writes/bytes per realistic commissioning workload.
- [ ] `esp_receipt_rate` — Measure receipt-journal writes/bytes, including interrupted workflows.
- [ ] `esp_metadata_rate` — Measure metadata/history write rate and throttle behavior.
- [ ] `esp_reservation_rate` — Measure 1W reservation write rate under normal and reboot workloads.
- [ ] `nvs_margin` — Estimate wear margin from measured rates, actual partition geometry and flash specification.
- [ ] `littlefs_partitions` — On each supported RP target, verify opt-in partition coexistence.
- [ ] `littlefs_mount` — Force mount failure; verify no unexpected formatting or write permission.
- [ ] `littlefs_atomicity` — Power-cut rename/update phases; verify the actual filesystem atomicity assumptions.
- [ ] `littlefs_wear` — Measure filesystem write amplification and wear under the same workload.

Evidence record: `storage_wear_filesystem_campaign` in `docs/release-evidence.template.json`. Set each case to `passed` only after running it; record exact OFM/OAM revisions, operator, date, setup, expected/observed results and hashed retained artifacts. The checker rejects missing cases and model-only results. Never store secret keys in published captures.
