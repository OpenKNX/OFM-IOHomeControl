# VELUX/Somfy TODO qualification, 2026-10-08

This tracks the supplied TODO (5) as hypotheses and work items, not as proven
causes or hardware results. Software implementation and hardware acceptance are
separate. Commits are grouped by independently reviewable topic.

| Topic | Software work | Remaining evidence/acceptance |
| --- | --- | --- |
| 2W source identity | Key import retains own NID; new ESP32 NID random/unicast; journal remains durable; own/hub/extraction NIDs reported; observed collision blocks active 2W, leaves 1W intact | Existing earlier cloned identities are preserved, not automatically migrated. Collision latch is runtime-only and re-detected on import/observed traffic; determine persistent migration and controller authorization with real capture/pairing evidence before changing a bound NID. Alternate KLR/KNX and reboot bench test pending. |
| Battery mode | Visible channel raw mode governs diagnostics; status prints raw/effective mode and each condition. Individually named guards and one bounded deferred manual request; identity changes cancel. Existing authenticated response decoding retained | Test actual full/partial downloads and VELUX mains/solar/Somfy. Unknown device capabilities remain unverified; no unsupported conclusion from GenericProfile. No new SensorInformation opcode or guessed battery percentage. |
| Power save | Separate ETS help for gateway LOW_POWER/preamble/wake policy versus actuator setting. Existing metadata/override behavior and timing retained | No verified actuator energy-saving setter. Capture actual command and capability/ACK first; no fabricated support/success. |
| VELUX ACEI | Label VELUX/KIG300; enum 99/103 and IDs unchanged. Native test covers 0/50/100/stop/favorite for 63/67; tilt/Cozy retain special formats | Actual same-device KLR payload comparison and ETS old-project/full/partial download remain bench requirements. No automatic manufacturer default switch. |
| Status/failure | Bounded redacted 2W trace; observed and correlated events separated; API exposes transport/authentication/device/actuation/target evidence independently. Explicit rejection remains rejection | No movement success inferred from 82.7% idle status. SX1276 RX evidence available; SX1262 IRQ timestamps not exposed by the current driver, reported absent. Independent RF timestamps and final positions required. |
| Inventory | Durable key captured distinct from no nodes and verified inventory; existing duplicate/no-change assignment semantics retained | Historical 1-versus-3 observation has no equal-condition trace corpus. No speculative listen/scan/SPE tuning. |
| Retry root cause | Actual TX source, attempts, state, preamble units/estimate, timing and correlation recorded; components named; existing MIB/CTRL1 deadlines and operation retry policy retained | Ten-run controlled wake/power/preamble matrix; RF silence versus missed window; whether early starts caused effects; positive KLR control and actual movement all pending hardware. |

The service procedures and capture matrix are in
[two-way-service-diagnostics.md](two-way-service-diagnostics.md), identity/migration
limits in [two-way-controller-identity.md](two-way-controller-identity.md), and
[key-import-verification.md](key-import-verification.md) describes inventory results.

Available software checks: native controller/crypto/exchange tests, production
battery gate/deferred-service adapters, channel ETS UI tests, standalone gates,
parameter XML/header generation and SX1262/SX1276 firmware compilation. ETS itself
and physical radio/actuator hardware are not available in this workspace. Product
package signing and an actual ETS download cannot be claimed from XML generation.

## Local verification results

- 688 native protocol/controller/crypto/exchange cases passed, including collision
  blocking, unchanged 1W TX, and authenticated completion without actuation evidence.
- 50 ETS channel/UI cases and 44 actual ETS JavaScript cases passed.
- Production-method battery gate/deferred-service, battery publication and
  standalone undefined/0/1 regressions passed.
- Production key-import branch retains its own NID across hub changes, commits
  before RF, and blocks collision/missing key/durable-write failure.
- Producer internal integrity checks passed; generated header macro definitions
  are identical to the checked-in header, including BAT offsets and 99/103 values.
  Expanded layout: 16 channel trees, 84 contextual help topics verified.
- Normal `release_OpenKNX_REG1_ESP_V00_11_SX1276_IP` and standalone
  `standalone_develop_OpenKNX_HELTEC_WIFI_LORA_32_V3_SX1262_IP` builds passed.
- Producer could not perform XSD validation (schema missing) or create/sign an ETS
  product package (ETS absent). Actual downloads and RF/actuation qualification
  remain open; do not interpret this list as completion of those acceptance items.
