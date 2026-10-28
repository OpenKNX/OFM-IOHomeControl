# Foreign activity and status polling

Only parsed, supported Execute payloads can request an observational follow-up:
main-parameter-only (6 bytes), or main plus FP1 (8 bytes). 2W requires START;
1W requires the parsed MAC layout and its two sequence bytes. This observation
does not authenticate the action or publish a position.

A directed action follows only its paired 2W destination. A known, linked 1W
remote may address a typed/all 1W group. Unknown destinations, actuator-originated
traffic, reads, replies, challenges, errors and unsupported shapes do not fan out.
Repeated copies are coalesced in a fixed per-channel 2-second observation cache.

An existing tracking epoch retains its deadline, next poll, silent/authentication
failure counters, moving misses and ownership. This applies while a poll is
queued, in flight or backed off. Even a different remote action coalesces within
that bounded epoch. A qualified fresh action after expiry creates a new bounded
epoch; passive replies cannot create one. Epoch checks continue to reject old
queued work. Local command/STOP/manual policies retain their existing path.

The status-attribution diagnostic includes bounded foreign decision counters
and the latest source/destination/opcode. Scheduling logs cite the actual Execute
observation rather than treating a previous status attribution as fresh evidence.

Validation uses the shared production scheduling policy, real controller frame
injection, existing ownership/security tests and the pinned RF corpus. Native
tests do not prove actuator reception or physical timing. Hardware A/B remains
required: identical peer/settings, second controller off/on, no re-pairing or
configuration writes, production RF limits, and separate counts of attempts,
transactions, ignored reads, coalesced actions and real responses.
