# Golden RF corpus

This directory makes RF regressions reproducible without retaining a real
controller key, system key, or an unredacted pairing exchange. The initial
corpus is in `golden_rf_corpus.h` so the native test binary does not depend on
its current working directory.

Every frame records its scenario, radio path, sanitized provenance, expected
decoded header fields, and crypto expectation. The test suite verifies that the
wire bytes still decode exactly as declared; the public `0x30` trailer-MAC
vector is additionally verified cryptographically.

The initial scenarios cover the August regression families:

| Scenario | Fixture / expected outcome |
| --- | --- |
| Smoove remove-add on SX1276 | Authenticated `0x39`, then 29-byte `0x30` without trailer |
| Dimmer pairing on SX1276/SX1262/LR1121 | 35-byte `0x30` whose out-of-length trailer verifies; the same protocol fixture is attributed to each radio path |
| RS100 on SX1262 | Foreign challenge is rejected by pairing correlation; valid retry confirmation decodes |
| Key extraction | KIG300/KLR200-style address-response shape pins address verification parsing |
| Pairing interference | One unrelated frame is assigned to each pairing wait state with its required outcome |

Radio labels describe the originating hardware path. The protocol corpus is
driver-independent; OFM has no LR1121 driver, so the retained LR1121 fixture is
protocol validation only and must not be claimed as an OFM hardware replay.

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
