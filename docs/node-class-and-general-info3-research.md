# NodeClass and GeneralInfo3 research

This note records the evidence boundary used by the implementation. It is not
a protocol specification. Unknown values remain unknown until captures from
multiple device classes and manufacturers establish stable semantics.

## GeneralInfo3 (`0x58` / `0x59`)

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
