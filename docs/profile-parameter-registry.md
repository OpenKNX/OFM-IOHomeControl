# KLF Appendix-2 profile parameter registry

The single MP/FP registry is in `src/protocol/IoHomeProfileRegistry.cpp`. Its
source is VELUX, *Technical Specification for KLF 200 API*, version 3.18,
Appendix 2, Table 276 (pages 104-105). The registry keys the full packed
`Profile/SubProfile` pair, not the manufacturer ID or a product signature.

The 30 entries cover every row in Table 276, including the on/off variants
`0x017A`, `0x01FA`, and `0x057A`. The internal `Unsupported` state means only
**not assigned by KLF Appendix 2 for this particular profile**. It does not
mean FP4-FP16 are unsupported by io-homecontrol: the protocol addresses all
16 FPs, and newer Overkiz product classes use higher indices. An unlisted
profile/subprofile resolves to `Unknown` for every
parameter; it never falls back to a different subprofile in the same profile.

The distinction between interior and exterior Venetian blinds matters:

| Packed type | FP1 | FP2 | FP3 |
| --- | --- | --- | --- |
| `0x0040` interior | slat orientation | slat orientation speed | blind speed |
| `0x0440` exterior | blind speed | slat orientation speed | slat orientation |

KLF Table 276 and `GW_COMMAND_SEND_REQ` example 2 both put the *interior*
Venetian slat angle at FP1; the example selects it with `FPI1=0x80`. A
separate KLF paragraph groups interior Venetian, exterior Venetian, and louvre
blinds as though all use FP3 for the angle. That sentence contradicts both
the table and its explicit command example. Keep `0x0040` FP1 as orientation
unless a device-specific capture proves otherwise; do not transpose it to FP3.

The two-way Execute and indexed Private status paths select FP1, FP2, or FP3
from the registry and build the corresponding activation bit. A returned FP3
field is interpreted as tilt only for profiles that assign FP3 an orientation
semantic; for an interior Venetian blind, that field reports speed and the
orientation poll instead targets FP1. Legacy channels
without discovery metadata retain their captured FP3 tilt behavior. The
one-way combined position/slat frame has no confirmed parameter index for a
discovered profile and remains disabled in that case.

The registry provides MP semantics, polarity, functional-parameter indices,
and descriptor-derived capabilities to both command and feedback paths.
An explicit packed-profile override can change behavior without modifying
the hardware-discovered identity.

ETS uses `ProfileOverride = 0` for automatic discovery-based behavior. An
expert may enter a documented packed value as a decimal number (for example,
`64` for interior Venetian or `1088` for exterior Venetian). A conflicting
override is logged, while the original Profile/SubProfile remains in
diagnostics and persistence. The automatic ETS channel exposes the common
control objects; firmware accepts only operations supported by the discovered
descriptor. ETS cannot add or remove group objects after a radio discovery,
so imported channels may still choose a static display role without forcing
the protocol profile.
