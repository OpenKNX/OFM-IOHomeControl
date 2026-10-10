# Low-power 2W wake policy — 2026-10-10

Implementation of `todo (6).md` against OFM `c9fb3d8` and OAM `719d88c9`.
The OAM `lib/OFM-IOHomeControl` symlink uses the sibling module directly. Changes
are local; no remote publication or firmware upload has been performed. No ETS layout or persistence format changes.

## Software behavior

A fresh Low-Power directed START uses actual moving-status evidence, with an
unsigned 120-second age window. Recent reception is diagnostic (`age_ms`) only.
STOP requires the same motion evidence: its name or optimistic travel position
is not enough. Transport/authentication completion does not establish motion.

| Evidence at exchange start | Attempt index 0 | Index 1 | Index 2 and greater |
|---|---|---|---|
| Actual moving status within 120 seconds | Radio's normal START (SX1262 48, SX1276 32) | Wake length | Wake length |
| No motion, expired motion, or actual stopped status | Wake length | Wake length | Wake length |

The plan and its wake length are frozen for the exchange. A wholly unanswered
Awake exchange consumes the old moving evidence for the next START. A generation
counter protects a newer status received during that exchange, including within
the same millisecond. STOP, challenges/correlated replies, and media-access
failures are exempt. STOP outcomes preserve evidence until actual status changes
it; a full STOP ACK's moving flag updates wake evidence without publishing stale
ACK positions. The pre-STOP trajectory remains governed by the existing snapshot
and settle-poll handling.

Background polls retain one exchange attempt. An asleep poll therefore leads
long, regardless of recent reception. An awake single-attempt poll can still fail;
its next scheduled poll uses long if no new moving status arrived. There is no
new in-poll retry: airtime, existing backoff, STOP-settle's full attempt allowance,
and RF queue priority are preserved. Truncated background PRIVATE status replies
cannot falsely complete a poll. A spontaneous STATUS_UPDATE is dispatched as
status evidence but does not complete a PRIVATE request.

## Runtime controls and bounds

```
iohc 2wdiag wake-preamble auto
iohc 2wdiag wake-preamble 1024
iohc 2wdiag wake-preamble 1536
iohc 2wdiag wake-preamble 2048
iohc 2wdiag wake-preamble 2450
iohc 2wdiag preamble auto
iohc 2wdiag preamble 32
iohc 2wdiag wake off
iohc 2wdiag wake on
iohc 2wdiag status
iohc 2wdiag reset
```

`wake-preamble` is independent of normal START and the explicit `preamble`
override. Default remains **1024**. `wake off` selects the configured wake length
for all known low-power queued START attempts; `preamble N` takes precedence for
all attempts. Reset restores wake=on, wake length=1024, and automatic START/power/
version/discovery settings. All settings are runtime-only. Pairing, discovery,
1W copies/repeats, key extraction, and non-START continuations retain their own
preambles. Challenge response 0x3D remains SX1262 8 / SX1276 12 bytes.

SX1262 accepts 1..8191 complete bytes. Conversion to its 16-bit bit field uses
32-bit arithmetic; 0, 8192 and 65535 are rejected before standby, state mutation,
or SetPacketParams. Packet-param programming also validates defensively. A busy
programming attempt restores the previous software length. SX1276 retains its
1..65535-byte representation. Console/API limits use the actual chip.

TX timeout = estimated airtime + bounded headroom (500 ms for short preambles,
1300 ms at >=1024). The long margin retains the earlier conservative ~1500 ms
1024-byte allowance; it is not a hardware measurement. A queued 2W attempt is
not sent if airtime plus its selected response window exceeds the remaining
session budget. The response selector is unchanged, and the response timeout
starts at observed TX end. A busy RX reopen cannot restart that clock or count
TX airtime twice; reopen is bounded by the response window. Accepted TX with a
lost done IRQ is conservatively accounted once at TX timeout. Repeats and regular
polls use the same queued accounting. MCU estimates do not establish compliance.

## Trace interpretation

`iohc 2wtrace` distinguishes TX start/rejection, TX done observation, RX ready,
RX observed/correlated/wrong peer/unexpected or invalid status, status delivery,
response timeout, exchange failure, and **status_poll_completed**. START,
LOW_POWER, raw PowerSaveMode (255 means unavailable), snapshotted belief/reason,
node addresses, attempt, preamble, channel frequency, LBT attempts/RSSI/bypass,
IRQ/CRC/parser/read-failure counters, and software PHY/IRQ-queue overflow are
available. Payloads remain restricted to the existing functional allowlist;
challenge/MAC/key/object data remains redacted.

TX/RX timestamps are MCU observations, not calibrated RF-edge timestamps.
`rx_ready` records TXdone-to-RXready separately. SX1262 preamble/sync times are IRQ
service observations; unavailable/uncorrelated activity timestamps must not be
used as measured RX edge times. SX1276 marks disconnected DIO4/DIO2 evidence
`not_available`. No missing-pin observation is classified as `no_preamble`.
Error categories separate no response, observed wrong peer/invalid frame/
unexpected command, challenge without final, explicit rejection, media access,
and local failure. An interfering frame observed during a timeout is evidence of
traffic, **not proof of the failure's cause**. The protocol has no recovered
per-request wire token for these legacy status exchanges: an arbitrarily late,
otherwise matching reply from the same peer cannot be distinguished conclusively
from a new reply. Hardware captures must investigate that ambiguity.

## Evidence and outstanding research

| Claim | Level | Result |
|---|---|---|
| Prefix selector chooses 10/54/1960 UART bytes | binary-proven for the supplied STM32 image | Re-disassembled selector 0x080139B8; supplied dump SHA256 `e0c20192affea4bc02514fd19d460b85a95f429b5834e53f1970a0018f946529`. |
| Shared 2W preparation clears LOW_POWER | binary-proven for this path | 0x0800FF26 loads r1=0; 0x0800FF28 calls 0x0800E6DC; its tail calls bit-5 setter 0x08013C78. |
| Longer session-state wrapper uses START=1 | binary-proven for this path | 0x08010984/86 passes 1 to wrapper; shared preparation still clears LOW_POWER. |
| 1960-byte prefix on every real PRIVATE 0x03 START | hypothesis, unresolved | Indirect selector reference at 0x08027E00 and all halfword-scanned direct branch candidates are recorded. Exhaustive concrete PRIVATE producer/virtual call resolution remains open. |
| 2450 SX bytes match 1960 UART 8N1 byte *duration* | derived | Both nominal preamble durations are 510.4 ms at 38,400 bit/s; waveform and framing are different. |
| Optimal default >1024 or ordinary START=64 | hypothesis | No local A/B RF measurement; neither default changed. |
| Actuator RX wake intervals, native MAC wake patterns/channel migration | unresolved | Gateway/host binary does not establish actuator-internal RX intervals. No actuator dump or original-controller IQ capture is available here. |

The bounded disassembly and reproducible scanner are in the OAM project's
`docs/STM32-WAKE-CALLSITES-2026-10-10.json` and
`tools/inspect_stm32_wake_paths.py`. Direct branch scanning is explicitly not
exhaustive interprocedural/virtual-call proof. No original actuator binary was
fetched or assumed to be present.

[External VELUX SSL SX1262 observations](https://github.com/laberning/home_io_control/blob/main/docs/adr/0040-low-power-start-preamble-follows-the-wake-belief.md)
support separate moving/resting tests: short 2/2 moving versus 0/8 resting;
1024 long 0/6 moving versus 8/17 resting. This small external sample does not
qualify our board or actuator. [Cyril's board configuration](https://github.com/CyrilOpenSource/iown-homecontrol-esp32sx1276/blob/master/include/board-config.h)
uses 64; it supplies no reason to change our normal START default without RF
comparison. Compare actual SX1276 0x25/0x26 settings, sync and power-frame waveform
against both implementations with an RF capture.

## Hardware A/B procedure (pending hardware)

1. Record both git SHAs plus local diff, chip/board/antenna, node/device type,
   ETS configuration and power-save mode. Use E50470, optionally 155D81/562292.
   Observe actual resting/moving state. Exclude controller AABBCC when possible;
   no concurrent pairing, key extraction or ETS download.
2. Use an idle RF owner and bounded individual probes. Account all traffic,
   authentication replies, retries, acknowledgements and background polls in
   the same band. Pause automatic polls while collecting isolated probes.
3. Resting baseline: wake-preamble 1024 with preamble auto. Collect individual
   attempts; then repeat identical trials at 1536, 2048 and 2450. Aim for 10–20
   per candidate only within the chosen access/airtime limits.
4. Resting short negative controls: explicit preamble 32/48/64. Moving tests:
   test those short STARTs, then long candidates separately. Deliberately test
   STOP only when physical movement is safe; observe coasting/stopped state.
5. Verify 0x3C → short 0x3D → correlated PRIVATE_RESP, RX turnaround and session
   continuation. Include STOP settle, short rest, 5/20/30 seconds after reception,
   silent/partially authenticated exchange, RF busy, weak signal and multichannel
   RX. Capture original-controller IQ/logic/RF waveform to measure wake prefixes
   and additional wake bursts; decoded SX corpus frames cannot measure them.
6. Run `iohc 2wdiag reset`, status, radio, battery status, then a new baseline poll.
   Confirm normal defaults, free RF owner, no queued poll or battery job conflict.

### Austria/EU airtime ledger

Use the current Austrian interface/access conditions and the matching radio
category. [GenBV 2026 §2](https://www.ris.bka.gv.at/GeltendeFassung.wxe?Abfrage=Bundesnormen&Gesetzesnummer=20013151)
requires compliance with the applicable interface restrictions. The conservative
non-specific SRD duty-cycle alternatives in [CEPT Annex 1](https://efis.cept.org/adhoc_grabber.jsp?annex=4)
are:

| Configured channel | Band | Duty-cycle alternative at <=25 mW ERP |
|---|---|---|
| 868.25 MHz | 868.0–868.6 | 1%, 36 seconds/hour |
| 868.95 MHz | 868.7–869.2 | 0.1%, 3.6 seconds/hour |
| 869.85 MHz | 869.7–870.0 | 1%, 36 seconds/hour |

Alternative compliant access schemes require separate qualification; this
firmware's best-effort LBT bypass is not that qualification. Its inherited global
1% per-channel limiter is insufficient by itself for the 868.95 MHz duty-cycle
alternative. For this bench procedure enforce the stricter external ledger,
including every sender-owned TX; do not run an automatic tight probe loop.
Example: a nominal 32-byte frame at 2450-byte preamble occupies about 520 ms.
Three such transmissions consume about 1.56 seconds before authentication or
other traffic. Under a 0.1% schedule a single 520 ms emission implies at least
519.48 seconds off-time if uniformly spaced. Measure actual airtime and include
all traffic; a larger configured TX guard is not radiated airtime.

### Recording and analysis

`LOW-POWER-WAKE-TRIALS.csv` is a header-only template: **no local measurements**.
Use one row per accepted RF emission, with `trial_id`, `tx_id`, zero-based
`try_index` and all fields from the todo. Added frequency, explicit correlation,
and status-delivery fields prevent counting a foreign/delayed status as a
successful poll. Leave unavailable timestamps blank, not invented zeros. A
media-access failure without emission goes in a separate attempt log, not the
transmitted-attempt denominator.

```
python3 tools/summarize_wake_trials.py --template
python3 tools/summarize_wake_trials.py docs/LOW-POWER-WAKE-TRIALS.csv
make -C test run
```

Summary groups separate firmware, chip, node, observed motion, preamble,
frequency and interfering-controller state. They report RF attempts, correlated
challenges/final replies, completed polls, separately delivered status, failures,
and unsigned-wrap-safe TX/RX transition durations. Hardware comparison results,
waveform measurements and a changed default remain pending, not simulated.

## Todo disposition

P0 items 1–4 are implemented with the minimal single-attempt polling fix. P1
items 5–8 have code guards, runtime controls, timeout/accounting and trace changes;
TX waveform/duration and hardware IRQ timing qualification remain pending. Native
regressions include recent-heard ages, movement expiration/wraparound, all retry
indices, runtime limits/overrides/toggle, packet stability, STOP result/status
cases, silent evidence consumption, foreign/unexpected/truncated status, budget
limits, and busy RX reopen accounting. Production-method tests cover actual
channel evidence and SX1262 state preservation/SetPacketParams bytes.

P2 received a bounded fresh disassembly and evidence report. Exhaustive PRIVATE
producer tracing, native MAC wake-pattern research, original-controller RF
capture, and actuator-dump analysis are explicitly open. Hardware A/B preparation,
procedure, CSV and analysis tooling are provided; physical execution and the
hardware portions of Definition of Done are pending equipment and observations.

## Validation completed locally

- `make -C test run`: 405 protocol + 230 controller + 63 exchange tests passed;
  6 SX1262 error-format and 13 PHY checks, SX1276 adapter scenarios, 51 channel UI
  tests, 4 wake-trial parser tests, production channel/radio methods, and existing
  battery/standalone/key-import checks passed.
- Production `requestStatus` and `onStatusPollFailed` methods are exercised for
  one-attempt polls, full STOP-settle attempts, 1W exclusion, timeout versus
  authenticated backoff, saturation, bounded tracking window and millis wrap.
- Both final PlatformIO targets compiled successfully:
  `release_OpenKNX_REG1_ESP_V00_11_SX1276_IP` and
  `standalone_develop_OpenKNX_HELTEC_WIFI_LORA_32_V3_SX1262_IP`.
- Continuation TX-done trace and duty estimates use the actual transmitted 0x3D
  frame length/preamble. Trace recording uses cached counters and avoids SPI/RSSI
  reads or console output between TX end and RX reentry.
- Both repository whitespace checks passed; no firmware was uploaded. Generated
  OAM build metadata/webassets were restored after validation. Pre-existing user
  changes remain intact.

These are software/build results. Local over-air answer quotas, measured TX
waveform/duration, GPIO timing calibration and actuator RX intervals are absent.
