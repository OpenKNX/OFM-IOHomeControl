# Dual-controller diagnostics

`AABBCC` identifies the second ESP running laberning/home_io_control in the supplied field analysis. It is distinct from OpenKNX `E4E61D`; neither identity establishes historical pairing ownership or key equality. This implementation preserves existing bindings, system key, flash schema 18 and ETS application layout. It does not initiate extraction, enrollment, configuration 6F, reset or movement during diagnostic scan/verify.

## Controlled capture

Record both firmware revisions, hardware/radio, configured identities, target IDs, polling settings and synchronized action timestamps. Save continuous logs. Use one command at a time:

```text
iohc duty debug off
iohc duty status
iohc polls pause 120
iohc pairdiag on
iohc keyimport capture on
iohc keyimport scan spe
iohc keyimport diag
iohc duty status
iohc keyimport verify 155D81
iohc keyimport diag
iohc radio
iohc keyimport capture off
iohc pairdiag off
iohc polls resume
```

Wait for actual duty recovery before transmitting. Turning debug off never refunds airtime. Diagnostic entry rejects bypass mode and central diagnostic admission retains duty and CCA enforcement even if bypass is subsequently selected. Background pause is volatile, capped at 300 seconds, and does not interrupt an active exchange or mandatory received replies. Diagnostic scan/verify reserves a 120-second quiet interval; a longer existing pause is retained. `iohc polls status` reports remaining pause. Queued owned polls revalidate identity/key/owner/epoch and defer locally during pause; they cannot preempt an active verification response window. Manual reads remain explicit foreground actions.

Perform A with the second ESP powered off, preserving its flash; B with OpenKNX powered off and the second ESP logging its own movement; C with both enabled and otherwise equivalent settings. An RF-disabled transmitter is not an independent sniffer. Collect three independent admitted scans per condition and retain each summary. `scan spe` requests 2A/2B; `scan standard` requests 28/29 using the existing respective producer/settings. They are different protocol families, not interchangeable wire requests. The default `scan` remains SPE. Neither mode changes existing listen durations or preambles. Previous evidence is printed before ledger reset. Stored bindings do not count as active replies.

`IOHC_DISCOVERY_WINDOW` records actual request headers and payload length (authentication body redacted), requested/effective listen duration, TX-done/RX-ready gap, receive activity and counter deltas. Missing preamble support stays unavailable; `rx_missed_by_retune=unknown` is not an inferred lost packet. Fresh 29 and 2B observations remain separate from stored and directed evidence. An unanswered window does not establish that a peer transmitted nothing.

## Collision and verification evidence

A received own-source 2W frame latches only with admissible physical receive, timestamp, frame contract and checked valid CRC evidence, without FIFO overrun/truncation. `IOHC_COLLISION_FIRST` is synchronous and immutable for the boot: identity, boot/session/time/transaction, header, frequency/RSSI, IRQ/FIFO/CRC provenance, radio/controller state and recent-own-TX context. It contains no body, key or authentication material. Later summaries repeat it independently of the ring. A credible own-ID echo conservatively blocks and records recent TX; proximity alone does not prove an echo. The existing explicit known-identity conflict path is labeled non-PHY and cannot be mistaken for a captured RF frame. No automatic clear or replacement ID is implemented. Stop active tests on a collision and inspect its provenance.

`IOHC_VERIFY` separates local duty/CCA/collision/driver/queue failure, transmitted no reply, 2F without full proof and authenticated success. Peer silence requires actual TX-start, TX-done and RX-ready evidence. Charged airtime is recorded only for accepted transmissions; local denial has `peer_no_reply=0` and does not increment candidate timeout. Conservative duty accounting retains two aligned buckets, so recovery can take longer than one hour. A visible 2F alone is not authentication.

Read-only ETS capability bit 9 adds API 42, schema 1, 25 bytes: schema/result/flags, node BE24, peer-timeout byte, transaction low32/frequency/charged-ms/used-ms BE32, duty mode. Results 0..9 are idle, queued, transmitted-no-reply, unproven-2F, authenticated, duty-denied, CCA-denied, collision, driver-error, queue-busy. Flags are TX-start=1, TX-done=2, RX-ready=4, challenge=8, authentication-TX=16, response=32. The existing commissioning status read includes this evidence when supported. It never starts RF.

## Status ownership and tracing

`IOHC_STATUSATTR` separates transport source/destination, optional last commander and originator, local transaction, source and authenticated-local-command proof. A local poll can legitimately receive a status whose last commander remains AABBCC. Foreign 04/71 remains observational: no KNX publication, local completion or tracking renewal. Foreign native 04 countdown decoding requires a recently observed matching main-info 03 request; unsupported layouts remain unknown. Trusted local correlation is distinct from cryptographic authentication. An authenticated inbound 71 follows the existing separate challenge context and reply gate.

Tracking start/expected/update/deferred/failure records include reason, owner, epoch/deadline and transaction plus explicitly marked current/cached receive evidence. A qualified stopped response ends its session; passive packets cannot start a replacement session. Poll records distinguish scheduled, tracking, remote-followup and manual queue/TX events, with own charged airtime and admission context.

`IOHC_CAPTURE` marks actual `rx_observed` as physical RX; TX-start/TX-done are local transmission events and `rx_ready` is radio rearm carrying the outbound snapshot. `trusted_status` and `authenticated_trust` are separate. Filters replace one another, with a new epoch and filtered/dropped counters. Use complete `capture on` for speed comparison; a `cmd 71` filter cannot establish reporting support or compare ordinary motion. Absence of 71 means not observed, including unknown routing, enrollment and dwell coverage.

Optional `iohc keyimport capture critical on|off` streams bounded critical headers independently of ring filtering, at most four records/second, with cumulative drop accounting. Bodies stay redacted. Streaming is opt-in and its hardware latency is unqualified. Collision first-trigger evidence does not depend on this stream. The OAM REG1 ESP32 IP build uses 14 volatile trace slots to retain static DRAM headroom; other release targets retain 24. Ring loss must be checked before analysis.

## Offline evidence

```sh
python3 tools/dual_controller_timeline.py full.log --csv timeline.csv
python3 tools/dual_controller_timeline.py slow.log --compare normal.log --node 155D81 --from-percent 20 --to-percent 80
python3 tools/sanitize_peer_config.py peer.yaml --compare-keys local.yaml --firmware-sha abcdef123
```

The timeline tool never sends RF. Comparison requires complete unfiltered, loss-free admitted traces, actual observed Execute provenance, consistent last commander, checked-valid CRC, one fixed target and a bracketed common position interval. It reports interpolated seconds/10%, median velocity, sample gaps and remaining-time error only where the observed target matches the interval endpoint. Missing RF samples remain unknown; unmatched captured polls are not proven RF losses. Split separate boots/runs before comparison. Original field logs are required to produce actual speed findings.

The sanitizer uses an allowlist and never prints keys, authentication bytes or unrelated secrets. Optional local key comparison emits only same/different/unknown, with no fingerprint. Secret/include tags are never resolved and produce unknown comparison. Configuration identity is not proof of an on-air identity. Record subscription/6F history from existing logs without sending speculative configuration writes. Laberning configuration source: https://laberning.github.io/home_io_control/cfg_index.html and https://github.com/laberning/home_io_control .

## Regression provenance and remaining physical work

`test/corpus/stefan-dual-controller-20261027.json` contains the three supplied payload excerpts: 64, 61 and 52 seconds, with AABBCC optional command records. Full original headers/CRC records and the seven original logs were not available for independent validation. Tests use explicitly synthetic transport envelopes, never claim captured PHY proof, and keep the existing 177 original laberning YAMLs unchanged. No bytes were supplied for the 15:51 target change; target-change negatives are synthetic. Positive high-byte countdowns remain synthetic and hardware-unqualified.

Host checks cover duty saturation/recovery, CCA failure, bypass resistance, collision provenance, foreign/local status separation, countdown decoding, filter replacement, pause/revalidation, unsupported inbound status and tracking ownership. Existing capture mutations, storage, authentication and protocol gates remain required.

Physical A/B/C, repeated admitted scans, normal/slow measurements, actual duplicate-ID cause, 71 routing/authentication, all three actuators, live ETS activation/download/persistence and power-cycle acceptance remain not run. Fresh extraction is a separate conditional workflow after RF isolation, using an authorized supported gateway and preserved backups; it is not a diagnostic fallback or a flash erase. No hardware/release acceptance is claimed by these software tests.
