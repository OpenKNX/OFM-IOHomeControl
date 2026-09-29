# KLF Appendix-2 profile parameter registry

The single MP/FP registry is in `src/protocol/IoHomeProfileRegistry.cpp`. Its
source is VELUX, *Technical Specification for KLF 200 API*, version 3.18,
Appendix 2, Table 276 (pages 104-105). The registry keys the full packed
`Profile/SubProfile` pair, not the manufacturer ID or a product signature.

The 30 entries cover every row in Table 276, including the on/off variants
`0x017A`, `0x01BA`, `0x01FA`, and `0x057A`. The internal `Unsupported` state means only
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
Normal parameter encoding (`Relative`, `Discrete`, or `Unknown`) is separate
from classification of a raw 16-bit value. Only `0000..C800` may use the
percentage converter; `C900..D0D0` are percent deltas. `D100` target, `D200`
current, `D300` default, `D400` ignore, profile-scoped `D8xx` aliases, and
`F7FF` no-feedback must not be converted to percentages. Unknown `D8xx` and
other reserved values classify as `Unknown`.

Overkiz aliases are keyed by exact packed profile/subprofile and parameter
index. `D800` on exterior Venetian FP3 means memorized tilt; roller-shutter
FP3 is not assigned that alias. Gate `D807`, garage `D809`, and exterior
Venetian/swinging-shutter `D80A` are likewise scoped. C0.22 HVAC aliases are
manufacturer-restricted, so they remain research-only rather than generic
production entries. Alias metadata does not authorize native RF transmission.
See [`parameter-evidence.md`](parameter-evidence.md) for the class matrix.

The four `.58` On/Off subprofiles carry a binary-only capability. Runtime
accepts only 0/100 On/Off endpoints, publishes boolean feedback, and rejects
continuous-position/movement KOs. ETS import exposes On/Off command/status
instead of percentage KOs for these profiles.

The diagnostic console can represent FP1-FP16 masks, but only a single
captured FP1-FP3 native Private/Execute shape is transmitted. Multi-FP and
FPI2 native RF layouts remain unverified and are displayed without TX. The
source for mask ordering is OVPd's `Manager.MpFpRefreshManager.lua`; its
Overkiz command payload is not assumed to be identical to a native RF frame.

The protocol's FPI1/FPI2 selection addresses FP1-FP16. The shared Overkiz
OVPd parameter model publishes MP, FP1-FP3, and FP9-FP16 definitions; it has
no equivalent shared public FP4-FP8 files in that tree. This is product
coverage of that implementation, not evidence that FP4-FP8 are reserved or
unsupported. Higher-FP meanings remain unknown in the KLF Appendix-2
registry until a profile-specific source or capture supplies them.

Source: <https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter/IoHomecontrolOVPd/Parameter>.

### Higher-FP research evidence (not generic KLF assignments)

These assignments come from Overkiz product/class models, not the KLF
Appendix-2 generic actuator table. The key is at least
`(profile, subProfile, FP index)`; manufacturer, product signature, and
software version may narrow it further. No global `FP16 = operating mode`
rule is valid.

| Node/profile | Higher FP | Evidence/meaning |
| --- | --- | --- |
| Roller shutter subtype 2 | FP9 | projection angle |
| Sliding window subtype 1 | FP9 | lock state |
| Indoor siren type 30 | FP9-FP14 | three sound-pattern/sonorous-sequence pairs |
| Light subtype 1 | FP10, FP11 | colour handling with MP in RGB converters |
| Light subtype 2 | FP14 | colour temperature |
| On/off switch subtype 2 | FP13 | MicroModuleOnOff use; exact semantic unconfirmed |
| Heat pump type 22 | FP15, FP16 | capabilities and active modes |
| Domestic hot water type 51 | FP10, FP15, FP16 | profile-specific modes/capabilities |
| Electrical heater type 52 | FP10, FP11 | maximum heat level and transitory timer |
| Electrical heater subtype 1 | FP12, FP13 | observed in model; generic meaning unconfirmed |
| Heat recovery ventilation type 53 | FP16 | ventilation configuration mode |

Source: <https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter/IoHomecontrold/Node/Class>.
An explicit packed-profile override can change behavior without modifying
the hardware-discovered identity.

Future vendor/product behavior belongs in a *higher* lookup layer:
generic `(profile, subProfile)` descriptor, then a manufacturer-specific
override, then a product/signature/software-version override using GI1/GI2
evidence. The override result may choose parameter encoding or quirks but
must never replace the raw discovery record. No unverified product override
is installed by this registry. Existing manual ETS profile overrides similarly
change the effective descriptor only; a regression test checks that discovery
identity remains intact.

ETS uses `ProfileOverride = 0` for automatic discovery-based behavior. An
expert may enter a documented packed value as a decimal number (for example,
`64` for interior Venetian or `1088` for exterior Venetian). A conflicting
override is logged, while the original Profile/SubProfile remains in
diagnostics and persistence. The automatic ETS channel exposes the common
control objects; firmware accepts only operations supported by the discovered
descriptor. ETS cannot add or remove group objects after a radio discovery,
so imported channels may still choose a static display role without forcing
the protocol profile.

KLF confirms `D803` as secured ventilation for Window Opener, but not the
exact native 2W Execute payload. The 2W ventilation command stays blocked
until a native RF capture confirms its frame; the existing 1W path remains.
