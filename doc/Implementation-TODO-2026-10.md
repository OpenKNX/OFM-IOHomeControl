# Recovered protocol implementation status

Tracks the 33 numbered points in the supplied 2026-10-05 TODO. Native tests verify software; hardware and ETS qualification require original peers, capture equipment, power switching and ETS. Unknown formats remain disabled.

## 1. Directed deadlines

Implemented all 64 CTRL1/MIB deadline entries. Complete discovered MIB selects class; missing/incomplete MIB uses a conservative 1011 ms fallback. Actual outgoing CTRL1 determines the row, including authentication continuation. RX timers begin after TX completion. Timing diagnostics carry deadline, row and fallback. The bounded exchange ceiling is 5 s so three conservative waits fit. Pairing, scan and responder guards retain their independent fixed limits. Native tests cover all entries and delayed TX completion with LPM classes 2/3. Physical timing verification remains pending.

## 2. Operation-specific session policy

EXECUTE uses mode 3 and the conservative recovered five-attempt budget; the 9-versus-5 database discriminator remains explicitly unresolved. Favourite commands remain single-attempt. PRIVATE has a separate three-state-attempt host policy; modes 3/4 require producer-specific evidence. RF media failures have a separate five-attempt cap, independent of state retries. Whole-session replay is disabled (one session), particularly after authentication. Pairing/key-exchange state machines remain independent. Callback outcomes distinguish media failure, unanswered transaction, authenticated/no-close, explicit rejection and exhausted state budget. Diagnostics expose session mode and counts.
