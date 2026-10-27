# Three-shutter diagnostic procedure

Implemented console commands (2026-10-27). Software tests use sanitized/synthetic
responses; these instructions do not assert successful RF operation on the tester's
hardware. Implementation was reviewed against local OFM 72dedab and its successors,
not assumed from public branch line numbers. The referenced tester text log and its
exact build identity were not supplied; the next build prints its own identity.

## Preserve the installation

Back up the ETS project/configuration. Keep existing keys, paired flash records,
channel mapping and the original hub. Do not start key extraction, clear flash,
re-pair, send SetConfig1, or enable a bench RF bypass for this procedure. Firmware
flash schema remains 18 and application/ETS version remains 4.7.

After restoring flash, startup prints `IOHC_BUILD`, `IOHC_SETTINGS`, `IOHC_DIAG`
and the stored-channel evidence. These identify OAM/OFM revisions, application,
MCU/RF, boot ID, own gateway, key presence, effective discovery overrides, and
session-only original-hub/extraction IDs. No key bytes are printed. Zero transient
hub/extraction IDs after reboot mean unknown/session reset, not key loss. Controller
authorization remains unknown unless a particular directed proof is recorded.

## RF-only test

1. Run `iohc keyimport diag`. Check gateway E4E61D and stored channels E50470,
   155D81 and 562292. `flash=1` and `fw_channel` are stored knowledge, not fresh RF.
2. Enable bounded timing and observation output:
   ```text
   iohc pairdiag on
   iohc keyimport capture on
   iohc keyimport scan
   ```
   The existing-key SPE scan does not enter extraction or assignment. It keeps the
   existing discovery timings, scan order and configured options; the effective
   settings/header and window/timing lines describe the actual operation. Repeat
   3–5 times only as RF admission permits. Each scan starts a new operation session
   and clears the evidence ledger. The duty accounting ledger is never reset.
3. Run `iohc keyimport verify 155D81`. This explicitly accepts a flash-known node
   even if normal import skipped it. It queues one ordinary directed 2E request
   on the existing normal 2W frequency (868950000 Hz), with the production
   preamble plan and exchange timeout. There is no diagnostic frequency retry or
   pairing/configuration finalizer. The existing challenge-response continuation
   may send 3D; 30/39/6F and configuration writes are not part of this operation.
   `IOHC_VERIFY` reports queued, authenticated completion or no verified reply;
   the existing `KeyImport directed timeout` line and RF trace distinguish
   no preamble, sync, challenge and final-response stages when available.
   A 2F observation alone is not an authenticated success.
4. Use the original remote to move, stop and close one shutter at a time. Keep
   the serial stream, including `droppedEvents` and receive-coverage information.
5. Run `iohc keyimport diag`, then disable streaming and verbose timing:
   ```text
   iohc keyimport capture off
   iohc pairdiag off
   ```

Optional stream filters (one filter at a time):

```text
iohc keyimport capture node 155D81
iohc keyimport capture cmd 71
```

This is an incremental drain of the existing bounded RF ring, not a simultaneous
three-frequency recorder. One retained event is drained per service iteration;
ring overwrite is counted. The build's ring capacity is unchanged. Frequency
hopping, active sessions, parse failures and hardware limitations can leave gaps;
`0x71` not observed never proves reporting is disabled or unsupported. Streaming
is opt-in and may affect loop latency; compare timing with streaming disabled.
The hot-path evidence hook performs only bounded memory updates, without logs or
additional radio reads. RF-only operations enforce real duty/CCA admission in
release or explicitly debug-off builds. Active OPENKNX_DEBUG bench mode bypasses
duty/CCA/quotas, including diagnostic admission (see [RF policy](rf-access-policy.md));
all modes retain identity checks, require idle RF and
commissioning ownership, and require a matching already-committed network snapshot.
Their persistence preflight compares state without writing it.

## Read the evidence

`IOHC_NODE` retains independent passive 2F/challenge/authentication, fresh active
2B, directed 2F/authentication and flash flags. Passive authentication means the
existing ordered hub-device 2E→3C→3D→2F proof verified with the system key; it does
not authorize our gateway. Active counts count unique accepted 2B sources and
packets separately. Normal candidate skips report their exact inventory/evidence
reason and remain separate from explicitly requested diagnostic verification.

For dual-controller isolation and updated trace semantics, see [Dual-controller diagnostics](DUAL-CONTROLLER-DIAGNOSTICS.md).

The ledger holds eight nodes (at most 336 bytes) for the smallest REG1-IP target;
`overflow` makes truncation explicit. It retains evidence when an import completes.
A targeted verification increments the operation session but retains preceding
passive/flash evidence; counters are labeled since the last scan/extraction reset.
First/last times are ledger observation times, and `rx_known=0` marks a flash-only
entry. Source/destination, request/response and RX index/RSSI are observations,
not fabricated correlation. A scan may clear prior passive evidence; save its
pre-scan diagnostic dump when comparing runs. Boot/operation IDs distinguish logs
across reboots. No diagnostic evidence or transient hub ID is added to flash.

Illustrative sanitized replay result (not a hardware log):

```text
node=155D81 passive_auth=1 active_2b=0 flash=1 fw_channel=2
activeUnique=2 activePackets=3 duplicates=1
```

RX/counter lines explicitly label lifetime/reset semantics. Firmware cannot know
the live ETS project state until the script reports parameter readback. API 41
is an identity-bound, capability-gated, volatile project report; it changes no
channel binding, key, flash state or RF configuration. `reported_1` is project
activation readback, not proof of device download. It becomes unknown on reboot
or ledger reset. A receipt ACK alone is historical assignment evidence, not a
fresh RF reply. `Status/Erkennung lesen` is labeled stored channel status; use the
separate console verification for a live RF test.

Foreign-destination frames are explicitly passive observations. They do not gain
inbound reply grants or publish trusted channel state. The stream redacts key,
challenge and HMAC command bodies and never serializes a frame's HMAC trailer.
For supported 71 position layouts, last-controller bytes 11..13 and originator
byte 14 are diagnostic metadata; short/zero records stay unknown. Existing 04
handling uses its distinct context and offsets. Inbound authentication pass/fail
and subsequent 72 TX events remain separate. Optional SetConfig results are
session-only numeric history (255 unknown, 0 not attempted, 1 accepted70,
2 rejectedFE, 3 timeout, 4 TX failure), separate from observed 71 traffic.

## ETS reconciliation and remaining confirmation

For each result the script reports node, evidence, mapped firmware channel,
activation before/after and result status. Firmware logs modern assignments and
legacy API1C batch mappings with the import session. Parameter setters are read
back; critical Active, mode, displayed Node ID and type must exist and agree.
A silent setter rejection fails before receipt ACK. Matching flash-paired disabled
channels are reactivated through the existing channel/receipt rather than moved.
Repeated reconciliation skips unchanged setters and already-acknowledged receipt
writes. Manual profile ownership and expert settings remain preserved.

The deliberate preview confirmation is still required. Automatic approval review
rejected removing it because that changes when persistent ETS assignment writes
occur. The requested first-click completion is therefore **not implemented**;
readback/idempotence tests cover the existing confirmed path. A separate proposed
patch in the OAM review documents shows the precise confirmation removal for user
review. No claim is made that a real ETS host refresh/reprogramming roundtrip or
three-device RF replay has been physically validated.

## Coverage and external gates

Production console/observation branches are compiled with recording stubs. Tests
cover the three-node provenance/duplicate case, timeout versus proof, ledger
overflow/reset, flash-known targeted verification, unsafe preconditions, actual
duty/CCA rejection even under simulation bypass, short/zero 71 metadata, existing
foreign/inbound authentication guards, silent ETS setter failures, existing-channel
activation, legacy host collections and repeat receipt reconciliation.

Real three-shutter RF/PHY, continuous spectrum coverage, original-hub operation,
power-loss/reboot and signed ETS import/download/UI refresh remain tester gates.
The diagnostic build and corpus results do not qualify those gates.
