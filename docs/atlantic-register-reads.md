# Read Atlantic registers without enabling controls

`iohc registers read CHANNEL HEX_FAMILY HEX_INDEX COUNT [calypso]` starts one
manual read job. `iohc registers status` shows the token and raw results;
`iohc registers cancel TOKEN` cancels the current job. Up to sixteen sequential
registers are allowed, with one second between exchanges, a two-minute total
budget and three minutes between starts in release builds. Debug builds bypass
the diagnostic cooldown along with RF airtime limits. Each exchange has one attempt and
uses the ordinary RF admission/accounting path. The main loop continues.

Family 00 requires a paired Atlantic 33/0 identity at its observed address.
Family 01 is limited to the existing qualified 52/1 heater identity. Adelis
Classic's actual type/subtype has not been supplied, so its live read capability
is withheld. Its public replies, exact model-name classifier and candidate
mappings can be tested offline without guessing that identity.

The request is exactly `0C 60 family index`; this interface cannot write 0C61.
Replies require the current peer/local address, channel/key/revision, controller
identity/system-key snapshot, job token, frequency and deadline. A closing21
requires the existing challenge/response continuation and the exact four-byte
echo. Empty values, no response, a sent continuation without closing response,
explicit rejection, cancellation and changed identity are distinct outcomes.
The closing reply is an exchange-bound read observation; it is not evidence of
physical actuation or a reconstructed vendor host authentication policy.

Raw mode is the default. Explicit operator-selected `calypso` interpretation
adds read-only mode, first-two-byte tank temperature and lifetime Wh, while
retaining all opaque suffix bytes. Capacity is marked a candidate. Clock,
component counters, heat-pump bitfields and target temperature remain raw.
Adelis room/target/mode interpretations are candidates pending direct display
and identity confirmation. No values are published to KNX, no setpoints or
modes are written, and ordinary appliance controls remain gated.

[Calypso sweep and display observations](https://github.com/laberning/home_io_control/issues/198#issuecomment-6617417409)
and [maintainer interpretation](https://github.com/laberning/home_io_control/issues/198#issuecomment-6624744765)
underpin the decoder. [Adelis evidence](https://github.com/laberning/home_io_control/issues/202)
has different register meanings. Native tests replay every retained reply and
cover four mode/temperature states, energy delta, malformed/short values,
wrong echo/source, missing challenge, key changes, empty values, cooldown,
range pacing, cancellation and bounded silence. Physical qualification and
complete Calypso pairing remain open.
