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

## Audit of 18 October 2026

The `audit-20261018/` YAML files retain the supplied public issue excerpts
(Atlantic AGILIA #165, MOTO1000 #163, Nina #162) and three unmodified upstream
fixtures, pinned to Laberning `6fdee1e7271838978342b1aa6f24c3a5c5c0674d`.
The Sunilus fixture was introduced by `6bb8013d7004fcfaed2bef99e3661f2f78590ea6`.
Twenty-six non-key-transfer protocol frames are executable `Frame` entries in
`golden_rf_corpus.h`, with six scenario records. Public addresses are retained
for correlation. No system/controller key or encrypted 0x32 transfer is included.
Masked authentication bytes are never reconstructed. Original public
challenge/MAC bytes in the upstream YAML are not used as verification vectors;
the corresponding C++ selection omits these authentication frames.

Sources:
- Atlantic: https://github.com/laberning/home_io_control/issues/165
- MOTO1000: https://github.com/laberning/home_io_control/issues/163#issuecomment-6252058342
- Nina: https://github.com/laberning/home_io_control/issues/162#issuecomment-6249941365
- Sunilus: upstream `tests/corpus/captures/pairing/somfy_sunilus_pairing_key_transfer_rejected_error.yaml`
- Battery: upstream `tests/corpus/captures/statuspoll/somfy_rs100_statuspoll_kig300_sx1276.yaml`
  and `somfy_oximo40_statuspoll_sx1262.yaml`.

These are **partial public hardware transcripts**, not newly recorded OFM RF
captures or complete authenticated replays. CRC bytes are absent. The PHY tests
generate a synthetic CRC/UART envelope for Nina's published bytes, and mutate
only the opcode for synthetic 0x2F/0x37/0x38 parser regressions. The SX1276 adapter
test simulates the hardware CRCOK flag. Synthetic pull sessions test real OFM
controller/crypto code with public test keys; they do not prove Atlantic's
corrected pull works on hardware.

Both cited battery fixtures report `0x60 = full`. Neither supplies a real
`not_reported` (`0x80`), `normal`, or `low` observation. Those classes and `0x61`
are explicitly synthetic boundary cases. Raw 0x71 parser tests do not authorize
battery publication from unauthenticated messages or new polling.

Immutable YAML SHA-256 values:

- `atlantic_agilia_pairing_push_refused_20261018.yaml`: `e8a33aa5ec3aa4bd0bef645d86255348f6e443935f587ee912bfdd958e6e93e0`
- `moto1000_execute_outcomes_20261018.yaml`: `2aa3e945d2003ea36918a23ce1f3729b6351e219523ab438b98c1a5ee6e3196d`
- `nina_directed_2e_20261018.yaml`: `34a63a6c06a5c1cf27d5fad82db2670508075c823f2235ed9f75abbf01999db5`
- `somfy_oximo40_statuspoll_sx1262.yaml`: `87a877a075d86cc511c5115c6e51ecb53b0f2b0098d44e5baba7add0efe72882`
- `somfy_rs100_statuspoll_kig300_sx1276.yaml`: `4fa35a95103aadc4bb7988e17fc6bef8ae3a1d9fd2580f6fe23bd9d0e5ce16ee`
- `somfy_sunilus_pairing_key_transfer_rejected_error.yaml`: `472e32dd809b0efd7e569f71e2f13885bb9fed27c7ae9df1bd0a281e0f0d9fc1`

## Lost 0x33: diagnostic evidence boundary (P0.5)

Reference: Laberning `6bb8013d7004fcfaed2bef99e3661f2f78590ea6`, issue #161.
The proposed `0x50 -> 0x3C -> 0x3D -> 0x51` name read is a possible diagnostic,
but its challenge is generated by the peer and its MAC is generated by OFM.
The peer's name reply has no independently verified MAC. A successful local
0x3D transmission plus 0x51 therefore cannot independently prove that the peer
installed the new key, or distinguish an old-key peer. Enabling persistence on
this basis would violate the checklist's proof requirement. OFM deliberately
keeps the bounded existing key-init retries and requires 0x33 for push success;
this audit adds a production-controller regression rejecting name replies as
key proof. No automatic GetName pairing fallback is enabled. Hardware work must
establish an independent peer-generated MAC bound to a fresh local challenge,
key, peer, session and channel before such a fallback can commit a key.
