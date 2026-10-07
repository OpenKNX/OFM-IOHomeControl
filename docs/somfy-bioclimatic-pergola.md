# Somfy Bioclimatic Pergola / Pergola louver IO

Profile **0x1D / subprofile 0** (packed **0x0740**) is an explicitly supported 2W position cover. MP represents the louver opening: 0000 = 0 %, 6400 = 50 %, C800 = 100 %, with normal polarity. The physical louvers do not imply a separate slat/tilt FP. No FP1..FP16 or ventilation capability is inferred.

ETS recognizes the profile automatically as a position cover. Position, Up/Down, Stop and Favorite use existing cover paths; slat/tilt remains hidden. The detailed device selector also includes “Bioklimatische Pergola (Lamellen über Position)”. No manual profile override is needed for discovery.

Wire behavior is based on public PR #157 captures from [laberning/home_io_control](https://github.com/laberning/home_io_control/commit/1c57116ba2843acc7d4a74439767e8714613e6f1), recorded on 2026-10-06 with Heltec V3 / SX1262. Public actuator node 262552 and hub 9AF3CE are preserved. This is public-capture qualification, **not physical OFM hardware qualification**.

The four original YAML capture files are retained byte-exactly under `test/corpus/captures`. All 33 safe frames are executable golden fixtures, including GI2, movement/poll requests, seven full status replies, two short EXECUTE ACKs and the STOP ACK. Missing/masked challenge and authentication frames remain absent. Controller tests use explicitly synthetic test authentication.

A stopped device can rest slightly away from its target. D200 is STOP/Current and D800 is Favorite, never numeric percentages. A valid current MP remains usable with either special target; D200/D200 supplies no usable position. Generic stopped-target fallback may use the valid current position, without decoding the special target as a number.

Short six-byte 04 ACKs close a correlated exchange without requiring a full status layout. Position/main-only ACK fields stay opaque. The capture-proven short STOP ACK current value (3EA0 ≈ 31.3 %) is published only for profile 1D/0, a matching queued STOP, successful challenge authentication, and matching channel/node/key/product-context revision. Follow-up PRIVATE 03 uses the ordinary full-status decoder.

The upstream notes mention missing replies at its default receiver bandwidth. These captures do not justify changing OFM radio settings automatically; RF and physical operation still require device testing.
