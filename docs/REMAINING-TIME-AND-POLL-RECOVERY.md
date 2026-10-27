# Native remaining time and bounded moving-poll recovery

Implemented against OFM f983459 and successors, 2026-10-27. Application 4.7,
flash schema 18, stored keys, pairing and ETS mappings are unchanged.

## Source boundaries

- [Lars Berning and contributors, laberning #184, 8214545](https://github.com/laberning/home_io_control/commit/821454537355a1d20f8f8622889611f4f491a11b)
  supplies the native 03/04 decoder interpretation and tests. The main-info
  reply's optional bytes 6–7 are BE16 remaining seconds. Request context matters;
  an eight-byte execute ACK is not the same layout. #184 adds no capture YAMLs.
- [Velocet command reference, b7a526d](https://github.com/Velocet/iown-homecontrol/blob/b7a526d07506f5192548aadfecf68d0f6e5497fa/docs/commands.md)
  identifies Overkiz A603/142 as a remaining-seconds parameter and describes
  variable RF replies. It does not independently establish native byte offsets.
- [VELUX KLF 200 API 3.16, manufacturer document mirrored at device.report](https://device.report/m/533a5d7f5adc938b0c19129716e3b64b3d52d913af7da08a4f8376e9a1a3da69.pdf),
  printed pages 70–71 (Main Info) and 44 (node state), corroborates seconds and
  sentinel semantics. Positive range ends at 65533; FFFE is beyond that range and
  FFFF undefined. Zero may mean unknown or at target. This gateway/serial API is
  not a native RF layout definition; the separate Velocet-hosted API 3.18 PDF was
  not independently inspected for this implementation.
- [Issue #178](https://github.com/laberning/home_io_control/issues/178) supplies
  empirical SX1276 polling/response motivation, not permission to bypass busy
  channels or increase regulatory airtime. No upstream general LBT policy was copied.

## Native request qualification

Only an owned, correlated main-info 03 poll with default MP/FP selectors can feed
remaining time. The controller checks command/parameters, active exchange, peer,
own destination, exact channel, saved key, product-context revision and tracking
epoch. Qualification is frozen before STOPPED feedback ends that epoch.
Unsupported ACK/probe layouts are unavailable; a six/seven-byte reply has no
remaining field. Periodic/untracked requests can log a qualified raw field but
cannot change the countdown callback's timing state. Unsolicited 71 is separate.
No failed/foreign/identity-mismatched response receives a time callback.

`remaining_time` logs rawRemainingTime, remainingTimeKnown, request source and
layout availability. The RF diagnostic trace retains ordinary functional payload
bytes without authentication material. This field never becomes a position or a
movement command.

Positive, zero, unbounded and undefined remain distinct. A precise countdown may
restart interpolation from the newly measured current, only while reported moving
and only when the duration fits the remaining bounded tracking window. Measured
current, independent target, motion KOs and optimistic command state remain separate.
A fresh measured moving report supersedes a pre-STOP optimistic snapshot.

A zero may request a short confirmation only after a positive count in the same
live moving epoch. Unknown/unbounded observations clear that counting provenance.
An already tracked moving report retains normal cadence when its countdown is
unavailable or a never-counting zero. An untracked moving report cannot create a
new polling epoch merely by reporting motion. The estimate only shortens an existing follow-up; it cannot create/extend tracking,
revive a stop, or exceed the earlier scheduled cadence. The normal default is 2 s;
configured shorter intervals remain honored. The low-power floor remains 10 s and
the original tracking deadline remains finite. Peer/key/profile/mode changes,
clear/cancel, a new tracking epoch and confirmed completion clear time provenance.
All milliseconds use wide intermediates and wrap-safe comparisons.

## Recovery and RF admission

Two missed single-attempt tracked polls can keep normal cadence only after a
qualified measured moving response or authenticated owned 71 update. Optimistic commands or current/target mismatch
alone cannot qualify. The third miss enters the existing silence/authentication
ladder; its latch cannot be reset by changing motion or failure classification.
A valid status reply or genuinely new epoch resets grace. STOP-settle multi-try
polls retain their special exchange budget and get no moving grace.

The controller already requires actual request RF-start and matching ownership
before notifying a peer miss. Local admission/driver/auth-build faults now use a
separate bounded deferral callback: no peer failure, grace or streak is consumed.
Deferral adds per-channel fairness, preserves the low-power floor and original
deadline, and leaves duty/CCA admission intact. Logs distinguish requested retry
from `RF_denied`, local fault and peer miss. There is no guarantee that the next
requested poll can immediately transmit.

## What the unchanged captures establish

The original 177 YAMLs remain pinned to c82d38f. Run:

```text
python tools/audit_remaining_time_corpus.py --check docs/remaining-time-corpus-audit-2026-10-27.json
```

The hash-checked structural audit lists every 04 frame and its request context.
There are 22 replies with a matching captured main-info request and 23 more whose
status-poll fixture documents the context but omits the request. All 45 candidate
poll fields have high byte zero. This is not live authentication/session proof.
No original capture in this qualified candidate set establishes a positive value
above 255 seconds. FFFE/FFFF, unrelated ACK fields and unknown request contexts
are never promoted as such proof. The native 256-second vectors are explicitly
synthetic integer-boundary tests.

The original SSL stopped-off-target fixture has a moving frame with count 1 and
stopped frames with count 0; its omitted poll requests remain omitted. The silent
while-moving and stop-settle fixtures preserve contrasting timeouts/preambles.
The awning LBT-contention and short ACK/tilt families keep their separate
transaction meanings. No capture was duplicated, rekeyed or relabeled.

## Validation and physical qualification

Native tests exercise actual controller routing; extracted production channel
methods exercise timing, peer/key resets, publication, overflow, sentinel and grace
behavior. Three permanent mutations must fail: dropped high byte, unqualified
execute ACK timing and stale interpolation overwriting a measurement. Original
SSL off-target/near-target motion mutations remain required.

Host tests and release builds do not establish reliable on-air motion updates.
Before acceptance, test a Somfy always-on and VELUX low-power actuator on SX1276
and SX1262 where available, retaining existing keys. Record profile/MIB class,
20→80% runs, decoded bytes 6/7, reported motion/current/target, elapsed and requested
poll times. Repeat at least ten runs; include a controlled single-loss case,
third-miss backoff, local duty/CCA denials, STOP mid-run and near endpoints, three
frequencies and multi-device fairness. Capture original-remote movement separately.
Keep complete sanitized traces, identify the hardware, and inspect KNX KOs.
Spectrum access, physical timing, reboot/power-loss storage and signed ETS
upgrade/download remain unrun external gates. `release_accepted` stays false.
