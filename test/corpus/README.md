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
