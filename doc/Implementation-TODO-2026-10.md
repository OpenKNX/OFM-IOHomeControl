# Recovered protocol implementation status

Tracks the 33 numbered points in the supplied 2026-10-05 TODO. Native tests verify software; hardware and ETS qualification require original peers, capture equipment, power switching and ETS. Unknown formats remain disabled.

## 1. Directed deadlines

Implemented all 64 CTRL1/MIB deadline entries. Complete discovered MIB selects class; missing/incomplete MIB uses a conservative 1011 ms fallback. Actual outgoing CTRL1 determines the row, including authentication continuation. RX timers begin after TX completion. Timing diagnostics carry deadline, row and fallback. The bounded exchange ceiling is 5 s so three conservative waits fit. Pairing, scan and responder guards retain their independent fixed limits. Native tests cover all entries and delayed TX completion with LPM classes 2/3. Physical timing verification remains pending.

## 2. Operation-specific session policy

EXECUTE uses mode 3 and the conservative recovered five-attempt budget; the 9-versus-5 database discriminator remains explicitly unresolved. Favourite commands remain single-attempt. PRIVATE has a separate three-state-attempt host policy; modes 3/4 require producer-specific evidence. RF media failures have a separate five-attempt cap, independent of state retries. Whole-session replay is disabled (one session), particularly after authentication. Pairing/key-exchange state machines remain independent. Callback outcomes distinguish media failure, unanswered transaction, authenticated/no-close, explicit rejection and exhausted state budget. Diagnostics expose session mode and counts.

## 3. Broadcast discovery budgets

Implemented all four destination rows in normal/LOW_POWER modes. Default windows derive from the transmitted destination and CTRL1. A runtime diagnostic override is explicit; zero restores protocol timing. Each window starts after TX completes and RX is restored. The separate bounded 50 ms packet-arrival grace prevents immediate retuning at a detected boundary. Traces show TX end, first/last packet and window close. Physical discovery timing qualification remains pending.

## 4. Response descriptors

Added command descriptors and separated opcode acceptance from application rejection. Confirmation accepts 0xFE; 0x32 folds it without claiming key installation; opening 0x31 still requires the challenge path. Pairing retains foldable raw payload and waits for actual key proof. Normal/management 0xFE remains an explicit application rejection. Raw payload/disposition are retained in timing diagnostics.

## 6. Strict discovery records

Split the permissive diagnostic decoder from accepted discovery records. All authoritative controller discovery ingestion now requires the complete nine-byte body. Source RF address remains ioAddress, raw bytes remain intact, and truncated frames cannot overwrite stored metadata or advance pairing. Length tests cover 0 through 12 and null input.

## 5. Discovery families

Added named 0x94/95 and 0x96/97 sensor discovery producers and strict nine-byte sensor identities, with independent runtime selection and recovered group budgets. In-system discovery signs its own opcode transcript. PrivateSomfy explicitly produces 0x20 `02 F6`, then `02 F8`, and captures correlated 0x21 responses without borrowing Atlantic WritePrivate semantics. Private response fields remain raw because the supplied TODO does not establish their exact body schema. Console: `iohc discovery sensor`, `sensor-system`, `somfy-private`. No version/family discriminator is guessed. Original-peer qualification remains pending.

## 7. Multi-return discovery lifecycle

One session collects all returns across windows/sweeps and reports accepted, duplicate, malformed and silent-sweep counts. Final completion distinguishes empty discovery from successful returns, with post-processing through the existing enrichment/candidate handlers. Truncated bodies are diagnostics only and are no longer delivered as candidates. Cancellation retains already accepted metadata. Silent sweeps never erase prior nodes/passively authenticated candidates. Directed verification remains available after collection. Tests cover multiple peers, malformed records, cancellation and later empty sweeps retaining inventory.

## 8. Recent-channel diversity

Added wrap-safe per-channel successful TX/RX activity timestamps and a 250 ms reference-inspired experimental cooldown. Alternate selection prefers non-current/non-recent channels, with bounded fallback when all are recent. Broadcast response scanning no longer excludes the request channel for its entire window; ordinary background scanning still visits all three channels. Runtime console `iohc radio diversity [0..2000]` controls the experiment. Physical A/B qualification remains pending.

## 9. Scan cadence controls and qualification

Runtime `iohc radio scan-cadence [3..20]` permits 3/4/5 ms SX1276 trials and resets measurement counters. Reports successful retunes, retune failures, maximum retune latency, parsed captures, parse failures, and preamble/sync holds. Production defaults remain SX1276 5 ms / SX1262 7 ms until hardware evidence exists. Native tests verify cadence limits and no retune during sync. Physical CPU-load/missed-preamble/truncation measurements remain pending: replay the same timed peer sequence at 3, 4 and 5 ms, record generator sent-packet count, radio CRC/FIFO counters, logic-analyzer SPI/IRQ latency and external task-load measurements. Counters alone cannot prove how many preambles were missed.

## 10. Radio self-healing qualification

Retained the intentional idle-only 5 s health supervision and three resets per boot; the recovered periodic scanner reinitialization is not treated as a Semtech requirement. Runtime `iohc radio supervision [3000|5000]` supports physical comparison. New counters expose watchdog triggers, failure reason, reset attempts, successful restores and exhausted budget. Native actual-driver register tests confirm selected frequency, RX/AFC bandwidth and RX mode survive reset; sleep is ignored. The driver does not own keys/sequence state or replay queued commands, and controller recovery only runs while idle. Original-peer tests and deciding whether to replace the three-per-boot cap with a rate limit remain pending hardware evidence.

## 11. Exact GetKeyOfNode primitive

Added an internal explicitly invoked peer/revision/token-bound 0x38 challenge -> 0x32 fixed-transfer-key AES/XOR primitive. It commits the imported global SystemKey immediately and attempts checked network persistence before optional authentication. Authentication outcome is separate and never rolls back the imported key. Cancellation/stale context rejects pending work. The ETS extraction/candidate/adoption workflow is unchanged and does not invoke this primitive. Native vectors cover commit-before-authentication and failed optional authentication retaining the key. Hardware qualification remains pending.

## 12 — 1W recovery visibility

Added per-channel current sequence, durable high-water, unused reservation count,
identity revision and skipped-on-restore diagnostics. Advancing to a durable floor
marks possible desynchronization; it never implies a known receiver watermark.
No rollback, range scan, key replacement or automatic re-pair is introduced.
Actuator-specific recovery remains blocked on a captured, proven peer procedure.
A successful RF send cannot clear this warning because 1W provides no peer ACK.

## 32 — Unified queued 2W exchange diagnostics

Extended the existing response timing record with actual CTRL0/CTRL1, version,
attempt, selected preamble/frequency, absolute TX-end/first/final response times,
RSSI, IRQ and terminal exchange result. Existing fields retain MIB classes,
timeout selector/fallback, session/retry counters and raw peer FE result.
SX1276 snapshots include CRC/FIFO and optional edge timestamp validity from the
radio evidence record. SX1262/native explicitly mark that evidence unavailable;
missing edge wiring never becomes an invented timestamp. This record describes
queued controller exchanges; standalone pairing/discovery retain their separate
telemetry. A complete common diagnostic history across those workflows remains
open. No key or authentication payload is added to this record.

## 24 — SX1276 peer and waveform qualification

Added a case-complete procedure and evidence gate in `docs/qualification/todo-24.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 25 — Optional DIO timestamp qualification

Added a case-complete procedure and evidence gate in `docs/qualification/todo-25.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 26 — Semtech prefix and framing qualification

Added a case-complete procedure and evidence gate in `docs/qualification/todo-26.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 27 — 2W journal physical power-cut campaign

Added a case-complete procedure and evidence gate in `docs/qualification/todo-27.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 28 — 1W reservation physical power-cut campaign

Added a case-complete procedure and evidence gate in `docs/qualification/todo-28.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 29 — Storage wear and filesystem qualification

Added a case-complete procedure and evidence gate in `docs/qualification/todo-29.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.

## 30 — Real ETS application migration qualification

Added a case-complete procedure and evidence gate in `docs/qualification/todo-30.md`. Actual qualification remains pending; the evidence template deliberately records every case as not run.
