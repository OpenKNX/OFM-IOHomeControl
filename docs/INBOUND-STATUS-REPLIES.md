# Inbound 2W status authentication and replies

A directed, parsed 2W StatusUpdate (0x71) from a paired actuator starts its own
inbound transaction while the controller is idle. A 1W peer, foreign recipient,
unknown source, short payload or an existing outgoing exchange cannot obtain a
fast reply or publish that status. Receipt of 0x71 and transmission of the 0x3C
challenge leave the saved payload untrusted.

The controller records a fixed channel/key/revision/node snapshot, local
identity, transaction generation, actual receive frequency and MCU packet-read
time. It may transmit exactly the generated 0x3C on that channel without an
additional carrier-sense/RX-settle step, inside the original 25 ms local bound.
Busy preparation never renews that timestamp. Expired inbound replies are
discarded, rather than sent unchecked or retried indefinitely.

The later 0x3D must have the correct source, destination, frequency, frame mode,
length and active receive deadline. Its HMAC is verified against the original
saved command plus payload and the snapshotted key. Pairing, deletion, rekeying,
revision/local identity changes, intervening packets, cancellation, sleep and
radio errors revoke the context. Proof consumes the nonce before the saved
status is dispatched. Invalid proof cannot publish position, movement or poll
success. A status ACK does not confirm any actuator command or physical action.

## ACK channel policy and evidence limit

Only verified status creates a StatusUpdateResponse (0x72) grant. Its deadline
begins after successful HMAC verification, not the original 0x71 reception.
The first ACK goes on the original status receive channel; at most that copy can skip additional
CCA. The other two copies retain the existing scan order with ordinary CCA,
radio readiness and duty admission. All three existing channel copies and the
END/LOW_POWER flags and `05 00` payload are retained. The old fixed order was
868.95, 869.85, 868.25 MHz; the new first channel is explicitly traced. An
unconfirmed TX completion, failed first ACK or expired transaction stops the
copy sequence. Accepted airtime is never refunded.

The local audit found status-only public RS100 fixtures in
`test/corpus/audit-20261018/somfy_rs100_statuspoll_kig300_sx1276.yaml` and parser
fixtures in `test/corpus/golden_rf_corpus.h`. They do not provide a complete
0x71/0x3C/0x3D/0x72 exchange proving the required ACK channel order or copy
count. No new RF capture was collected. Receive-channel-first is therefore an
implementation policy awaiting physical qualification; retaining the other
copies avoids asserting that a single ACK is sufficient. This policy must be
reviewed against real Somfy and VELUX exchanges before release acceptance.

## Grant provenance and bounded admission

Explicit mutually exclusive reply roles are `outbound_answer`, `pairing`,
`inbound_challenge` and `verified_status_ack`. Each fast send requires its
trusted producer, expected controller state and opcode, unchanged channel/key
identity, peer/local nodes, transaction generation, frequency and age. A raw
sender cannot borrow a grant even by using a continuation opcode. Another RX,
error or successful TX invalidates it. Trace records distinguish the role and
`qualified_reply` from debug/bench bypass. Keys, challenges and HMAC bytes remain
redacted.

CCA retry state uses a fingerprint of candidate bytes, frequency, preamble,
state, local identity and session; a new candidate starts a new bounded attempt
sequence. This fingerprint controls only retry bookkeeping, never permission
to skip CCA. Radio readiness and per-band airtime admission still apply to every
frame. The 25 ms bound remains a local heuristic, not a measured universal peer
deadline or spectrum-access qualification.

## Verification and remaining release gates

`make -C test run` includes inbound handshake, wrong MAC/identity/channel,
1W/foreign/malformed frames, cancellation, expiry at 24/25/26 ms, unsigned timer
wrap, lost TX completion, busy/invalid CCA, duty denial and cross-candidate
ownership tests. `test_inbound_reply_mutation.py` compiles a temporary controller
copy and independently restores CCA on inbound 0x3C and verified 0x72. Each
mutation must fail the busy-RSSI handshake regression; the repository source is
never modified by that test.

`two_way_access_recovery_campaign` in the release-evidence template also
requires genuine unsolicited handshakes, ACK channel/copy traces, cancellation
and peer reliability checks. These remain `not_run`. Collect RX on-air end,
packet read, TX request/start/done, RX ready, actual position and firmware SHA
with SX1276/OXIMO 50 io, SX1262, sleeping VELUX and multiple peers. Repeat under
strong/weak RF, occupied channels and wrong keys, with bench bypass off. Keep
Atlantic, 1W, NVS/power-cut and signed ETS upgrade gates open. Native tests and
firmware builds cannot qualify on-air timing or regulatory access conditions.
