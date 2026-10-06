# Recovered protocol implementation status

Tracks the 33 numbered points in the supplied 2026-10-05 TODO. Native tests verify software; hardware and ETS qualification require original peers, capture equipment, power switching and ETS. Unknown formats remain disabled.

## 1. Directed deadlines

Implemented all 64 CTRL1/MIB deadline entries. Complete discovered MIB selects class; missing/incomplete MIB uses a conservative 1011 ms fallback. Actual outgoing CTRL1 determines the row, including authentication continuation. RX timers begin after TX completion. Timing diagnostics carry deadline, row and fallback. Exchange ceilings are operation-specific host limits (5/9/20 seconds), separate from each recovered response deadline. Pairing, scan and responder guards retain their independent fixed limits. Native tests cover all entries and delayed TX completion with LPM classes 2/3. Physical timing verification remains pending.

## 2. Operation-specific session policy

EXECUTE uses mode 3 and the conservative recovered five-attempt budget; nine is available for a proven zero SetBeacon/SystemId match count; incomplete topology falls back to five. Favourite commands remain single-attempt. PRIVATE has a separate three-state-attempt host policy; modes 3/4 require producer-specific evidence. RF media failures have a separate five-attempt cap, independent of state retries. Whole-session replay is disabled (one session), particularly after authentication. Pairing/key-exchange state machines remain independent. Callback outcomes distinguish media failure, unanswered transaction, authenticated/no-close, explicit rejection and exhausted state budget. Diagnostics expose session mode and counts.

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

## 13 — Operational RCM prerequisite and ownership

Connected the lifecycle to controller RF queuing and the tracked-node database.
BasicNode SetBeacon marker/SystemId values require explicit evidence import; they
are never inferred from discovery MIB, profile or display name. Unknown markers
block entry. Zero matching marked records enters base mode directly; one/multiple
select context variants 0/3 and send the recovered empty 36 request. Only actual
37 with at least three bytes enters base mode; FE cannot enter it. The model owns
600/300-second timers independently of discovery, with a chosen 5-second host
prerequisite guard. Peer/key/revision changes, cancellation and stale tokens block
queued work; other queued commands cannot take ownership while RCM is active.
Automatic acquisition of SetBeacon database evidence and peer qualification remain
pending. This does not emulate every reference recipient-side configuration event.
Source: dated STM32-2W report, sections “RCM action 0x0B” and “SystemId 9/5 selector”.

## 14 — SensorEventDelegation record/selector service

Added named 8D/8E commands, exact 8-byte RAM records, identity/revision-bound
upsert, exact/controller/sensor removal, non-mutating FF query and strict four-byte
8E numeric result parser. Capacity 16 is an OFM bound. No persistence is added
because lifetime is unproven. The caller must authenticate/resolve SensorNode
identity before applying a selector. No automatic subscriptions or KNX alarm
publication is enabled. Generic slave RF dispatch/response production remains
outside the present actuator-controller role; this is the reusable database service.
Source: dated 2W report “0x8D/0x8E” and “delegation response payload”.

## 15 — Object write allowlist and early rejection

Added an explicit empty RF write allowlist and rejected 48 at queue admission,
before any RF side effect. Offline preparation remains peer/key/revision/token
bound, bounded and cancellable; it grants no production write permission.
The dated reports explicitly leave writable 4300/4302 and 8100/8103 schemas,
product authorization and peer result semantics unresolved. No generic arbitrary
provider/key write or guessed terminal closure is introduced.

## 16 — 8100/8103 member serialization

Reviewed the full dated reports; they explicitly retain this gap. Evidence requirements and current gates are recorded in `docs/qualification/todo-16.md`. No speculative schema/meaning/model binding was added.

## 17 — 4300/4302 writable schemas

Reviewed the full dated reports; they explicitly retain this gap. Evidence requirements and current gates are recorded in `docs/qualification/todo-17.md`. No speculative schema/meaning/model binding was added.

## 18 — A607 key/state bridge

Reviewed the full dated reports; they explicitly retain this gap. Evidence requirements and current gates are recorded in `docs/qualification/todo-18.md`. No speculative schema/meaning/model binding was added.

## 19 — Unknown FP/alias/GI meaning

Reviewed the full dated reports; they explicitly retain this gap. Evidence requirements and current gates are recorded in `docs/qualification/todo-19.md`. No speculative schema/meaning/model binding was added.

## 20 — Exact commercial/generation binding

Reviewed the full dated reports; they explicitly retain this gap. Evidence requirements and current gates are recorded in `docs/qualification/todo-20.md`. No speculative schema/meaning/model binding was added.

## 2 follow-up — Recovered SetBeacon/SystemId selector

The newly readable dated report closes the discriminator: mode 3 uses nine when
no SetBeacon-marked BasicNode matches current SystemId, five otherwise. Added
explicit current-SystemId import and database query; incomplete marker inventory
or changed global key falls back to five. No SystemId is invented from a key hash
or MIB. Nine-attempt mode has a separate 20-second OFM host ceiling; this is not a
recovered reference duration. Favorite/authenticated repeat safeguards stay intact.
Automatic acquisition of the reference BasicNode marker inventory remains open.

## 21 — Physical bounds with provenance

Added volatile per-channel validated temperature bounds with source, evidence ID,
node and semantic revision. Missing provenance, reversed/equal limits and stale
identity/revision cannot supply codec context. Every channel context invalidation
also discards bounds. Diagnostic MP/FP defaults are never promoted to physical
bounds; no inverse RF write permission follows from storing context. Dynamic
product-specific bounds acquisition and unidentified sentinels remain unresolved
in the dated report and require original-peer/product evidence.

## 22 — Per-family/FP write qualification table

Added explicit RGB FP10/11 and tunable-white FP14 qualification rows requiring
exact product identity, RF request format, auth/session, units/range and original
peer acceptance. Every current row is denied; absent rows are denied. Product
access now obtains write permission from this table rather than representation
knowledge. Raw reads and offline encoders remain independent. Enable one row
only with retained product-specific original-peer evidence; exact commercial
binding and physical write acceptance are not yet qualified.

## 23 — KNX publication gate review

Reviewed existing product publication/trust/freshness and coherent RGB tuple guards. Advanced permissions remain false pending exact identity and original-peer physical qualification. Documented standard-profile availability versus authentication, and the enablement evidence checklist in `docs/qualification/todo-23.md`. No guessed units/DPTs or correlated-to-authenticated promotion is introduced.

## 31 — New commissioning ownership coverage

Exact GetKeyOfNode and RCM exclude unrelated queued jobs while they own the
controller. RCM and topology evidence are revision-bound; unknown channel markers
cannot masquerade as a complete zero-match inventory. Global-key changes cancel
RCM and invalidate the current-SystemId retry context. Cancellation/stale tokens
block unsent work; no new operation resumes RF after reboot. Exact key adoption
uses the existing durable network path; temporary RCM/delegation data is volatile.
Existing ETS jobs/receipts continue to distinguish candidate recognition, explicit
project adoption and required download. Full durable receipt/history coverage of
all console commissioning paths remains open, as does physical cancellation/ETS
qualification; no software audit is claimed as that acceptance.

## 33 — Current behavior documentation

Updated MIB deadline comments, discovery budget diagnostics/console range, channel
diversity ETS label/help and old “no timeout effect” statements. Invalid console
input no longer silently resets the override. Raw KLF hints and MIB bit-5 meaning
remain distinct from recovered CTRL1 LOW_POWER timing. RCM/key/discovery status
above explicitly separates implemented software from unqualified hardware and
unresolved business schemas. The overview below is the current completion state.

| Points | Current state |
| --- | --- |
| 1–4 | Recovered selectors/descriptors and session policy implemented; automatic BasicNode topology acquisition and some producer-specific PRIVATE modes remain open. |
| 5 | Sensor and Somfy private producers integrated; private replies retained raw, unproven fields not decoded. |
| 6–8 | Strict identity acceptance, multi-return lifecycle and recent-channel diversity implemented. |
| 9–10 | Runtime trials, measurements/recovery diagnostics and software checks implemented; production tuning requires physical evidence. |
| 11–12 | Exact key primitive and 1W gap visibility implemented; peer resynchronization procedure remains unproven. |
| 13 | RCM prerequisite/timers/ownership integrated; marker evidence import is explicit, automatic acquisition/full recipient emulation and qualification remain open. |
| 14 | Delegation record/selector/reply service implemented; generic slave RF dispatch remains outside the present role. |
| 15 | Object writes explicitly denied before enqueue; no qualified writable schema. |
| 16–20 | Reports explicitly retain unresolved schemas/semantics/commercial identity; evidence requirements recorded, no guessed implementation. |
| 21–23 | Bounds provenance, write qualification and publication gates implemented/reviewed; physical product qualification remains pending. |
| 24–30 | Separate case-complete bench/ETS procedures and evidence gates committed; actual runs not performed. |
| 31 | New owners isolated and stale context blocked; full console receipt/history coverage and qualification remain open. |
| 32 | Unified queued-exchange record extended; standalone pairing/discovery retain separate telemetry. |
| 33 | Current comments/help/docs aligned; unresolved facts stay labelled. |

This work does not establish protocol-complete release qualification. The dated
source reports copied into OAM docs are user-provided evidence inputs and are not
modified or committed as part of these changes.

## 12 follow-up — Confirmed durable floor

Diagnostics now retain the successfully loaded/committed journal floor separately
from the RAM allocation cache. A failed first commit reports an unknown durable
floor, not the rejected reservation; fault injection also verifies RF stays silent.

Sequence diagnostics resolve shared 1W channels to the actual counter owner and
report its owner index. Cached durable state from a different node/key is unknown,
not a floor for the new identity. Shared-owner and key-change tests cover this.
