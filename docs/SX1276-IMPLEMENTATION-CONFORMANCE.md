# SX1276 conformance status

The branch retains the existing SX1276 FSK configuration and preamble presets. The reverse-engineered STM32 radio values and legacy software-UART framing do not justify silently replacing these settings.

| Setting | Current implementation | Meaning |
| --- | --- | --- |
| Crystal / bit-rate divisor | 32 MHz / 833 | 38,415.366 bit/s; nominal 38,400 |
| Deviation divisor | 314 | 19,165.039 Hz |
| RX / AFC bandwidth | `0x13` / `0x13` | 41.667 kHz |
| Preamble detector | `0xAA` | enabled, two-byte detection window, ten chip errors tolerated over that window |
| Initial preamble | 8 | FSK bytes, about 1.666 ms |
| Driver start / response defaults | 32 / 12 | FSK bytes; the controller may override these per exchange |
| Shaping / PA ramp | Gaussian BT=1.0 / 15 µs | retained working configuration |

`setPreambleLength` writes an unsigned 16-bit **byte count** directly to the FSK preamble registers. A legacy stream of 10, 54 or 1960 software-UART bytes, including framing bits, is not the same count in these registers. `PowerFrame` behavior and actual generated wake duration still require observation on the chosen SX1276 hardware.

Before qualification, capture the actual configured register snapshot and over-the-air waveform for ordinary commands, short responses, low-power wake and 1W enrollment. Measure preamble, sync, payload and RX/TX turnaround separately. Exercise preamble detection with DIO4 wired and absent, scan hold, simultaneous traffic, timeout and CRC failure. Native SX1262 PHY tests do not qualify the SX1276 driver. Compilation checks also do not establish interoperability.

Frame codec support for the version-3 `0B 01` header is implemented. This does not mean every controller authentication/continuation producer chooses version 3; that policy needs an original-device exchange capture before changing outgoing defaults.

Controller 3D now preserves the extended form of its authenticated working request. `iohc 2wdiag version auto|3` supplies an explicit queued-2W bench override; ordinary defaults and separate pairing/discovery producers remain unchanged. See [peer qualification procedure](SX1276-PEER-QUALIFICATION.md) for the unexecuted hardware acceptance checks and product-bound high-FP representation vectors.

## Combined product snapshot GET — 2026-10-05

The native OVPd `getMpFp` combined endpoint `0x10012` passes the raw two-byte
selector after `03`. `requestMpFpMaskRead` now uses this established path.
Logical mask bit0=FP1; RGB requests MP+FP10+FP11 with `03 00 60` in one
exchange. All requested FPs must be present before any observation is ingested.
The existing identity/key/context guards and one-attempt budget apply. A shared
reply generation proves correlation/coherence, not authentication: unsigned
responses remain Correlated, and RF-write/KNX-publication gates remain closed.

Function-property `32 channel maskBE16 expectedNodeBE24` returns
`result schema channel maskBE16` (5 bytes); capability bit6 advertises support.
ETS uses one combined request when supported and retains the individual-read
fallback for older firmware. The RGB KNX read trigger also queues one request.
Native tests exercise selector mapping, incomplete-tuple rejection, atomic
complete-tuple ingestion and absence of legacy position publication.

## Explicit default sensor subscription — 2026-10-05

`iohc sensor subscribe-default NODE BACKBONE` builds only the recovered canonical
`8B` write: zeroed 17-byte body, byte3=1, bytes13..15=the explicitly supplied
established target backbone, byte16=CC. Both addresses are hex. It rejects
zero/oversized backbone addresses, non-Sensor identities, 1W/unpaired channels,
RF ownership conflicts and concurrent queued work. Key and semantic-context
snapshots invalidate a stale queued write. The exchange has one attempt and
retains the existing 8C >=17-byte response decoder as correlated evidence.

This is an expert configuration operation, not automatic subscription or a
physical acceptance result. No unknown bytes0..12 are exposed as settings, no
SensorInformation reply layout is reused as a write, and no sensor units/KNX
publication or event-monitoring policy are inferred. `sensor info` remains the
separate read-only `8B FF` operation. Source: OVPd serializer
0x0018C698..0x0018C900, documented in the 2W report.

## ETS effective-setting summary — 2026-10-05

The normal commissioning view now includes an offline project-setting summary.
It displays the four independently controlled recognition values and ownership,
counts fields whose ETS values are preserved, applies the profile-override veto
and exposes retained protocol/power/suspend settings. Hiding expert controls
never clears their values. This reports project configuration, not a claim that
firmware has downloaded it or a fabricated history of how a value was adopted.
The online evidence view includes the same summary. No parameter memory, KO
number, saved permission or expert value changes. Executed ETS JavaScript tests
verify independent ownership, override precedence and absence of mutations/RF.

## Management ownership and semantic invalidation — 2026-10-05

Priority/sensor-status/sensor-information starts now require an idle managed RF
owner. Their queued reads snapshot the semantic-context revision as well as
node/key. Current samples are hidden on any revision change; old priority
refreshes are retired rather than querying a reclassified node. This prevents
same-address/key metadata changes from preserving stale sensor meanings. The
existing raw schemas, correlated trust and unknown physical units are intact.
