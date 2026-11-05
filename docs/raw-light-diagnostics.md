# Authenticated raw light diagnostics

Source: [RS100 application observations](https://github.com/laberning/home_io_control/issues/156#issuecomment-6665992911), [original log](https://github.com/user-attachments/files/34441214/somfy-log.txt). The unchanged supplied JSON in `test/corpus/rs100-light-event-vectors-2026-11-05.json` contains 15 class-B application payloads, not RF captures. Native authentication transcripts are synthetic.

`iohc sensor light-raw CHANNEL` reads the last authenticated inbound 0x71/0x2005 sample locally, without RF. Its exact ten-byte DATA must contain a five-byte `06 00 HH LL ZZ` body. HH LL is unsigned big-endian; zero is valid; ZZ stays opaque. There is no proven lux or irradiance unit, no conversion, KNX publication, persistence or automatic subscription. The 24-byte per-channel slot is allocated with the channel, not as a static controller array.

Source/destination, receive and proof timestamps, frequency and inbound-challenge provenance accompany the raw reading. Age describes the last observation, not current physical freshness. Existing proof ownership, identity, session, frequency and timeout checks gate updates. Key/node/mode/product-context invalidation and explicit event clear erase it. Other event types do not replace it. A direct subscription response cannot populate it through outbound correlation.

External reference sensors differ in position and sometimes orientation. The sample at 15:00 has no paired lux measurement; missing slots are not interpolated. No calibration formula follows from these observations.
