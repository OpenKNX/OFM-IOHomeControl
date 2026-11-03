# Atlantic issue evidence and conservative diagnostics — 2026-11-03

Sources: [Calypso #198](https://github.com/laberning/home_io_control/issues/198)
and [Tatou #199](https://github.com/laberning/home_io_control/issues/199).
Exact source responses are retained privately under Velocet/Findings; reviewed
fixtures, source hashes and per-observation provenance are in
`test/corpus/atlantic_issues_198_199.json` and its generated header.

## Verified source observations

#199 contains 13 full packet observations: two Discovery 28 packets from D46FF6
to 00FFFB on different channels and eleven broadcast WritePrivate 20 packets.
Their DATA family is exactly `0C 61 01 21 XX 00`. No key is included. Discovery
28 is not a received DiscoveryResponse 29. The diagnostic classifier retains
XX as an opaque byte. With existing event tracing enabled it reports source,
destination, control bytes and frequency, explicitly unverified/observe-only.
It does not interpret function 21 as a key, temperature or mode, or respond.

#198 supplies a message-level pairing timeline and reported metadata, not raw
RF packets. Metadata tests therefore project a synthetic GetInfo response for
manufacturer 0C, profile 33, subtype 0, always-alive and 40 ms turnaround. The
reported eight-message sequence ends without an actual raw 33 packet; no
successful key proof or final 6F is invented. State-machine tests stop at the
missing response instead of declaring pairing successful.

## Source changes and limitations

Discovery taxonomy names 33 domestic hot water and 34 electrical heater. The
latter taxonomy does not prove Tatou advertised that profile. Values above FF
cannot alias a smaller enum profile. New identities remain diagnostic; ordinary
actuator Execute and channel control cannot inherit a shutter/Cozy mapping for
33/34, including a manual override. Existing dedicated, separately qualified
heating register paths retain their own tuple and authentication gates.

There are no new ETS appliance controls, broadcast-triggered pairing changes,
key recovery claims or automatic gateway responders. A gateway experiment is
still a separate, explicitly armed hardware investigation. These changes do
not establish compatibility with a Calypso or Tatou on real hardware.

## Regression coverage

The native fixture tests preserve all thirteen packets, reject malformed
selector shapes, reject unrelated 28 during normal pairing, preserve key and
paired state on broadcasts, and exercise trace-on/off behavior. Synthetic
metadata and partial pairing tests carry separate provenance. The production
channel guard is also compiled directly by `test_atlantic_issue_guards.py`,
including manually configured actuator selections and large profile values.

Remaining physical gates: full Calypso pairing with actual 33/proof/6F bytes;
Tatou discovery correlated with another controller and all channels; an
explicitly authorized gateway experiment; model-specific authenticated command
captures before enabling any controls. No RF experiment was run here.
