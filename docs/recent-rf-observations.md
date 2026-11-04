# Preserve recent RF observations without authentication bodies

The reviewed corpus contains the new Adelis high/standard-FP replies, foreign
transfer headers/closures and all five new garage attachments. Each retained
frame has the original bytes, source URL/hash, line, direction, clock time and
frequency where actually logged. Radio `probe_rx` observations and later
application reply summaries are separate records, not duplicate RF exchanges.
Masked/incomplete frames and unknown transfer value bodies remain omitted;
transport headers/counts can be retained without publishing their values.

[Adelis source](https://github.com/laberning/home_io_control/issues/202#issuecomment-6637212886),
[foreign transfer source](https://github.com/laberning/home_io_control/issues/201#issuecomment-6635803208),
[garage sources](https://github.com/laberning/home_io_control/issues/163#issuecomment-6635663656).
Original inputs are retained privately in Velocet/Findings; reviewed JSON and its
reproducible C++ header are public source fixtures. Full parser/re-encode tests
cover the retained packets. Authentication for controller replay is synthetic,
with no attempt to reconstruct masked challenges or proof bytes.

A46/47/48/4A/4B observation is not automatically part of a complete transfer.
In particular a captured48 does not authorize writes; unknown object payloads
and key-bearing data are not made available through the importer or controller.
