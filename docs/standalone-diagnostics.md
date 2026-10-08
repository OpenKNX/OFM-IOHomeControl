# io-homecontrol standalone diagnostics (without ETS)

This is a general io-homecontrol diagnostic mode for pairing, key import,
metadata inspection and raw radio captures. It is independent of a device model
and can be extended with future diagnostic commands. The current default is one
2W actuator channel; supported operations still follow the existing protocol and
device capability checks.

Enable it with `IOHC_STANDALONE_DIAG=1`. OAM enables the switch in:

- `standalone_develop_OpenKNX_ESP32_S3_DEV_SX1276_IP`
- `standalone_develop_OpenKNX_HELTEC_WIFI_LORA_32_V3_SX1262_IP`

Build with `pio run -e standalone_develop_OpenKNX_ESP32_S3_DEV_SX1276_IP` and flash the
resulting firmware as usual. Other build environments retain the ETS configuration
requirement. An undefined switch or `IOHC_STANDALONE_DIAG=0` disables standalone mode.

These development targets use `-Og` optimization with debug symbols and logging
to fit the existing application partition. Their existing 8 MB flash/storage layout is unchanged.

For the Heltec board, build with
`pio run -e standalone_develop_OpenKNX_HELTEC_WIFI_LORA_32_V3_SX1262_IP`.
Use the **863–928 MHz** WiFi LoRa 32 V3 variant for io-homecontrol, as listed in
[Heltec's hardware specifications](https://heltec.org/project/wifi-lora-32-v3/).
The onboard SX1262 uses SCK 9, MISO 11, MOSI 10, CS 8, reset 12,
DIO1 14 and BUSY 13. DIO3 supplies the 1.8 V TCXO with a 5 ms startup delay;
DIO2 controls the RF switch, following the manufacturer's
[pin configuration](https://github.com/HelTecAutomation/Heltec_ESP32/blob/master/src/driver/board-config.h)
and [radio initialization](https://github.com/HelTecAutomation/Heltec_ESP32/blob/master/src/driver/sx126x.c).
Serial output uses the onboard CP2102 USB-to-UART bridge (USB CDC disabled).
The onboard OLED is not used by this diagnostic firmware.

Use the board matching your radio wiring. Open the serial console at 115200 baud.
No ETS application, KNX group objects or KNX connection is needed for IOHC diagnostics.

The standalone framework wrappers initialize the module even when unconfigured.
A one-second module startup delay precedes the existing identity restore hook;
commands are blocked until it completes. The normal framework hook remains in
use for configured devices. There is no shared framework/core modification.

Without an ETS application there is one diagnostic channel, channel 1: 2W,
manual override 0, automatic RF power, default discovery/confirm/ACEI and key-init
delay. Other ETS channels are not activated or made assignment targets. The
existing network journal retains bindings outside the diagnostic channel count.
No ETS defaults or generated parameter memory are edited.

## Direct pairing (no existing gateway)

1. Wait for the standalone startup message, then `iohc pairdiag on`.
2. Put the target device into its normal programming/pairing mode.
3. Run `iohc01 pair` and wait for pairing and metadata enrichment to complete.
4. Check `iohc01 status`, then `iohc01 probe info1` and `iohc01 probe info2`.
5. If metadata is incomplete, run `iohc metadata refresh NODE` (hex node address).
6. Use the existing console diagnostics appropriate to the device. For a
   protocol-v4 actuator, optionally read the dynamic objects as described below.
7. Run `iohc event trace on` and exercise the device or its inputs. For a cover,
   use `iohc01 send 0`, `iohc01 stop`, `iohc01 send 100` to capture movement.
8. End with `iohc event trace off`. Reboot, check `iohc01 status`, then repeat
   the applicable diagnostics to verify the identity and channel binding survived.

Existing runtime RF overrides remain available: `iohc 2wdiag power auto|always|low`,
`iohc 2wdiag preamble auto|N`, `iohc 2wdiag wake on|off`, `iohc 2wdiag status`.
Pairing timing, keys and authentication use the existing device-independent paths.

## Existing gateway (no actuator reset)

1. Run `iohc extract start 120`.
2. Start Add Device on your existing io-homecontrol gateway.
3. Observe `iohc extract status` and `iohc keyimport status`. Wait for recovery,
   verification and authenticated inventory discovery to complete.
4. Run `iohc keyimport candidates`. In a standalone build the final result lines
   show a zero-based index, node, profile/subtype/manufacturer, power class,
   metadata and authentication/verification flags and the assigned channel.
5. Select the target device, for example `iohc keyimport assign 0 1`.
6. Continue with the status, device diagnostics and reboot checks above.

`iohc01 stop` is a standalone-only wrapper around the existing 2W Execute STOP
command, gated on a paired actuator and idle commissioning/RF ownership.

`keyimport assign` is compiled, parsed and advertised only in standalone builds.
It wraps the existing `assignKeyImportDevice` path and only accepts channel 1.
Malformed/overflowing indices, non-final results, occupied channels, mismatched
keys, busy commissioning/RF ownership and storage failures are rejected. Existing
assignments are not silently overwritten. Status 0 means assigned; 1 means already
assigned; 2 means channel/persistence rejection; 3 means unavailable result; 4
means identity conflict. Inspect the console and `iohc 2wrecovery status` on failure.

The optional assign-all command is deliberately omitted: there is one diagnostic
channel and explicit selection avoids importing the wrong device. The pre-existing
normal `keyimport candidates`/status/trace diagnostics remain available unchanged.

## Persistence and KNX safety

Controller identity, network key, bindings and metadata use the same checked
network journal, assignment receipts and metadata stores as normal firmware.
There is no hard-coded key and no alternative unverified storage path.
Framework `openknx.flash.save()` declines writes while unconfigured; diagnostic
persistence therefore relies on the existing checked stores, not legacy ETS flash.
Only standalone receipt recovery fills missing metadata when the network journal
has already restored a node. Metadata snapshots subsequently restore the fuller
identity and GeneralInfo evidence. Keep storage failures fail-closed.

Module startup/discovery/scan and RF callback KO publication are guarded by
`knx.configured()`. An unconfigured channel loop does not perform ETS-driven
polling, power-on actions, estimates or KO access. RF/controller callbacks still
retain received position, movement, RSSI and error state for serial diagnostics.

## Optional protocol-v4 dynamic actuator diagnostics

Run `iohc dynamic 1 read`, wait for completion, then `iohc dynamic 1 status`
to print the exact raw responses. These reads are optional, not a prerequisite
for generic standalone diagnostics. The authenticated path uses provider 00,
objects 8100 then 8103,
offset 0 and bounded at 1024 bytes. It requires a paired 2W **actuator** with
GeneralInfo1 length >10 and byte 10 >=4. Do not weaken this check to make a read run.
Unknown schemas and event/subtype/scalar remain explicitly UNKNOWN. Key transfers
and unrelated object chunks (including A607) remain redacted in raw tracing.

There are no new dynamic event subscriptions, no invented 0FB0 RF bridge and no
battery percentage, SunEnergy or lux publication. See
[dynamic sensor evidence limits](rs100-solar-dynamic-sensors.md).

## What to send back

Send the complete serial log with node address, pairing or extraction transcript,
metadata replies and traces relevant to the operation under investigation.
Include the device catalogue/model, radio chip, distance, stimulus/action and
corresponding timestamps. For dynamic actuators, also include exact raw 8100/8103
bytes when supported. Pairing, extraction and reboot qualification still require
the tester's hardware; software regression results are not a physical claim.

Software regressions cover normal and standalone configuration gates, startup,
import assignment, KO safety and existing RF/storage behavior. The selected ESP32-S3
development build provides the ready-to-flash standalone firmware.
