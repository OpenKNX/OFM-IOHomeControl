# OFM-IO-Homecontrol

OpenKNX firmware module for the direct integration of **Velux, Somfy and other io-homecontrol devices** into KNX. The module uses an ESP32 together with an SX1276 or SX1262 868 MHz radio and does not require an external manufacturer gateway.

Protocol implementation adapted from [io-rts-esp32](https://github.com/nicolas5000/io-rts-esp32) by nicolas5000.

## Supported Devices

The io-homecontrol protocol covers a wide range of motorized building products:

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
- **Up/Down/Stop commands** via standard KNX DPTs
- **Slat/tilt control** for venetian blinds (conditionally visible in ETS)
- **Thermostat control** for Atlantic Cozy io — temperature setpoint, operating mode, presence, window contact (conditionally visible in ETS)
- **Direction inversion** per channel for different mounting orientations
- **Protocol mode** selectable per channel (2-way bidirectional or 1-way unidirectional)
- **Device pairing/unpairing** via ETS buttons (with JavaScript event handlers), ETS function properties, or serial console
- **Automatic status polling** with configurable intervals (30s – 30min); after pairing, the module also tries to enable device-driven status updates when the device supports them
- **Position estimation** during travel using configurable opening/closing times and linear interpolation
- **Battery level detection** for solar-powered devices (e.g., Velux solar) — exposed as KO
- **Signal strength (RSSI)** per channel for radio coverage verification
- **Device name and type code** readback per channel
- **Favorite & ventilation positions** — dedicated KOs for device-stored presets
- **Channel lock/unlock** for child safety or maintenance
- **Error status reporting** per channel (communication error, duty cycle, pairing lost)
- **KNX scene support** (DPT 17.001 / 18.001) — configure up to 10 scenes per channel with device-type-specific scene data directly in ETS (position/action, thermostat temperature/mode, or on/off state)
- **Wind/rain alarm** safety input — auto-retract awnings, close windows on alarm
- **Step-stop (Langzeitbetrieb)** — standard KNX blind behavior for venetian blinds
- **Power-on behavior** configurable per channel (nothing, request status, restore last feedback position)
- **3-channel frequency hopping** across the 868 MHz ISM band
- **EU duty cycle compliance** with per-sub-band tracking (1-hour window)
- **Command queue** with automatic retries (up to 3 attempts, cycling frequencies)
- **Remote observation** — track io-homecontrol remotes on the bus, link devices to remotes
- **Passive/sniffer mode** for diagnostics (listen-only, key extraction from observed pairing)
- **Network scan** with per-node packet statistics and RSSI tracking
- **Encrypted discovery (SPE)** for scanning already-paired devices
- **Flash persistence** of pairing data, encryption keys, and system key (AES-128); scene parameters are stored in ETS parameter memory

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
| **SX1276** | `-DRADIO_SX1276` | RFM95W, HopeRF RFM96W, Heltec LoRa32 v2, TTGO LoRa32 v2 | default, untested |
| **SX1262** | `-DRADIO_SX1262` | Heltec LoRa32 v3, Waveshare SX1262, E22-868T | Implemented and validated on hardware |

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

## ETS Configuration

### Global Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| Anzahl io-homecontrol Kanäle | Number of active channels (1–16) | configured per product |
| Fernbedienungs-Beobachtung aktivieren | Enable tracking of io-homecontrol remotes | Aus |

### Module-Specific Global Communication Objects

| KO | Name | DPT | Direction | Description |
|----|------|-----|-----------|-------------|
| 20 | Modulstatus | 1.001 | Read | Radio subsystem operational (1 = radio initialized and ready) |
| 21 | Discovery starten | 1.001 | Write | Trigger a broadcast discovery scan |
| 22 | Discovery aktiv | 1.001 | Read | 1 = discovery scan in progress |
| 23 | Netzwerk-Scan | 1.001 | Write | Start/stop passive network scan |
| 24 | Netzwerk-Scan aktiv | 1.001 | Read | 1 = network scan in progress |
| 25 | Beobachtete Fernbedienung | 12.001 | Read | Last observed remote address (4 bytes) |

### Per-Channel Parameters

| Parameter | Type | Values | Default |
|-----------|------|--------|---------|
| Beschreibung | Text (40 bytes) | Free text | — |
| Kanal aktiv | Checkbox | Aus / Ein | Ein |
| Gerätetyp | Enum | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Thermostat, Licht, Tor, Schloss, Sonnenschutz horizontal, Vorhangschiene, Lüftung, Schalter | Jalousie / Rollladen |
| Protokoll-Modus | Enum | 2-Wege (bidirektional), 1-Weg (unidirektional) | 2-Wege |
| Status-Abfrageintervall (Fallback) | Enum | Deaktiviert, 30 Sekunden, 1 Minute, 5 Minuten, 15 Minuten, 30 Minuten | 5 Minuten |
| Öffnungszeit (Sekunden) | Float (0.1–300) | Travel time for position estimation (opening) | 30.0 |
| Schließzeit (Sekunden) | Float (0.1–300) | Travel time for position estimation (closing) | 30.0 |
| Richtung invertieren | Checkbox | Aus / Ein | Aus |
| Verhalten nach Neustart | Enum | Nichts tun, Status abfragen, Letzte Position anfahren | Status abfragen |
| 1W Aktor-Node-ID | UInt32 | Visible in 1W mode; decimal actuator node ID for first blind pairing, `0` disables it | 0 |
| Anzahl Szenen | Auswahl | 0–10 | 0 |
| Szene 1–10 Aktion | Enum | Position, Favorit, Lueftung | Position |
| Szene 1–10 Position | Prozentwert | 0–100 % | 0 % |
| Szene 1–10 Lamellenposition | Prozentwert | 0–100 % | 0 % |
| Pairing-Modus | Enum | Anlernen, Entfernen | Anlernen |

Scene parameters depend on the selected device type:

- Position-capable channels use `Szene n Aktion`, `Szene n Position`, and for blinds `Szene n Lamellenposition`.
- Thermostat channels use `Szene n Temperatur` and `Szene n Modus`.
- Light, switch, and lock channels use `Szene n Zustand`.

There is no separate `Szenensteuerung aktivieren` checkbox anymore. The scene KOs become visible as soon as `Anzahl Szenen` is greater than `0`.

`Letzte Position anfahren` restores the last state stored in the feedback KOs after reboot: the last position for position-capable channels and the last on/off state for light, switch, and lock channels.

The ETS button for starting or removing a pairing is available only while an active online connection to the device exists. The per-channel KO `Pairing-Status` remains separate from this and represents the persistent paired or unpaired state on the bus.

For 2W channels, the local pairing state machine confirms discovery with `0x2C/0x2D`, attempts the documented pull-key exchange `0x38 -> 0x32 -> 0x3C -> 0x3D`, and falls back to the established push path `0x31 -> 0x3C -> 0x32` if no reply is received.

For 1W channels, the online pairing action uses the already paired node ID first and otherwise the ETS parameter `1W Aktor-Node-ID`. The ETS value does not mark the channel as paired in advance; `Pairing-Status` remains `0` until the 1W learning procedure has completed successfully.

### Per-Channel Communication Objects (25 KOs per channel)

**Base KOs (all device types):**

| Index | Name | DPT | Size | Direction | Description |
|-------|------|-----|------|-----------|-------------|
| 0 | Position | 5.001 | 1 Byte | Write | Set position 0–100% |
| 1 | Position Rückmeldung | 5.001 | 1 Byte | Read | Current position feedback |
| 2 | Auf/Ab | 1.008 | 1 Bit | Write | Up (0) / Down (1) |
| 3 | Stopp | 1.001 | 1 Bit | Write | Stop movement |
| 4 | Bewegt | 1.001 | 1 Bit | Read | 1 = moving, 0 = idle |
| 5 | Lamellenposition | 5.001 | 1 Byte | Write | Slat/tilt position (Jalousie only) |
| 6 | Lamelle Rückmeldung | 5.001 | 1 Byte | Read | Slat position feedback (Jalousie only) |
| 7 | Favorit-Position | 1.001 | 1 Bit | Write | Trigger device's stored favorite position |
| 8 | Lüftungsposition | 1.001 | 1 Bit | Write | Ventilation position (Fenster / Lüftung) |
| 9 | Pairing-Status | 1.001 | 1 Bit | Read | 1 = paired, 0 = unpaired |
| 10 | Batterielevel | 5.001 | 1 Byte | Read | Battery level 0–100% (solar devices) |
| 11 | Signalstärke | 5.001 | 1 Byte | Read | Radio signal strength (RSSI) 0–100% |
| 12 | Kanal sperren | 1.001 | 1 Bit | Write | Lock (1) / Unlock (0) channel |
| 13 | Fehlerstatus | 5.010 | 1 Byte | Read | Error code (0=OK, 1=comm error, 2=duty cycle, 3=pairing lost, 4=interference) |
| 14 | Szene | 17.001 | 1 Byte | Write | Scene recall for the configured scenes 1–10 |
| 15 | Szenensteuerung | 18.001 | 1 Byte | Write | Scene learn/recall; learn applies only to Position scenes |
| 16 | Wind-/Regenalarm | 1.005 | 1 Bit | Write | Safety alarm — auto-retract/close |
| 17 | Langzeitbetrieb | 1.008 | 1 Bit | Write | Step-stop: short=stop, long=move (Jalousie only) |

**Thermostat KOs (visible when Gerätetyp = Thermostat):**

| Index | Name | DPT | Size | Direction | Description |
|-------|------|-----|------|-----------|-------------|
| 18 | Temperatur Sollwert | 9.001 | 2 Bytes | Write | Target temperature setpoint |
| 19 | Temperatur Rückmeldung | 9.001 | 2 Bytes | Read | Current temperature feedback |
| 20 | Betriebsmodus | 20.102 | 1 Byte | Write | HVAC operating mode |
| 21 | Anwesenheit | 1.018 | 1 Bit | Write | Presence status |
| 22 | Fensterkontakt | 1.019 | 1 Bit | Write | Window open/close status |

**General KOs (all device types):**

For device types `Licht` and `Schalter`, KO `2` / `4` are relabeled to `Ein/Aus` and `Status`. Lock channels currently expose `Status` plus the generic channel-lock KO, while scenes can still use `Szene n Zustand`.

| Index | Name | DPT | Size | Direction | Description |
|-------|------|-----|------|-----------|-------------|
| 23 | Gerätename | 16.001 | 14 Bytes | Read | Device name reported by the device |
| 24 | Gerätetyp-Code | 7.001 | 2 Bytes | Read | Raw io-homecontrol device type code |

## Serial Console Commands

| Command | Description |
|---------|-------------|
| `iohc help` | Show available commands |
| `iohc status` | Show pairing status for all channels |
| `iohc status NN` | Show detail for channel NN |
| `iohc radio` | Show radio driver state; on SX1262 builds `initDev` and `devErr` include decoded Semtech device-error names |
| `iohc radio raw` | Show low-level radio registers and raw SX1262 diagnostics; in standard mode SX1262 should report chip sync `57FD99` |
| `iohc pair NN` | Start 2W pairing, or 1W pairing using the ETS target node ID or current stored node ID |
| `iohc pair NN AABBCC` | Start 1W pairing with known actuator node ID `AABBCC` |
| `iohc pair cancel` | Cancel ongoing pairing |
| `iohc unpair NN` | Remove pairing for channel NN |
| `iohc discover` | Broadcast discovery scan (no pairing) |
| `iohc send NN PP` | Send position PP% to channel NN |
| `iohc set1w NN` | Set channel NN to 1-way (unidirectional) mode, even before pairing |
| `iohc set2w NN` | Set channel NN to 2-way (bidirectional) mode, even before pairing |

**Thermostat commands (for Atlantic Cozy io devices):**

| Command | Description |
|---------|-------------|
| `iohc cozy temp NN TT` | Set temperature (in tenths, e.g., 215 = 21.5 °C) |
| `iohc cozy mode NN MM` | Set operating mode |
| `iohc cozy presence NN 0/1` | Set presence on/off |
| `iohc cozy window NN 0/1` | Set window open/close |
| `iohc cozy poweron NN` | Send power-on command |
| `iohc cozy midnight NN` | Send midnight time sync |

**Remote observation commands:**

| Command | Description |
|---------|-------------|
| `iohc remote list` | List tracked remotes |
| `iohc remote add ADDR NAME` | Add a remote by hex address |
| `iohc remote del ADDR` | Remove a remote |
| `iohc remote link ADDR DEV` | Link a device to a remote |
| `iohc remote unlink ADDR DEV` | Unlink a device from a remote |
| `iohc remote observed` | Show recently observed remote addresses |

**Network scan commands:**

| Command | Description |
|---------|-------------|
| `iohc scan start` | Start passive network scan |
| `iohc scan stop` | Stop network scan |
| `iohc scan dump` | Dump captured packets |
| `iohc scan stats` | Show per-node statistics |

### SX1262 Bench Validation

For a Windows bench test with an attached and ETS-configured SX1262 target, this helper optionally uploads the selected environment, runs `iohc radio` and `iohc radio raw`, and stores the captured output in `artifacts\`:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\validate-sx1262-radio.ps1 -Port COM10 -Upload
```

On standard io-homecontrol SX1262 builds, the helper also expects `iohc radio raw` to report `sync=57FD99`. That is the chip-side sync remap for the protocol sync `55FF33` used by the software-emulated io-homecontrol PHY.

## Function Properties (advanced)

ETS function property interface (objectIndex=160, propertyId=10).


| Code | Request payload | Response payload | Description |
|------|-----------------|------------------|-------------|
| 0x10 | `cmd, channel[, nodeIdHi, nodeIdMid, nodeIdLo]` | `status` | Start pairing on the selected channel. The optional 3-byte node ID override is used for 1W commissioning. |
| 0x11 | `cmd` | `status` | Cancel the active pairing session. |
| 0x12 | `cmd, channel` | `paired, nodeIdHi, nodeIdMid, nodeIdLo, controllerState, lastPairStartStatus` | Read the current pairing state and diagnostics for one channel. |
| 0x13 | `cmd, channel` | `status` | Unpair the selected channel, erase the stored key, and persist the change. |
| 0x20 | `cmd, channel, percent` | `status` | Test helper for sending a position command to an already paired device. |

Status byte values used by commands `0x10`, `0x11`, `0x13`, and `0x20`:

| Value | Meaning |
|-------|---------|
| 0x00 | Command accepted / executed successfully |
| 0x03 | 1W pairing rejected because the target node ID is missing |
| 0x04 | Pairing start rejected because the controller is currently busy |
| 0xFF | Invalid request, channel out of range, or command not supported in the current state |

For the `0x12` status query, `lastPairStartStatus` is currently encoded as follows:

| Value | Meaning |
|-------|---------|
| 0 | Last pairing start request was accepted |
| 1 | Last pairing start request was blocked because the controller was busy |
| 2 | Last 1W pairing start request was rejected because the target node ID was missing |
| 3 | Generic pairing start failure |

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

This OFM defines a **single channel template** (`IoHomecontrol.templ.xml`). The OAM (application module) controls how many instances are created via `op:define NumChannels="16"`. The ETS parameter `IOHCVisibleChannels` lets the integrator choose how many channels (1–16) are active — unused channels are hidden in ETS. This follows the same pattern as OAM-Neopixel segment instancing.

```
IoHomecontrol (OpenKNX::Module)
├── IoHomecontrolChannel[0..15] (OpenKNX::Channel)  — one per paired device
│   ├── Pairing data (node ID, encryption key, challenge)
│   ├── Position tracking (current, target, opening/closing time)
│   ├── Status polling (interval, timer)
│   ├── Thermostat state (temperature, mode, presence — Cozy devices)
│   └── KO callbacks (position, up/down, stop, slat, thermostat, status)
│
└── IoHomeController
    ├── Radio (SX1276 or SX1262 via compile-time selection)
    ├── Command Queue (8 entries, circular buffer)
    ├── State Machine (Idle → TxPending → TX → WaitResponse → Process)
    ├── Pairing State Machine (Discovery → Confirm/ACK → PullKey attempt → KeyInit → KeyTransfer → Confirm)
    ├── Network Scan (passive packet capture, per-node stats)
    ├── Remote Observation (track remotes, link to devices)
    ├── IoHomeFrame (frame serialization/deserialization, 9–32 bytes)
    └── IoHomeCrypto (AES-128 ECB, HMAC, CRC-16 Kermit, IV construction)
```

## Security

- **AES-128 ECB** encryption for all authenticated commands (via mbedtls)
- **6-byte HMAC** for frame authentication
- **Challenge-response** protocol for bidirectional verification
- **Per-device encryption keys** derived during pairing
- **System key** (16 bytes) generated per module, persisted in flash
- **CRC-16 Kermit** checksum on all frames
- Replay protection via challenge freshness checking

## Radio Protocol

- Modulation: 2-FSK, 38.4 kbps, no shaping
- Bandwidth: 250 kHz, deviation: 19.2 kHz
- Frequencies: 868.25 / 868.95 / 869.85 MHz (3-channel hopping)
- Preamble: 1024 bytes (START frames), 8 bytes (follow-up)
- Sync word: 0xFF 0x33 (preceded by 0x55 preamble anchor byte)
- Packet format: variable length, hardware CRC (CCITT), io-homecontrol mode enabled
- Frame size: 9–32 bytes

## Credits

- io-homecontrol protocol: [io-rts-esp32](https://github.com/nicolas5000/io-rts-esp32) by nicolas5000
- OpenKNX framework: [openknx.de](https://openknx.de)
