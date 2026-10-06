# Software implementation 0.8.0 / ETS 3.12

Historical `v1dev-KLFDocumentation` checkpoint, 2026-10-05. Commits use Franz Reisenhofer. No push or device flash.

Each numbered audit point has a separate commit for its supported scope. This does **not** mean every operational acceptance criterion is complete. In particular points 20–28 are product-scoped/offline representations unless explicitly noted, point 30 initially added an RCM lifecycle model, and point 32 is an opt-in storage adapter rather than a complete RP2040 radio port.

Current RCM integration is recorded in [Implementation TODO, point 13](../doc/Implementation-TODO-2026-10.md#13--operational-rcm-prerequisite-and-ownership). The commit table below records the original checkpoint; later integration supersedes its model-only RCM scope.

| Point | Commit | Software scope |
|---|---|---|
| 1 | `b11d0fb` | unify explicit ETS commissioning continuation and stage guidance |
| 2 | `9819d06` | preview frozen import candidates and exact channel assignments before saving |
| 3 | `c411b7e` | distinguish device receipt ETS project application and pending download |
| 4 | `c742c5a` | apply completed normal pairing through the shared durable ETS receipt path |
| 5 | `e38e478` | report recognition conflicts and offer explicit preservation of manual choices |
| 6 | `53bf374` | preview and confirm restoration of automatic recognition ownership |
| 7 | `1cd13aa` | journal bounded key-free terminal and ETS synchronization outcomes |
| 8 | `b1742f7` | persist checked identity-bound raw metadata with conflict-safe restore |
| 9 | `62c952c` | expose explicit identity-bound sensor setup and bounded monitoring in ETS |
| 10 | `b86133f` | present correlated priority and raw sensor evidence with validity and age |
| 11 | `04e39a8` | add token-bound ETS object read diagnostics and bounded opaque readback |
| 12 | `fe647ec` | exercise actual SX1276 adapter register and FIFO paths natively |
| 13 | `6760667` | bound idle SX1276 receive recovery without replaying protocol operations |
| 14 | `2ab2a43` | generate scoped semantic catalogue with source units and permission boundaries |
| 15 | `fcd3ef4` | explain product-scoped semantics and respect scene orientation capabilities |
| 16 | `6a5bbbf` | check legacy ETS memory and KO compatibility against a frozen baseline |
| 17 | `8bddf2d` | mark historical research checkpoints (research repository) |
| 18 | `9daf998` | decode explicit product observations with coherent context freshness and units |
| 19 | `8319259` | append gated temperature and mode KNX bank with explicit diagnostic read selection |
| 20 | `ca7ccaf` | encode coupled generic heater setback with validated context and ascending FP order |
| 21 | `46088e5` | represent recovered HeatPump mode MP and FP16 combinations |
| 22 | `e5a536d` | represent Atlantic DHW absence and relaunch without interpreting opaque bits |
| 23 | `6863502` | add strict Atlantic ventilation FP16 codec and offline activation |
| 24 | `908166c` | add explicit pergola orientation and speed codecs and diagnostic presentation |
| 25 | `e72a003` | decode alarm zone masks and preserve scoped sliding-lock raw evidence |
| 26 | `d41fb4f` | model dual shutter upper lower selectors with exact neutral-half representation |
| 27 | `3054880` | prepare bounded identity-bound segmented object writes while keeping RF disabled |
| 28 | `0a48a44` | serialize recovered opaque sensor subscription blob and fixed node tail |
| 29 | `15cb90f` | capture optional SX1276 preamble and sync ISR timestamps with explicit validity |
| 30 | `fcd3eac` | model recovered bounded RCM topology and nested lifetimes independently of discovery |
| 31 | `06ec2a1` | add explicit host LBT policy and bounded disturbance and forced-TX counters |
| 32 | `ab164f0` | provide checked namespaced storage adapter and opt-in non-formatting RP2040 LittleFS backend |

## Integration boundaries

- Legacy channel stride 68 bytes, 25 KOs/channel, numbers 600..999 preserved. Existing RGB/white bank 1000..1095 preserved. Appended PVX bank 1100..1195 has six objects/channel and a separately appended two-byte parameter block (definition/index), starting at 14525. Total application parameter size 14557. Explicit selection grants read diagnostics; value setters are ignored and validity remains false until exact binding/publication qualification. Defaults hide both product banks.
- Guided ETS actions remain explicit clicks: stage/status, frozen assignment preview, unchanged-plan apply, receipt application and download-pending status. Normal pairing reuses the receipt path. Per-field automatic ownership remains optional; conflicts are visible. Restoring automatic ownership needs a second unchanged-preview click and does not clear keys/counters.
- Eight checked, key-free history entries record terminal/project outcomes. Boot/job/node identify records; pending assignment receipts reconcile committed bindings after restart. No RF transaction resumes on boot. This history is not a full event log or proof that an ETS download finished.
- Metadata journals contain protocol class/MIB, original raw identity/name/GI evidence and bounded provenance, privately bound to the committed node/key. At most one changed channel is saved per idle loop. Cross-store ordering is not invented: conflicting legacy/journal profiles fail closed and fuller legacy identity is preserved. Samples/freshness are never restored. Persistence health is API34.
- Sensor default subscription is explicit, one attempt, requires independently established backbone. Poll interval 1..600 s and duration 1..3600 s are chosen host settings, volatile and disabled by default. General blob copy/default/tail serialization is offline only; unknown settings and physical units remain opaque.
- Product decoder API3A requires explicit product, index and optional supplied Centikelvin bounds. Same-generation fresh paired observations are required for coupled setback/siren/mode fields. User context is not discovered identity. Alarm special-zone words use a lookup; sliding-lock raw non-endpoint values remain unqualified even though the retained converter labels every non-C800 word unlocked. RGB FP10/11 are chromaticity u/v, not packed RGB bytes. White FP14 maps 2000..6500 K.
- Generic heater producers put FP11 before FP12/13; FP13 is comfortRaw minus encoded eco setpoint. HeatPump MP/FP16, Atlantic DHW opaque4000, ventilation enum and dual-shutter D100 neutral half are kept product-scoped. No producer grants RF-write permission.
- Object writes are a bounded identity/key/revision-bound 48/49 + continuation model only. No writable schema is qualified and no write is queued. Object-read ETS start/status/cancel/slice uses the existing allowlist and boot/token checks, bounded raw slices and no semantic/authentication claim for returned bytes.
- SX1276 tests compile the actual driver against a register/FIFO transport. They cover init, bandwidth, FIFO reset, TX completion, payload-ready polling, CRC failure and truncation/drain. They do not exercise electrical SPI timing. Idle RX supervision checks every 5 s and permits three driver-only recovery attempts per boot; no queued/active operation is replayed. API39 exposes counters.
- `IOHC_RX_EDGE_TIMESTAMPS=1` enables optional ESP32 edge timestamps. DIO4 records preamble; `IOHC_RADIO_DIO2` adds sync mapping. Default mapping stays39; optional wired DIO2 uses3D (Semtech packet-mode SyncAddress). API3B reports separate validity/source and unqualified accuracy. Missing wiring creates no timestamps. No default RF tuning changes.
- RCM now queues the empty `0x36` prerequisite through the controller for matching-node counts 1/>1, using context variants 0/3. Only a validated `0x37` with at least three payload bytes advances that path to base mode; zero marked matches enters base directly. The lifecycle owns the 600 s outer and 300 s temporary timers, plus a chosen 5 s prerequisite guard. Marker/SystemId evidence import is explicit; automatic legacy inventory acquisition and physical peer qualification remain pending. Discovery timing is independent.
- Host LBT policy API3C selects current timing-best-effort(default), strict or forced behavior and a chosen disturbance cadence (0 disables,100..60000 ms). API3D shows saturating per-channel disturbance and per-operation bypassed/forced TX counts. Disturbance threshold−81 dBm and15/40/15 sample thresholds are separate from LBT. Sampling observes the current tuned channel; counts do not imply balanced channel exposure or recovered legacy cadence.
- RP2040 `IOHC_RP2040_CHECKED_STORAGE=1` opts into LittleFS. Failed mounts never format; records are namespaced, written to temporary files, renamed into the inactive slot and checked by existing CRC/generation/readback logic. Network, receipt, 1W reservation, metadata and history use the adapter. Native tests validate mount/rename failure and namespace contracts. Target build, partition coexistence, wear and real power cuts remain unqualified. Other MCU SX1276 SPI/RF integration is outside this storage adapter.

## Function-property additions

These are API opcodes on object160/property10, **not RF command IDs**.

| API | Meaning |
|---|---|
|33|Newest-first key-free history index0..7; schema1,21-byte record|
|34|Channel metadata journal health; no repair side effect|
|35|Identity-bound sensor info/default subscription/start/stop poll|
|36|Identity-bound priority or sensor-status query|
|37/38|Allowlisted object read start / boot-token cancel|
|39|SX1276 bounded receive-recovery counters|
|3A|Explicit product-decoded observation, raw/trust/age/unit retained|
|3B|Optional RX read/preamble/sync timestamp validity/source|
|3C/3D|Volatile host radio policy / disturbance and forced-operation counts|

## Verification and remaining acceptance

601 protocol/controller/exchange cases pass (370/169/62), plus actual SX1276 adapter and existing SX1262 suites,32 executed ETS JavaScript tests,34 UI checks,4 release-evidence and2 radio-summary tests. Storage adapter contract scenarios pass. Both generators and174 preserved memory/KO definitions pass. Producer4.3.12 integrity and project20 XSD validation pass. All three ESP32-S3 SX1276 TP-development,TP-release andIP-development builds pass.

Still unrun: physical SX1276/FIFO/waveform/peer/version3/1W tests; journal power-cut/exhaustion/wear and real recovery; actual ETS import/save/reopen/download/migration; signed product generation (ETS absent); optional edge wiring/latency and RP2040 target acceptance. `rfWrite` and `knxPublish` stay false. Still unknown: exact commercial/generation binding,8100/8103 member serialization,4300/4302 writable schemas,A607 bridge, physical bounds and unnamed FP/alias/GI/6F/73 fields. Automatic unsolicited-event/wake policy, automatic legacy BasicNode/SetBeacon inventory acquisition and full recipient-side RCM emulation remain future work; the controller prerequisite path is integrated.
