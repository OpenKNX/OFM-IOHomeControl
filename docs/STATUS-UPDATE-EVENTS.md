# StatusUpdate event classification

Only a valid `0x100A` event envelope may enter the existing `0x71` position,
movement, status-expected and last-controller decoders. Other IDs remain opaque;
malformed envelopes do not fall back to position parsing. Raw diagnostic capture
remains available. Source/destination, identity, authentication, minimum inbound
admission and ACK behavior are unchanged.

The supported envelope has flags/status at DATA0/1, BE16 event ID at DATA2/3,
body length at DATA4, then exactly that many bytes. Position decoding still
requires at least eleven total DATA bytes. This is a conservative application
allowlist, not a claim that every vendor release uses this complete schema.

Evidence: laberning/home_io_control b580542 and 66d9c72 community fixtures
`probe/somfy_rs100_probe_subscribe_event_sx1262.yaml` (ten-byte `0x2005`)
and `statuspoll/somfy_rs100_statuspoll_kig300_sx1276.yaml` (sixteen-byte `0x100A`).
The latter DATA is `04 60 10 0A 0B 00 00 AC 9E 00 0F BE FE DB 01 00`.
Neither corpus record independently proves the authentication sequence/CRC.
Controller regression tests use these bodies with synthetic identities and keys.

The original 2022 Cortex controller (SHA256
`e0c20192affea4bc02514fd19d460b85a95f429b5834e53f1970a0018f946529`)
forwards RF `0x71` to the host at `0x08005C80`; a complete vendor host event parser
was not recovered. Original shared bytecode names `0x2005` LOCAL_SENSOR_EVENT,
but its concrete caller uses OVP managers. These limits are recorded in
Velocet/Findings/2026-10-30-laberning-event-radio-validation.

The reproduced bug was a trusted synthetic eleven-byte `0x2005` event reaching
position/target callbacks; the actual ten-byte captured event already fails the
existing inbound minimum. Tests also cover padded/malformed/unknown events,
preserve valid `0x100A` feedback, and retain the existing proof and ACK gates.
No subscription, physical-unit decoding, ACK/auth, 0% encoding, preamble,
protocol timeout or RF-limit change accompanies this classification fix.
