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

## 2W key extraction and automatic import

The **2W-Schlüssel extrahieren** ETS action keeps its online dialog open for the
complete workflow:

1. The firmware enrolls as a temporary device at the existing, owned gateway
   and extracts the network key.
2. After the gateway address-verification window closes, it adopts the newly
   enrolled controller ID and automatically starts authenticated SPE discovery
   with the long wake-up preamble.
3. ETS displays all discovered Node IDs. Devices that are not already assigned
   are added only to unused channels without a stored device identity. Occupied
   channels are never overwritten.
4. If channel parameters changed, ETS explicitly requests a new application
   download. The imported communication objects and channel configuration only
   become active after that programming step.

The last result and up to 16 discovered Node IDs remain visible on the
commissioning page. If too few unused channels are available, the remaining
devices stay unassigned and the final message reports that condition.

## Configuration ownership

ETS supplies explicit behavior and protocol overrides. Existing device flash
stores commissioned associations and learned metadata. This layout change does
not alter flash restoration or configuration precedence. Online status fields
are snapshots; refresh them after a download or commissioning action.

For 2W power class, explicit configuration overrides learned metadata. Automatic
uses the learned class when available and otherwise defaults to always alive.
Solar/battery devices with unknown metadata need an explicit Low Power setting.
Shared 1W profiles use their owner's manufacturer and power-class settings.

The per-channel 2W expert settings also expose the post-discovery handshake:

- **2W Discovery-Bestätigung** defaults to **Senden**. `0x2C` is retried three
  times with 1500 ms reply windows; ACK, refusal and silence all continue to
  key initialization. **Senden + ACK** sets the ACK bit only for always-alive
  targets, while **Überspringen** restores the direct `0x29 -> 0x31` path.
- **2W Pause vor Schlüsselaustausch** defaults to 300 ms and accepts 0–10000 ms.
  It is a nonblocking wait after the confirmation step and is omitted in Skip mode.

For the hardware-confirmed VELUX SSL solar profile, use discovery command `0x28`,
destination `0x00003F`, ACK on, discovery LOW_POWER off, preamble 32, and
**2W Discovery-Bestätigung = Senden**. Once the correlated `0x29` is accepted,
the exact successful discovery preamble caps the directed `0x2C`, `0x31`, and
optional `0x6F` START frames for that pairing transaction. The target's learned
LOW_POWER flag is retained; `0x32` and `0x3D` remain short continuation frames.

`0x33` completes and persists the pairing. The later `0x6F SetConfig1` request is
optional and only enables automatic status feedback when supported. Its rejection,
timeout, or TX failure is shown as an optional configuration warning, not as a
failed pairing. Trigger only one PROG/registration gesture per pairing attempt and
do not press PROG again while the current registration window is active.

For field diagnosis, `iohcNN status` separates full response timeouts from
authenticated-but-unconfirmed exchanges. `iohc radio` also prints the last such
exchange's node, command, attempt, frequency, RSSI, RX/CRC/IRQ, preamble, and sync
snapshot.

## Compatibility and verification

Existing parameter offsets and communication-object numbers remain stable. The
per-channel memory union grows by two bytes for the key-init delay; the confirmation
mode uses previously free bits. The display selectors remain ETS-only parameters
outside device memory. Battery and RSSI objects remain enabled by default for
existing installations; disabling them is an explicit project configuration change.

Run the ETS/UI regression checks and the full OAM producer before release.
ETS import, navigation and project-upgrade behavior still require verification
with an installed ETS environment. A producer run that skips XSD validation or
package creation does not establish that the final application package is valid.
