# Preserve register and RS100 capture evidence

The public Calypso register sweep and Adelis Classic issue body were downloaded
on 2026-11-04. Reviewed derivatives retain byte-exact received 21 frames, their
four-byte echo, source/destination, source line, source/body hashes and published
time. Empty replies and message-only unconfirmed/no-reply outcomes are separate.
No TX request or authentication proof is reconstructed as a captured packet.

Sources: [Calypso sweep](https://github.com/laberning/home_io_control/issues/198#issuecomment-6617417409),
[Adelis Classic](https://github.com/laberning/home_io_control/issues/202).
The offline importer and reproducible header tests reject inconsistent lengths,
index/echo mismatches and unreviewed write shapes. Native tests parse and
reserialize every retained full response.

The new RS100 attachments are now available privately. Their exact hashes:

| Capture | Bytes | SHA256 |
|---|---:|---|
| somfy-io-hub-logs-4.txt | 45838 | 0a14ccde02c414c06949445271873a6c0fc3952f57fe4063848331d4d2a767b7 |
| somfy-io-hub-logs-5.txt | 26889 | 0ba4700608e4c2b1977f232d408d0068c2fc31e61cce1f310ad8f6f48a0182ab |

[Log 4 source](https://github.com/laberning/home_io_control/issues/156#issuecomment-6587085099),
[log 5 source](https://github.com/laberning/home_io_control/issues/156#issuecomment-6625445130).
Both include complete 67-byte 8103 and 16-byte 8005 dumps. The older 54-byte
8103 prefix differs at offsets 42,44,45,48; it is a different observation, not
missing bytes to concatenate. The empty list does not establish subscription
state. Both 8005 samples are 06 followed by fifteen zeros, including the sample
accompanied by the tester's south-facing 131 lx report. This establishes no
motor-local light scale or illuminance measurement.

The reviewed transfer-open table has 25 observations in log 4 and seven in
log 5; it preserves class, ID, handle, status and declared length. It does not
invent a complete 27-index sweep from the maintainer's aggregate 21/27 summary.
Long class-2 dumps remain metadata-only; A607 and other unreviewed values are
omitted. No class-2 object is exposed by this import. The parser now recognizes
explicit class syntax and never treats a class-2 record as class zero merely
because its ID matches a reviewed class-zero object.

Log 5 contains 342 of 366 bytes for A601, 90 of 431 for A613 and 341 of 341
for A615; individual partial/complete flags remain authoritative. Raw transfer
chunks, when not independently associated and reviewed, remain redacted.
No immediate subscription reply is an unconfirmed outcome, not a verified
refusal or proof of persistence. Original inputs and API bodies are local-only
under Velocet/Findings, with restrictive permissions.
