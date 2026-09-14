# ETS configuration layout

The IO-HomeControl section separates everyday configuration from commissioning
and protocol diagnostics. Other application modules are unaffected.

- Overview shows the last values explicitly read from the device, not live data.
- Channel selection retains activation and suspension semantics.
- Each enabled channel exposes name, device type, Suspend, communication mode,
  profile selection, power class and startup behavior on its main page.
- Functions and feedback groups travel-time estimation, direction, supported
  feedback settings, silent operation and optional diagnostic objects.
- Scenes remain opt-in with their existing default and object assignments.
- Expert view exposes the existing protocol overrides without resetting them
  when the view is closed. Hidden overrides remain effective.
- Commissioning groups the existing online actions and last-read results.
- Global diagnostics distinguishes active device discovery from passive radio
  monitoring.

## Configuration ownership

ETS supplies explicit behavior and protocol overrides. Existing device flash
stores commissioned associations and learned metadata. This layout change does
not alter flash restoration or configuration precedence. Online status fields
are snapshots; refresh them after a download or commissioning action.

For 2W power class, explicit configuration overrides learned metadata. Automatic
uses the learned class when available and otherwise defaults to always alive.
Solar/battery devices with unknown metadata need an explicit Low Power setting.
Shared 1W profiles use their owner's manufacturer and power-class settings.

## Compatibility and verification

Existing memory offsets, union size, parameter defaults and communication-object
numbers are preserved. The new display selectors are ETS-only parameters outside
device memory. Battery and RSSI objects remain enabled by default for existing
installations; disabling them is an explicit project configuration change.

Run the ETS/UI regression checks and the full OAM producer before release.
ETS import, navigation and project-upgrade behavior still require verification
with an installed ETS environment. A producer run that skips XSD validation or
package creation does not establish that the final application package is valid.
