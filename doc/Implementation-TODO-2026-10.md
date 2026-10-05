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
