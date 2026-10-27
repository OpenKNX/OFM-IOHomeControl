# Original capture regressions

The pinned source and per-scenario coverage are documented in
[test/corpus/README.md](../test/corpus/README.md) and
[laberning-manifest.json](../test/corpus/laberning-manifest.json).

The original VELUX SSL observations include stopped replies 119 and 171 raw
units off target and a moving reply only 92 raw units from target. Two production
changes follow from these tests:

- A valid device motion flag is authoritative even near the target. Distance
  within 100 raw units no longer silently changes moving to stopped.
- A stopped status ends motion estimation and tracked polling without snapping
  the measured current to an endpoint or replacing a distinct reported target.
  Estimated/local STOP handling keeps its existing separate path.

Original fixtures are unchanged. Unknown keys/absent CRC, reconstructed headers,
synthetic time/context, observer provenance and LR1121 protocol-only attribution
remain explicit. MOTO1000 refusal, stale ACK and optional pairing configuration
regressions protect existing restrictions; they do not create generic writers,
pretend an ACK proves movement, or weaken authentication/admission.

The host-only fixture data does not enter firmware. ETS application 4.7 and
parameter/KO offsets remain unchanged. Software and build outcomes are recorded
in OAM; physical timing, actual peer reliability and release qualification are
still unrun.
