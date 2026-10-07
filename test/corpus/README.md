# Golden RF corpus

This directory makes RF regressions reproducible without retaining a real
controller key, system key, or an unredacted pairing exchange. The initial
corpus is in `golden_rf_corpus.h` so the native test binary does not depend on
its current working directory.

Every frame records its scenario, radio path, sanitized provenance, expected
decoded header fields, and crypto expectation. The test suite verifies that the
wire bytes still decode exactly as declared; the public `0x30` trailer-MAC and
`0x2A` authenticated-discovery vectors are additionally verified
cryptographically.

The initial scenarios cover the August regression families:

| Scenario | Fixture / expected outcome |
| --- | --- |
| Smoove remove-add on SX1276 | Authenticated `0x39`, then 29-byte `0x30` without trailer |
| Dimmer pairing on SX1276/SX1262/LR1121 | 35-byte `0x30` whose out-of-length trailer verifies; the same protocol fixture is attributed to each radio path |
| RS100 on SX1262 | Foreign challenge is rejected by pairing correlation; valid retry confirmation decodes |
| Key extraction | KIG300/KLR200-style node-verification response pins node/system verification parsing |
| Pairing interference | One unrelated frame is assigned to each pairing wait state with its required outcome |
| VELUX KLI-compatible enrollment | Public source-derived `0x30` shape with source, wrapped key and sequence masked; four ADD destinations plus STOP/DOWN timing metadata |
| KLR300 2W pairing/search (2026-09-12) | Sanitized 0x28/0x2E/0x2C/0x31/0x32/0x2A/0x3D/0x36 sequence; CTRL1 and payload lengths preserved, secrets replaced |
| Authenticated 2W discovery | Public re-keyed `0x2A` known-answer vector pins the one-byte command transcript, separate six-byte challenge and six-byte HMAC |
| VELUX KLI310/KLI313 open-registration sweeps | Captured 1W `0x2E` class destinations and rolling-sequence progression for BF/FF/37F; MAC material retained only as public/redacted fixture bytes |
| TaHoma/KLI SSL (Issue #112) | Public `0x28`, `0x29`, `0x2C`, `0x51`, `0x54`, `0x57` and `0x04` frames; masked key/challenge frames deliberately excluded |
| VELUX MSU mid-travel STOP (Issue #95) | Public authenticated 1W STOP pins exact destination, payload and declared-length/HMAC shape; no controller key is retained |

Radio labels describe the originating hardware path. The protocol corpus is
driver-independent; OFM has no LR1121 driver, so the retained LR1121 fixture is
protocol validation only and must not be claimed as an OFM hardware replay.

The KLI-compatible enrollment reference is intentionally labeled
**source-derived**, not captured. It records the stable behavior published by
the public KLI implementation (manufacturer `0x01`, four `0x30` destinations,
STOP then DOWN to `0x00003F`) and masks controller-specific bytes. Replace or
augment it with a sanitized second-receiver capture after a physical KLI/KUX
bench session; do not relabel the current fixture as hardware evidence.

## Adding a capture

1. Redact or re-key every controller/system key before committing. Keep an
   original capture outside the repository.
2. Record a concise provenance string: device family, radio path, date range,
   and the transformation applied. Do not include a serial number or location.
3. Add the sanitized wire frame and complete metadata in
   `golden_rf_corpus.h`. Prefer a public crypto vector when a MAC needs a
   repeatable verification key.
4. Add a scenario-specific assertion in `test_protocol.cpp` if decoding alone
   does not cover the expected controller outcome.
5. Run `make corpus` from `test/`. CI runs the same target.

The corpus intentionally stores only values that are safe to publish. A frame
whose real cryptographic outcome depends on a private key may assert
`HmacPresentKeyRedacted`, but must not embed the key merely to make a test pass.

## Somfy bioclimatic pergola / Pergola louver IO (PR #157)

`somfy_pergola_louver_pr157` retains public 2W GI2, EXECUTE, status-poll and ACK
captures for io type 0x1D / subprofile 0. It verifies MP-position semantics,
STOP/Favorite special values, short ACK layouts, and non-exact resting positions.
Source: [https://github.com/laberning/home_io_control/commit/1c57116ba2843acc7d4a74439767e8714613e6f1](https://github.com/laberning/home_io_control/commit/1c57116ba2843acc7d4a74439767e8714613e6f1), public PR #157 (@piitaya),
2026-10-06, actuator 262552 / hub 9AF3CE, Heltec V3 / SX1262. The probe/session
were recorded by a second receiver; full status and immediate ACKs came from the
hub log. Original timing, radio/context notes and omitted authentication remain
in all four original YAML files alongside the executable C++ fixtures. No OFM
physical capture or hardware qualification is claimed. Synthetic authentication
in controller tests is OFM test infrastructure, not captured wire material.

The YAML files below were copied unchanged from the cited commit. Their SHA-256
hashes are checked in the UI/provenance regression:

- `exchange/somfy_pergola_louver_exchange_execute_acks_sx1262.yaml`: `58834ce7445b33204d98d11745e5a2b94822ddebc202cc77c68df87a5b7e8d21`
- `exchange/somfy_pergola_louver_exchange_open_close_position_stop_sx1262.yaml`: `81e1330c04a84915f479faad29ba45a4b504b12ece2937bb627d9a06d9eff028`
- `probe/somfy_pergola_louver_probe_get_info2_sx1262.yaml`: `ec9a75ca7b04faf89b9decc7c935769b7fbfb6a86bccb0bd562f2ba3a1bb525e`
- `statuspoll/somfy_pergola_louver_statuspoll_replies_sx1262.yaml`: `bca765421eaadb8a3856790e8ed7945fbe875badff6ed09b0651754b09d6f869`
