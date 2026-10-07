# RS100 Solar: dynamic actuator sensors

An RS100 Solar is one RF actuator. TaHoma's #1 shutter / #2 sensor components
are logical components, not a second RF node. No product-name heuristic or
additional channel is used to claim solar support.

## Implemented

After GeneralInfo1 identifies main protocol version >=4 (byte 10), the controller
attempts provider-00 objects **8100 then 8103**, once per node/key/product-context
revision per boot. Restored metadata enables the same reads after reboot.
Reads use the existing authenticated, identity-bound object-transfer path with
1024-byte bounds and deadlines. Both are attempted even if the first fails.
Failed optional enrichment does not fail pairing/import. No periodic polling or
endless retry is added. Explicit refresh is available below.

Each channel retains exact completed raw responses and completion flags.
Key changes, reassignment and product-context changes discard these snapshots.
Unknown schemas remain unknown; even bytes containing 2001/2005/06 do not
activate capabilities. This prevents accidental subscription or sensor publication.

The bounded normalized model holds up to 8 sensor types and 16 event IDs,
parametersManagement and optional autonomy. Its normalized assignment rejects
oversize and duplicate lists atomically. It is not a wire parser.

The supported intersection for subscription requests is 2001 (battery) and
2005 (embedded sensors), and requests require advertised capabilities.
The **server** parameter is 0FB0 with exactly two payload bytes, 20 01 or 20 05.
There is no leading 01 byte. This encoder does not send an RF transaction.

The normalized scalar handler requires matching capabilities: event 2005 and
sensor 06 yield SunEnergy raw and luminance = raw × 110 lux, using a 32-bit
integer without a guessed physical unit or lux clamp. Types 89 (window) and 82
(intrusion) remain available in the capability model for future handlers.
Event 2001/sub-ID 00 stores a 0..100 battery candidate separately; it never
sets percentValid or changes categorical battery-low evidence.
These normalized APIs are tested with synthetic values, not presented as RF
capture qualification.

## Console diagnostics

Channel numbers are 1-based, as in existing battery diagnostics:

- `iohc dynamic CH status`: completed object flags, lengths and exact raw bytes.
- `iohc dynamic CH read`: manually refresh both objects when commissioning,
  metadata refresh and radio diagnostics are idle; requires paired 2W/v4+.
- `iohc event trace on` / `off`: trace owned-node incoming frames with node,
  command, length, bytes, RSSI, radio channel, timestamp and MAC verification flag.
  Event ID, subtype and scalar are explicitly UNKNOWN. Tracing is off by default.

Key-transfer payloads and object chunks outside the bound 8100/8103 sequence
are redacted, including potentially sensitive A607 data.

Tracing is observation, not an assertion that a frame is an authenticated
subscribed event. Existing authentication and actuator processing remain in use.

## Missing evidence / disabled production paths

The supplied implementation document explicitly leaves the 8100/8103 serialized
member mapping, dynamic event RF layout and scalar encoding unresolved. It also
identifies 0FB0 as a server parameter, without proving its RF transaction mapping.
Existing legacy RF 8B sensor monitoring is a different service and is not reused
as an invented bridge. No decoder, subscription RF producer, unsubscribe opcode,
KNX sensor objects or battery percentage publication is enabled by this change.
Adding dormant ETS objects would not make the unknown RF scalar decodable.

Next steps require a real RS100 capture or a proven native decoder/producer:

1. Map both object payloads into the normalized model, including schema validation.
2. Prove the 0FB0 server-to-RF bridge and ACK behavior; record successful
   subscriptions and replay them on reconnect/reboot.
3. Authenticate and bind event ID/subtype/scalar decoding to the current identity.
4. Expose optional luminance KNX objects (DPT 9.004), with explicit handling of
   that DPT's representable range; optionally expose a unitless raw SunEnergy KO.
5. Correlate several dynamic battery scalars against TaHoma before enabling SOC.

Hardware validation: read both objects, subscribe to advertised events, cover /
uncover the sensor, compare raw values 10→1100 and 20→2200 lux with TaHoma,
record battery values at several SOC levels, then reboot and verify restoration
and normal shutter control. None of these physical checks has been claimed here.

Source: user-supplied `RS100-Solar-ioHomeControl-Implementation.md`, read
2026-10-07; corroborating retained KizBox2 analysis in the OAM doc folder.
The compound IDs 0FAF200506 / 0FAF200100 are product-model evidence and a strong
structural inference, not an RF framing rule implemented here.

Validation: 686 native tests, 50 UI tests and the battery-publication regression
passed; SX1262/TP and SX1276/IP release firmware builds passed. Tests include
authenticated object transport, raw-byte preservation, protocol/class gates,
timeout recovery, key/context invalidation, commissioning cancellation, restart
reads and trace key redaction. Physical RS100 validation is still outstanding.
