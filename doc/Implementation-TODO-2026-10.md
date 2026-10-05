# Recovered protocol implementation status

Tracks the 33 numbered points in the supplied 2026-10-05 TODO. Native tests verify software; hardware and ETS qualification require original peers, capture equipment, power switching and ETS. Unknown formats remain disabled.

## 1. Directed deadlines

Implemented all 64 CTRL1/MIB deadline entries. Complete discovered MIB selects class; missing/incomplete MIB uses a conservative 1011 ms fallback. Actual outgoing CTRL1 determines the row, including authentication continuation. RX timers begin after TX completion. Timing diagnostics carry deadline, row and fallback. The bounded exchange ceiling is 5 s so three conservative waits fit. Pairing, scan and responder guards retain their independent fixed limits. Native tests cover all entries and delayed TX completion with LPM classes 2/3. Physical timing verification remains pending.
