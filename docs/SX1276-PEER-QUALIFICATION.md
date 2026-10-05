# SX1276 / version-3 / high-FP bench qualification

Status on 2026-10-05: **not executed on hardware**. The host exposes only
Bluetooth-Incoming-Port and debug-console, with no identified SX1276 board or
original peer. Native vectors and builds below are software evidence.

Record board/antenna/wiring (including DIO4), OFM/OAM commit IDs, peer model,
peer firmware, known starting state, permitted actions and second-receiver
capture provenance before an active test. Retain network keys privately;
re-key or redact captures before adding them to the public corpus.

## Controller-originated version 3

1. On a configured paired 2W bench channel, capture the ordinary baseline with
   `iohc pairdiag on`, `iohc 2wdiag status` and a metadata read using
   `iohc metadata refresh NODE` (NODE is hex). Retain raw RX/TX and timing logs.
2. Use `iohc 2wdiag version 3`, then repeat the read. This affects newly built
   queued 2W requests only. It does not change separate pairing/discovery
   state machines, 1W, flash, ETS parameters or automatic negotiation.
3. Check the transmitted request has version3, marker0B01, addresses at4/7,
   command at10, and declared length at most32. If challenged, the generated
   3D must keep version3 from the working request, six MAC data bytes and
   controller continuation START/END clear. An ordinary received challenge
   must not silently downgrade the working request. MAC transcript is the
   original command/data, not the extended routing header.
4. Confirm peer acceptance through a correlated final response. A successful
   radio send or absence of an error is insufficient. Record rejection,
   timeout and wrong-source/wrong-destination outcomes as such.
5. Only after read acceptance and explicit permission for movement, exercise
   a reversible actuator command, authenticated completion and retries.
6. Restore `iohc 2wdiag version auto` (or `iohc 2wdiag reset`), repeat baseline,
   and verify expert settings and normal control remain effective.

The override is frozen in each working request; changing it during an active
exchange applies to subsequent requests. It is an experiment, not a learned
per-device preference. Pairing version3 remains a separate qualification gate.

## Radio measurements

Capture registers and the waveform with an independent receiver/logic
analyzer. Record actual bitrate, deviation, RX/AFC bandwidth, detector,
sync word and preamble register byte count. Compare against
[SX1276 conformance status](SX1276-IMPLEMENTATION-CONFORMANCE.md).
Measure RF preamble, sync/payload boundaries, TX end to RX ready, received
preamble/sync time and peer response separately. Serial logging latency does
not measure those edges. Exercise always-alive and low-power peers, each
used channel, scan hold, DIO4 connected/absent, bad CRC, contention, silence
and retry paths. Change one setting at a time and restore defaults afterward.
Report observed distributions and failure counts; do not declare acceptance
from a single successful exchange or substitute UART prefix counts for FSK
register bytes. 1W enrollment needs its own original-remote/actuator capture.

## Product identity and high-FP writes

The binding helper requires full valid actuator identity and rejects vendor
signature or GI2 profile conflicts. RGB is scoped to profile6/subprofile1,
manufacturer0/2; white to6/2, manufacturer2. Generation bits and commercial
model IDs remain unresolved. Atlantic12/profile22/subprofile1 uses GI2[7..9]
620000 or520001 for PassAPC heat-pump/hybrid metadata. It must not select the
generic normalized HeatPump temperature codec.

`IoHomeProductActivation.h` prepares activation blobs only; it does not enqueue
RF. Keep high-FP diagnostic transmission gated until a source/peer capture
establishes the full originator/ACEI, command, authentication and final
response. Do not use the FP1–3 diagnostic writer for FP10/11/14.

Retained-source/software vectors (not captures):

| Intent | MP/FP blob only |
| --- | --- |
| RGB255,0,0 | 00 00 00 60 7C A9 65 4C |
| RGB black, explicit safe policy | C8 00 00 00 |
| White4250K with ignored raw MP | D4 00 00 04 64 00 |

FP10 and11 must be sorted after FPI2=60; FP14 selects FPI2=04. White's MP
argument is a raw word, not a percentage. Lighting positive truncation and
black handling are explicit implementation policies; compare them to the
original runtime/peer. Verify color/temperature through an independently
known peer state and correlated authenticated feedback. Capture black/off,
endpoints, a mixed color, ignored MP, invalid context and unsupported-family
rejection. An accepted blob alone does not establish units or a commercial
product match. Only after these checks should a product-specific RF producer
and KNX presentation be enabled and recorded as hardware-qualified.

## Acceptance record

For each scenario retain commit IDs, peer identity evidence, original intent,
raw sanitized packets, relevant register snapshot, edge measurements,
expected/observed state, attempts/failures and reset-to-default result.
Mark unrun rows explicitly. Add captures to `test/corpus` with provenance and
separate source-model vectors from measured original-peer traffic.

## Optional sensor producer acceptance

The explicit default-subscription producer and bounded raw polling require their
own peer acceptance record before claiming supported sensor operation. Use
`check_release_evidence.py RECORD --product sensor`; the template gate
`sensor_status_default_subscription_polling` remains `not_run`. Record exact
sensor identity, established backbone, 8B body/8C response, authentication/error
behavior, observed event/listening effects, polling intervals and stop/expiry/
commissioning ownership. Decoder and queued-controller tests cannot fill this
physical gate. No physical sensor unit or generic auto-subscription is claimed.
