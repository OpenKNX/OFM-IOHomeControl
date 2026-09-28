# KLF Appendix-2 profile parameter registry

The single MP/FP registry is in `src/protocol/IoHomeProfileRegistry.cpp`. Its
source is VELUX, *Technical Specification for KLF 200 API*, version 3.18,
Appendix 2, Table 276 (pages 104-105). The registry keys the full packed
`Profile/SubProfile` pair, not the manufacturer ID or a product signature.

The 30 entries cover every row in Table 276, including the on/off variants
`0x017A`, `0x01FA`, and `0x057A`. FP4-FP16 are `Unsupported` for these documented
entries. An unlisted profile/subprofile resolves to `Unknown` for every
parameter; it never falls back to a different subprofile in the same profile.

The distinction between interior and exterior Venetian blinds matters:

| Packed type | FP1 | FP2 | FP3 |
| --- | --- | --- | --- |
| `0x0040` interior | slat orientation | slat orientation speed | blind speed |
| `0x0440` exterior | blind speed | slat orientation speed | slat orientation |

The existing captured two-way tilt Execute and tilt feedback paths address
FP3. They are enabled for a discovered profile only when this registry assigns
FP3 an orientation semantic. A known interior Venetian blind therefore does
not receive an FP3 tilt command or interpret FP3 feedback as slat position.
Legacy channels without discovery metadata retain their configured behavior.
The existing one-way combined position/slat frame has no confirmed parameter
index for a discovered profile, so it is not promoted to a profile-aware FP
command by this registry.

The registry also provides MP semantics to the status path and derives
capability flags from its MP/FP assignments. This step does not change MP
polarity or implement arbitrary FP activation masks; those are P1-MP.2 and
P1-MP.3. Profile-specific routing of all received FP values is P1-MP.4.
