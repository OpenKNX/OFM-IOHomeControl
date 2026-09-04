# OFM-IO-Homecontrol

OpenKNX firmware module for the direct integration of **Velux, Somfy and other io-homecontrol devices** into KNX. The module uses an ESP32 together with an SX1276 or SX1262 868 MHz radio and does not require an external manufacturer gateway.

This implementation combines protocol research and practical implementation work from several community projects. The main protocol documentation and 2W reference implementation is [Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol). The 1W remote-profile behavior is cross-checked against [rspaargaren/iohomecontrol](https://github.com/rspaargaren/iohomecontrol) and RF captures. Additional implementation references are listed in [Related protocol sources](#related-protocol-sources).

## Supported Devices

The io-homecontrol protocol should cover a wide range of motorized building products (most of them are still untested!):

| Category | Examples |
|----------|----------|
| Roller shutters | Velux SSL, Somfy RS100 io |
| Venetian blinds | Somfy Venetian io, external venetian blinds |
| Horizontal blinds | Somfy horizontal blind io |
| Awnings | Somfy Sunea io, horizontal awnings |
| Roof windows | Velux Integra (KMX/KSX), window openers |
| Garage doors | Somfy Dexxo io, gate openers |
| Gates | Somfy Slidymoove io |
| Curtain tracks | Somfy Glydea io |
| Thermostats | Atlantic Cozy io |
| Lights | io-homecontrol dimmers and switches |
| Locks | io-homecontrol door locks |
| Ventilation | io-homecontrol ventilation units |

The protocol implementation covers manufacturers such as Velux, Somfy, Atlantic, Honeywell, Niko, WindowMaster, Renson and ASSA ABLOY. The practical validation status may differ between individual device families.

## Features

- **Up to 16 independent channels** — each paired with one io-homecontrol device
- **Bidirectional (2-way) communication** with AES-128 encrypted command authentication
- **Position control** (0–100%) with position feedback from the device
- **Up/Down/Stop commands** via standard KNX datapoints
- **Slat/tilt control** for venetian blinds
- **Thermostat control** for Atlantic Cozy io — temperature setpoint, operating mode, presence, window contact
- **Direction inversion** per channel for different mounting orientations
- **Protocol mode** selectable per channel (2-way bidirectional or 1-way unidirectional)
- **Independent persistent 1W controller profiles** per channel, with explicit profile sharing for 1W groups
- **Strictly separated 2W and 1W identities** — 1W remotes use their own node address/key and never silently reuse the 2W gateway identity
- **Configurable typed 1W broadcast destinations and controller manufacturer**, plus Situo QR identity import
- **Device pairing/unpairing** via ETS workflows, function properties, or service console
- **Automatic status polling** with configurable intervals (30s – 30min); after pairing, the module also tries to enable device-driven status updates when the device supports them
- **Position estimation** during travel using configurable opening/closing times and linear interpolation
- **Battery level detection** for solar-powered devices (e.g., Velux solar)
- **Signal strength (RSSI)** per channel for radio coverage verification
- **Favorite & ventilation positions** — dedicated KOs for device-stored presets
- **Channel lock/unlock** for child safety or maintenance
- **Error status reporting** per channel (communication error, duty cycle, pairing lost)
- **KNX scene support** (DPT 17.001 / 18.001) — up to 10 scenes per channel with device-type-specific scene data
- **Wind/rain alarm** safety input — auto-retract awnings, close windows on alarm
- **Step-stop (Langzeitbetrieb)** — standard KNX blind behavior for venetian blinds
- **Power-on behavior** configurable per channel (nothing, request status, restore last feedback position)
- **Optional brightness control for dimmable lights**, plus dedicated binary command/status handling for lights, switches and locks
- **3-channel frequency hopping** across the 868 MHz ISM band
- **EU duty cycle compliance** with per-sub-band tracking (1-hour window)
- **Command queue** with automatic retries (up to 3 attempts, cycling frequencies)
- **Remote observation** — track io-homecontrol remotes on the bus, link devices to remotes
- **Explicit passive key sniff workflow** for diagnostics (listen-only session with separate start/stop/status/clear controls)
- **Active 2W key extraction workflow** for owned hubs (temporary device-role responder via `iohc extract ...`)
- **Network scan** with per-node packet statistics and RSSI tracking; observation-only
- **Encrypted discovery (SPE)** for scanning already-paired devices
- **Flash persistence** of 2W identity/pairing data and complete per-channel 1W controller profiles
- **Byte-exact protocol self-tests** for serializer boundaries, 1W/2W crypto vectors and key-transfer transcripts

For 2W, the module uses one global controller node ID and system key. For 1W, each channel uses a Cyril-style virtual remote profile containing its own controller address, key, sequence counter, reserved sequence watermark, type and manufacturer. Channels that should control the same 1W group can explicitly share one profile. Normal 1W commands reserve sequence numbers ahead in flash instead of saving after every frame; pairing/add/remove still force an immediate persistence update. New/imported profiles use sequence `1` for their first frame.

### 1W protocol notes

The 1W path follows the reference remote model more closely than older gateway-derived implementations:

- Default 1W pairing uses the observed remove/add flow (`0x39 RemoveController` followed by unauthenticated `0x30 SendKey1W`).
- VELUX/KLI-compatible profiles use the KLI enrollment variant: one logical `0x30` is broadcast to `0x00003F`, `0x0000BF`, `0x0000FF`, and `0x00037F` with a shared sequence number, then authenticated STOP (`0xD200`) and DOWN/CLOSE (`0xC800`) commands are sent to ALL (`0x00003F`) with fresh sequences. DOWN starts well within the required three-second window after STOP.
- The ETS parameter **1W enrollment finalizer** is conservative by default: **Automatic** selects STOP + DOWN only for a VELUX controller manufacturer. **None** disables it, while **STOP + DOWN** explicitly enables it for another profile. It is not assumed that Somfy or unknown 1W devices need this finalizer.
- `0x30 SendKey1W` has 29 declared bytes: 9-byte header plus `encryptedKey[16] + manufacturer + 0x01 + sequence[2]`. The default profile sends no trailer; an ETS profile option can append the observed six-byte MAC outside CTRL0's declared length.
- A channel can clone an existing original remote instead of enrolling a new identity: `iohcNN pair1w receive` arms a listener that captures the remote's `0x30 SendKey1W` "copy remote" frame, decrypts the contained key with the public transfer key, and stores the remote's address, key, manufacturer, and a future reserved sequence window into the channel's own unshared 1W profile. This is required for actuators that only obey remotes added through the manufacturer's copy procedure. Use `pair1w stop`/`pair1w status` to cancel or inspect the capture.
- Normal 1W commands use typed broadcast destinations by default, computed as `dst=((type << 6) | 0x3F)`. Automatic mode maps the ETS device role to its protocol class; type `0` remains the explicit all-device target.
- Normal 1W control uses the raw io-homecontrol closedness convention internally (`0=open`, `100=closed`). UI/KNX open percentages are converted explicitly at the channel boundary.
- 1W radio transmission uses five total sends by default: one long-preamble first TX followed by four short-preamble repeats with 40 ms spacing. Enrollment is an asynchronous serialized operation, so normal queued traffic cannot appear between REMOVE, ADD, STOP, and DOWN.

### VELUX KUX/KLI commissioning and troubleshooting

1. Put the owned KUX, actuator, or window product into its physical PROG/association window before starting module enrollment. The exact gesture and confirmation movement are device-specific; follow the relevant VELUX manual.
2. Configure the effective 1W controller manufacturer as **VELUX** (`0x01`). Automatic finalization depends on this profile value.
3. Leave the 1W command ACEI at the VELUX remote value `0x61` unless a capture of the original remote proves another value.
4. Use the automatic broadcast class first. The KLI-compatible ADD flow covers ALL plus the public window/shutter/other type destinations; the finalizer deliberately uses ALL.
5. Leave the `0x30` MAC trailer disabled unless the original remote or actuator is known to require the 35-byte variant. Both MAC and no-MAC forms remain supported.
6. If enrollment fails, run `iohcNN 1wctrl status` and `iohc pairdiag status`. Check profile ownership, controller source/key, finalizer resolution, per-phase destination/sequence/TX result, and STOP-to-DOWN timing. Diagnostics redact wrapped keys and key material.
7. After the target confirms pairing, test OPEN, STOP, and CLOSE, then reboot the module and repeat a command to verify that the persistent controller key and reserved sequence remain accepted.

Because 1W devices do not acknowledge these frames, a successful local TX trace alone is not proof of physical enrollment. A second receiver capture and the target's confirmation movement are the definitive checks.

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| MCU | ESP32 or ESP32-S3 |
| Radio | **SX1276** or **SX1262** 868 MHz module (see below) |
| KNX interface | NCN5120 or NCN5130 via OpenKNX hardware |

### Supported Radio Modules

The firmware supports two Semtech radio chips, selected at compile time via a build flag:

| Chip | Build Flag | Example Modules | Status |
|------|-----------|-----------------|--------|
| **SX1276** | `-DRADIO_SX1276` | RFM95W, HopeRF RFM96W, Heltec LoRa32 v2, TTGO LoRa32 v2 | default, implemented |
| **SX1262** | `-DRADIO_SX1262` | Heltec LoRa32 v3, Waveshare SX1262, E22-868T | Implemented |

If no build flag is set, the firmware defaults to **SX1276** and emits a compiler warning.

Both chips use the same io-homecontrol FSK protocol parameters (38.4 kbps, 19.2 kHz deviation, 250 kHz BW, 3-channel hopping). The abstraction layer (`Radio.h`) provides a compile-time `typedef` that maps `Radio` to the correct driver class — only one chip is compiled into the binary at a time.

### GPIO Pinout

Default GPIO assignments (configurable in `IoHomecontrolHardware.h`):

**Common pins (both chips):**

| Signal | GPIO |
|--------|------|
| SPI_SCK | 18 |
| SPI_MISO | 19 |
| SPI_MOSI | 23 |
| SPI_CS | 5 |
| RADIO_RST | 14 |

**SX1276-specific pins:**

| Signal | GPIO | Description |
|--------|------|-------------|
| RADIO_DIO0 | 26 | TX done / RX payload ready interrupt |
| RADIO_DIO4 | 27 | Preamble detection (optional) |

**SX1262-specific pins:**

| Signal | GPIO | Description |
|--------|------|-------------|
| RADIO_DIO1 | 26 | Combined IRQ (TX/RX/preamble/sync/CRC/timeout) |
| RADIO_BUSY | 27 | SPI busy indicator (required, active high) |

Optional SX1262 board-control overrides:

- `IOHC_RADIO_RF_SW`: shared external RF switch GPIO, driven HIGH for RX and LOW for TX
- `IOHC_RADIO_DIO2_RF_SW=1`: enable the chip's DIO2 RF-switch control on boards wired that way
- `IOHC_RADIO_RX_EN`: dedicated external RX enable GPIO
- `IOHC_RADIO_TX_EN`: dedicated external TX enable GPIO
- `IOHC_RADIO_TCXO_VOLTAGE`: SX1262 DIO3 TCXO voltage code
- `IOHC_RADIO_TCXO_DELAY_US`: initial TCXO startup delay in microseconds; the driver now retries longer delays automatically if XOSC start errors persist

### Build Configuration

Add the radio selection flag to your `platformio.ini`:

```ini
build_flags =
    -DRADIO_SX1276    ; for SX1276-based boards
    ; -DRADIO_SX1262  ; for SX1262-based boards
```

To override default pin assignments:

```ini
build_flags =
    -DRADIO_SX1262
    -DIOHC_RADIO_DIO1=33
    -DIOHC_RADIO_BUSY=34
    -DIOHC_RADIO_RF_SW=38
    ; -DIOHC_RADIO_DIO2_RF_SW=1
    ; -DIOHC_RADIO_RX_EN=21
    ; -DIOHC_RADIO_TX_EN=22
    ; -DIOHC_RADIO_TCXO_VOLTAGE=0x00
    ; -DIOHC_RADIO_TCXO_DELAY_US=5000UL
```

## Documentation

ETS parameters, communication objects, DPTs, pairing workflows, scenes and user-facing diagnostics are documented in the application description:

[Applikationsbeschreibung io-homecontrol](doc/Applikationsbeschreibung-IoHomecontrol.md)

## Service and Diagnostics

The firmware provides an `iohc` serial console for commissioning, service and bench diagnostics. The commands intended for normal commissioning and service are documented in the application description to avoid duplicating ETS/user documentation here.

Useful diagnostic entry points include:

- `iohc status` / `iohcNN status` — show 2W identity and per-channel 1W remote identity separately.
- `iohc extract start [SEC]` / `stop` / `status` / `clear` — arm the temporary 2W device-role responder used to recover the system key from an owned third-party 2W hub during a manual pairing attempt.
- `iohc 1wctrl status` / `iohcNN 1wctrl status` — show effective 1W profile, type, manufacturer, sequence, reserved sequence, and configured/resolved enrollment finalizer.
- `iohcNN pair1w [ADDR] add-only|announce-add` — test reference-style 1W add flows without inserting `0x39` automatically.
- `iohcNN remove1w [ADDR]` — send the explicit 1W remove flow.
- `iohcNN send1w-type open|close|stop|vent|force [TYPE|dst=typed|dst=all|dst=exact ADDR]` — test typed/all/exact 1W destinations.
- `iohcNN 1wctrl reuse2w [MFG]` — diagnostic-only command to intentionally reuse the 2W identity for a 1W profile.
- `iohc pairdiag on|off|status` — show compact pairing, TX, crypto, key, repeat and sequence diagnostics.
- `iohc 2wdiag power auto|always|low` / `preamble auto|N` / `status` / `reset` — independently override the 2W power bit and directed START preamble in RAM for Issue #87-style hardware bisection; continuation frames remain short.
- `iohc proto selftest` — run byte-exact protocol self-tests on-device.

## Function Properties (advanced)

ETS function property interface (objectIndex=160, propertyId=10).

| Code | Request payload | Response payload | Description |
|------|-----------------|------------------|-------------|
| 0x10 | `cmd, channel[, nodeIdHi, nodeIdMid, nodeIdLo]` | `status` | Start pairing on the selected channel. The optional 3-byte node ID override is used for 1W commissioning. |
| 0x11 | `cmd` | `status` | Cancel the active pairing session. |
| 0x12 | `cmd, channel` | `paired, nodeIdHi, nodeIdMid, nodeIdLo, controllerState, lastPairStartStatus, profileChannel, profileNodeHi, profileNodeMid, profileNodeLo, manufacturer, sequenceHi, sequenceLo, broadcastType` | Read pairing state plus the effective 1W profile diagnostics for one channel. The profile fields are unused and should be ignored for 2W channels. The sequence field reports the active profile sequence; the console status additionally shows the reserved high-water sequence. |
| 0x13 | `cmd, channel` | `status` | Unpair the selected channel, erase the stored key, and persist the change. |
| 0x15 | `cmd, channel` | `status` | Generate a new own 1W controller profile. Rejected for shared profiles or while any paired channel uses the profile. |
| 0x16 | `cmd, channel` | `status` | Start the 1W clone listener on the selected channel with the default timeout. The original remote must then send its manufacturer copy frame. |
| 0x17 | `cmd` | `status` | Start the active 2W key extraction responder with the default timeout. Afterwards trigger the manual add-device flow on the owned third-party gateway. |
| 0x20 | `cmd, channel, percent` | `status` | Test helper for sending a position command to an already paired device. |

Common status byte values used by commands `0x10`, `0x11`, `0x13`, `0x17`, and `0x20`:

| Value | Meaning |
|-------|---------|
| 0x00 | Command accepted / executed successfully |
| 0x04 | Command rejected because the controller is currently busy |
| 0xFF | Invalid request, channel out of range, or command not supported in the current state |

Status byte values specific to command `0x15`:

| Value | Meaning |
|-------|---------|
| 0x00 | New own 1W controller profile generated and persisted |
| 0x02 | Channel is not configured for 1W, or profile generation failed |
| 0x03 | Profile is currently used by at least one paired channel |
| 0x04 | Channel uses a shared profile instead of its own profile |
| 0xFF | Invalid request or channel out of range |

For command `0x16`, the currently used status byte meanings are:

| Value | Meaning |
|-------|---------|
| 0x00 | 1W clone listener armed successfully |
| 0x02 | Channel is not configured for 1W |
| 0x04 | 1W clone listener could not start because the controller is busy or the selected profile is shared |
| 0xFF | Invalid request or channel out of range |

For the `0x12` status query, `lastPairStartStatus` is currently encoded as follows:

| Value | Meaning |
|-------|---------|
| 0 | Last pairing start request was accepted |
| 1 | Last pairing start request was blocked because the controller was busy |
| 3 | Generic pairing start failure |

Value 2 is retired. It previously reported a missing 1W target node ID; a 1W controller commands typed broadcasts and needs no target node.

## Architecture

### Radio Abstraction

The radio layer uses compile-time polymorphism to support multiple Semtech chips without virtual dispatch overhead:

```
src/radio/
├── Radio.h              ← selector: typedef Radio = RadioSX1276 or RadioSX1262
├── RadioTypes.h         ← shared enums (RadioState, RadioError)
├── RadioSX1276.h/.cpp   ← SX1276 driver (register-based SPI)
├── RadioSX1262.h/.cpp   ← SX1262 driver (command-based SPI)
├── sx1276Regs-Fsk.h     ← SX1276 FSK register map
└── sx1262Regs-Fsk.h     ← SX1262 command opcodes and register addresses
```

Both drivers expose an identical public API. The controller includes `Radio.h` and uses `Radio mRadio;` — the only conditional code in the controller is the `init()` call (different pin arguments per chip).

### Channel Instancing

This OFM defines a **single channel template** (`IoHomecontrol.templ.xml`). The OAM (application module) controls how many instances are created via `op:define NumChannels="16"`. ETS always shows the complete channel-selection table: channel 1 is enabled by default, channels 2–16 are disabled by default, and each enabled channel can additionally be suspended without losing its configuration.

```
IoHomecontrol (OpenKNX::Module)
├── IoHomecontrolChannel[0..15] (OpenKNX::Channel)  — one per paired device
│   ├── Pairing data (node ID, encryption key, challenge)
│   ├── Persistent 1W virtual-remote profile (controller ID, key, sequence reserve, type, manufacturer)
│   ├── Position tracking (current, target, opening/closing time)
│   ├── Status polling (interval, timer)
│   ├── Thermostat state (temperature, mode, presence — Cozy devices)
│   └── KO callbacks (position, up/down, stop, slat, thermostat, status)
│
└── IoHomeController
    ├── Radio (SX1276 or SX1262 via compile-time selection)
    ├── Command Queue (8 entries, circular buffer)
    ├── State Machine (Idle → TxPending → TX → WaitResponse → Process)
    ├── Pairing State Machine (2W discovery/key-transfer/auth; 1W announce/add/remove)
    ├── Network Scan (passive packet capture, per-node stats; observation-only)
    ├── Remote Observation (track remotes, link to devices)
    ├── Passive Key Sniff (explicit session, retained capture result)
    ├── Active Key Extraction (temporary 2W device-role responder)
    ├── Protocol Builders (centralized 1W/2W frame templates)
    ├── IoHomeFrame (frame serialization/deserialization, 9–32 bytes)
    └── IoHomeCrypto (AES-128 ECB, HMAC, CRC-16 Kermit, IV construction)
```

## Security

- **AES-128 ECB** encryption for all authenticated commands (via mbedtls)
- **6-byte HMAC** for frame authentication
- **Challenge-response** protocol for bidirectional verification
- **Per-device encryption keys** derived during pairing
- **2W system key** (16 bytes) generated per module, persisted in flash
- **Independent 1W controller keys and monotonic sequence counters**, persisted per profile with reserved high-water sequence windows
- **Dedicated 1W key-transfer encryption** based on the 1W remote/controller node address
- **CRC-16 Kermit** checksum on explicit raw/diagnostic frame paths
- Replay protection via 2W challenge freshness and persistent 1W sequence reservations

## Radio Protocol

- Modulation: 2-FSK, 38.4 kbps, no shaping
- Bandwidth: 250 kHz, deviation: 19.2 kHz
- Frequencies: 868.25 / 868.95 / 869.85 MHz (3-channel hopping)
- Preamble: 1024 symbols for long/first transmissions, 8 symbols for short follow-up/repeat transmissions
- Sync word: 0xFF 0x33 (preceded by 0x55 preamble anchor byte)
- Packet format: variable length, hardware CRC (CCITT), io-homecontrol mode enabled
- Frame size: 9–35 bytes; authenticated 1W frames declare the appended HMAC in CTRL0
- 2W `0x3D` ChallengeResponse carries HMAC bytes as command data, not as an appended frame HMAC
- 1W `0x30 SendKey1W` defaults to 29 bytes total; selected profiles may use the observed 35-byte form with a six-byte trailer MAC outside the declared length
- 1W frames use the low-power flag and type-dependent broadcast destinations

## Related protocol sources

The io-homecontrol protocol support in this module consolidates findings from several open-source projects. These projects cover different parts of the protocol and hardware landscape, including protocol documentation, 1W and 2W communication, ESP32 targets, SX1276-based radios and later SX1262 support.

- [Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol) — main protocol documentation and reference source for this implementation.
- [cridp/iown-homecontrol-esp32sx1276](https://github.com/cridp/iown-homecontrol-esp32sx1276) — ESP32/SX1276 implementation reference.
- [psolyca/iown-homecontrol](https://github.com/psolyca/iown-homecontrol) — 1W/2W implementation reference with detailed command and crypto handling.
- [CyrilOpenSource/iown-homecontrol-esp32sx1276](https://github.com/CyrilOpenSource/iown-homecontrol-esp32sx1276) — ESP32/SX1276 1W/2W implementation reference.
- [rspaargaren/iohomecontrol](https://github.com/rspaargaren/iohomecontrol) — ESP32 implementation reference with additional 1W-focused work.
- [samr037/iohc-flipper](https://github.com/samr037/iohc-flipper) — public KLI-compatible VELUX pairing reference for manufacturer/ACEI values, multicast ADD destinations, and STOP/DOWN finalization.
- [nicolas5000/io-rts-esp32](https://github.com/nicolas5000/iorts-esp32)- — ESP32 implementation reference for io-homecontrol 2W and legacy RTS.

## Credits

- io-homecontrol protocol research and implementation references: see [Related protocol sources](#related-protocol-sources)
- OpenKNX framework: [openknx.de](https://openknx.de)
