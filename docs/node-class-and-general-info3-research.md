# NodeClass and GeneralInfo3 research

This note records the evidence boundary used by the implementation. It is not
a protocol specification. Unknown values remain unknown until captures from
multiple device classes and manufacturers establish stable semantics.

## GeneralInfo3 (`0x58` / `0x59`)

Protocol identity and product identity are distinct records. The protocol
record retains ioAddress, packed profile/subProfile, backbone, manufacturer,
the raw MIB, its decoded power mode, and the discovery timestamp/raw bytes.
GI1/GI2, software/product signatures, and a commercial model are separate
product evidence. OVPd uses GI1/GI2 for product-specific class decisions; that
must not rewrite the observed discovery bytes. An unknown product can still
have a valid protocol identity, and an unlisted profile must remain unlisted.

The pairing enrichment sequence sends `GetGeneralInfo3` after name, GI1, and
GI2. A `GetGeneralInfo3Response` is retained byte-for-byte, up to the normal
23-byte frame-data limit. It does not update position, movement, profile,
manufacturer, NodeClass, or any other production field.

The query outcome is retained separately as one of:

- not queried;
- requested;
- response;
- error response, including the raw error payload;
- timeout;
- transport failure.

This distinction deliberately does not label every protocol error as
"unsupported". That conclusion requires a confirmed error-code definition or
repeated device-specific captures.

### Capture matrix

No hardware capture corpus for GI3 is checked into this repository yet.

| Manufacturer/family | `0x59` response | Error/unsupported | Stable byte pattern |
| --- | --- | --- | --- |
| VELUX | needed | needed | not assigned |
| Somfy | needed | needed | not assigned |
| Hörmann | needed | needed | not assigned |
| Atlantic | needed | needed | not assigned |
| Other | needed | needed | not assigned |

For each capture, record the commercial model and label, RF source/destination,
full `0x58` request, full `0x59` or error response, profile/subProfile,
manufacturer ID, known gateway class, firmware version if available, and
whether repeated replies are byte-stable. Do not assign byte semantics from a
single product.

## NodeClass

`IoHomeNodeClass` is stored independently from profile/subProfile:

```text
Unknown, Actuator, Sensor, Controller, Stack, Beacon
```

The default is `Unknown`. Unknown nodes retain the existing actuator-compatible
behavior so an upgraded installation is not disabled. When an independent,
authoritative source supplies a non-actuator class, actuator commands and
status polling are suppressed. A later discovery refresh cannot erase a known
class for the same ioAddress merely because the discovery packet has no class
field.

NodeClass is versioned in flash layout 17. Older layouts restore it as
`Unknown`; keys and pairings remain readable.

## Native-source investigation

### Discovery identity provenance

The native 9-byte `0x29`/`0x2B` identity layout is **OVPd-confirmed and
capture-confirmed**. `Node/Class/Abstract.lua` serializes `NodeType` (UInt16),
`BackboneAddress` (UInt24), `ManufacturerId` (UInt8), `MultiInfoByte` (UInt8),
and `TimeStamp` (UInt16), corresponding to data offsets 0-1, 2-4, 5, 6,
and 7-8. KLF v3.18 confirms the profile/subprofile, manufacturer, and backbone
meanings. The raw MIB and timestamp remain retained; decoding every MIB bit is
not implied by confirmation of the byte layout.

| Claim | KLF | OVPd | Velocet docs | Capture | Current status |
| --- | --- | --- | --- | --- | --- |
| Native 9-byte field order | corresponding system data | explicit `Abstract.lua` serialization | compatible | prior 0x29/0x2B observations | confirmed layout |
| Profile/subProfile, manufacturer, backbone | defined | stored | described | observed | protocol identity |
| MIB bits 2/3 polarity | `1` means member/RF support | raw byte only | opposite Yes/No text | needed | KLF interpretation retained |
| MIB bit 5 | undefined | raw byte only | `SyncCtrlGrp` | needed | provisional; no behavior |
| MIB bits 7:6 unit | 5/10/20/40 ms | raw byte only | 5/10/20/40 s | timing measurements needed | raw class authoritative |
| Discovery timestamp | field present | UInt16 | described | raw byte observations | preserve raw; no clock meaning inferred |
| Native NodeClass RF source | not established here | separate class model | separate classes | needed | unknown |

The raw `multiInfoByte` remains authoritative. `responseTimeClass` is merely
bits 7:6; the KLF millisecond and Velocet second interpretations are logged
as source hints, never used to change production timeouts. Bit 5 is only a
SyncCtrlGrp *candidate* pending differing-device captures. Product evidence
from GI1/GI2 and software signatures stays in a separate layer from the
protocol discovery record.

Source: <https://github.com/Velocet/iown-homecontrol/blob/main/docs/parameter/IoHomecontrolOVPd/Node/Class/Abstract.lua>.

### Research-only extended node types

The KLF v3.18 Appendix-2 list and the later Overkiz/Velocet node model are
separate evidence sets. The following **OVPd-only** types are not inserted
into the KLF profile registry and do not infer actuator semantics:

| Type | Overkiz/Velocet label | KLF Appendix 2 | Capture |
| ---: | --- | --- | --- |
| 27 | Sliding Window | absent | needed |
| 28 | Zone Control Generator | absent | needed |
| 29 | Bioclimatic Pergola | absent | needed |
| 30 | Indoor Siren | absent | needed |
| 51 | Domestic Hot Water | absent | needed |
| 52 | Electrical Heater | absent | needed |
| 53 | Heat Recovery Ventilation | absent | needed |
| 255 | Central House Control | absent | needed |
| 1008 | Test and Evaluation | absent | needed |
| 1023 | Remote Controller | absent | needed |

Source: <https://github.com/Velocet/iown-homecontrol/blob/main/docs/parameter/IoHomecontrolOVPd/Node/nodeModel.lua>.

The current reverse-engineered Overkiz implementation keeps class and profile
as separate dimensions:

- `nodeModel.lua` defines distinct actuator, sensor, stack, beacon, and
  controller class IDs.
- `Node/Manager.lua` keeps separate per-class stores.
- transport objects construct sensor and controller nodes with explicit class
  values rather than deriving class from profile.
- discovery UI actions only issue actuator-specific wink behavior for nodes
  already classified as actuators.

Sources:

- <https://github.com/Velocet/iown-homecontrol/blob/main/docs/parameter/IoHomecontrolOVPd/Node/nodeModel.lua>
- <https://github.com/Velocet/iown-homecontrol/blob/main/docs/parameter/IoHomecontrolOVPd/Node/Manager.lua>
- <https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter/IoHomecontrolOVPd/Transport/Objects>

The inspected public pairing, discovery, and system-copy paths do not establish
which RF field or command is the authoritative native NodeClass source. In
particular, `0x29`/`0x2B`, GI1, GI2, and GI3 are not treated as class sources.
Profile-only inference is intentionally absent because public class/profile
tables contain overlapping numeric profile meanings across classes.

### Remaining evidence work

Capture pairing and system-copy traffic for known actuator, sensor, controller,
stack, and beacon nodes. Compare explicit class values from a known-good
gateway with every RF response in the same session. Only promote a source into
production after it is stable across more than one device family or is backed
by an authoritative protocol definition.
