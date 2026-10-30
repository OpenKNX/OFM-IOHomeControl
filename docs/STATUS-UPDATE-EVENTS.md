# StatusUpdate event classification

Only a valid `0x100A` event envelope may enter the existing `0x71` position,
movement, status-expected and last-controller decoders. Other IDs remain opaque;
malformed envelopes do not fall back to position parsing. Raw diagnostic capture
remains available. Source/destination, identity, authentication and ACK behavior
are unchanged. Short valid non-position events now use the same inbound proof
path; the eleven-byte position minimum is not applied to opaque event bodies.

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
position/target callbacks; the initial classification-only fix still rejected the
actual ten-byte captured event before authentication. Tests cover padded/malformed/unknown events,
preserve valid `0x100A` feedback, and retain the existing proof and ACK gates.
No subscription, physical-unit decoding, ACK/auth, 0% encoding, preamble,
protocol timeout or RF-limit change accompanies this classification fix.

## Last Device Event

`iohc event CHANNEL status` displays the last authenticated non-position event;
`iohc event CHANNEL clear` clears this diagnostic slot. Both are local-console
operations and send no RF. These are session-only raw diagnostics, independent
of battery-monitoring configuration. No new ETS object or flash schema is added.

A paired 2W device's own-addressed event can enter the existing inbound challenge
exchange when its five-byte envelope is complete and its declared body length
matches exactly. Short position events and malformed short events are rejected.
No record is stored before proof. The original saved payload, key, identity,
product revision, frequency, mode and proof deadline are still verified. Unknown
IDs may be recorded as opaque data; bytes are never coerced into sensor units,
battery percentage, position, movement, status-expected or polling activity.

The bounded slot retains source/destination, event ID, the entire DATA envelope,
frequency index, time of successful verification and a saturating counter. It
contains no keys. Node/key/mode/product-context/gateway changes clear the slot,
as do restart and explicit diagnostic clear. Position events do not replace it.
Proof replay cannot increment it. An event can be recorded even if the existing
ACK subsequently fails admission; the record confirms proof, not ACK delivery.

This feature handles the authenticated inbound path only. It does not add `0x73`
subscription writes, change final-response association, or claim the complete
original vendor host-event/authentication policy has been reconstructed.
