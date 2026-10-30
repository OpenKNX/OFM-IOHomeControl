# SX1276 TX/RX timing recorder

This instrument measures MCU software boundaries. It does not repair RF timing,
prove an actuator received a request, or measure physical RX acquisition.

## Build and operation

Add `-D IOHC_SX1276_TIMING=1` to the selected SX1276 environment. The default is
zero, including release builds: no recorder member, ISR instrumentation or clock
reads are included. The enabled recorder has 64 fixed 32-byte entries (2,104 bytes
including metadata, plus the ESP32 portMUX). There is no dynamic allocation.
When compiled in it still starts disabled at runtime. Disabled runtime recording
does not enter the timing lock or read the clock in the hot event hooks.

Local console, while the controller has idle management ownership:

```
iohc radio timing on
iohc radio timing off
iohc radio timing dump
```

`on` clears the old buffer/drop count and arms for the next accepted TX.
Armed background scans are ignored without clock reads or timing-lock contention.
The first TX starts one RF-attempt capture. Only its same-logical-transaction,
same-attempt `3D` authentication continuation can add another TX, with a distinct
TX ID. No-challenge/post-answer timeout, correlated04, peer rejection, driver
failure or the next non-continuation TX freezes it. Later retries require a new
`on`; they cannot overwrite the first attempt. An active capture also has a
10-second diagnostic cap, independent of protocol deadlines. `off` preserves
records and marks an interrupted active capture as partial. `dump` first stops
recording, then prints at most 64 rows. Busy/KNX debug-KO requests cannot change
the recorder. None of these commands transmits RF or changes radio settings.
SX1262 reports unavailable. Enabling this build flag does not enable RF admission
bypass; retain production safety policy for field use.

The status line reports compile support, memory, in-scope drops, ignored scan
events and capture state (0 off,1 armed,2 active,3 frozen), plus optional timestamp
availability. `ignored_scan_events` are intentionally excluded RX-setup events,
not buffer loss. Post-outcome scans cannot fill the frozen capture. Header-only,
armed or zero-TX captures remain unavailable, not zero-latency success. Preamble/sync ISR evidence additionally requires the existing
`IOHC_RX_EDGE_TIMESTAMPS=1` option and genuinely wired DIO4/optional DIO2. The
recorder does not attach extra interrupts or invent absent timestamps.

## Clock, ownership and ISR constraints

All MCU timestamps are the low 32 bits of `esp_timer_get_time()` (the same clock
used by Arduino `micros()`). Differences use unsigned subtraction; intervals
>=2^31 microseconds are treated as unavailable by the analysis tool.

ISRs call `esp_timer_get_time()` directly. The tested ESP32-S3 link places that
function, `dio0Isr`, `timingAt` and critical-section helpers in `.iram0.text`.
Arduino `micros()` was in `.flash.text` in that build, so it is not called from
these ISRs. The record writer is inlined or IRAM-resident and uses only bounded
RAM writes. A portMUX serializes task/ISR access, including 64-bit context copying.
There is no SPI, allocation, logging, formatting or forced hardware read in ISR.

Only the first TX-owned DIO0 ISR entry is saved. Repeated edges and repeated
in-progress polls do not fill the buffer. A newly accepted TX clears edge/poll
ownership; a busy call cannot replace the current TX context. RX DIO0 events
remain PayloadIsr, never the next attempt's TX completion. Timestamp zero is valid.
If completion is found only by the existing IRQ poll, TxDonePolled is recorded;
there is no fabricated ISR/pin-edge timestamp. Full buffers drop new entries and
increment a saturating drop counter; they do not overwrite early evidence.

## CSV events

`IOHC_TIMING_CSV` rows contain:

`us,window_transaction,tx_id,attempt,event,tx_hz,rx_hz,state,cached_irq`

`tx_id` is the driver's accepted-transmit counter, so a challenge answer has its
own ID even within one logical attempt. Zero window transaction/attempt means
no queued exchange context. RX events identify a receive-window observation,
**not** a correlated peer reply, successful authentication or actuation. Retuning
is visible in `rx_hz`; a frame from another controller remains observational.
`cached_irq` is the latest existing software snapshot, not an ISR SPI read.

| Event | Boundary |
|---:|---|
| 0 TxStart | Driver accepted TX and wrote TX mode |
| 1 FirstTxPoll | First driver completion check for this TX |
| 2 TxDoneIsr | First DIO0 ISR entry while software state is Transmitting |
| 3 TxDonePolled | Completion observed without a recorded ISR entry |
| 4 CompletionPollEntry | Entry time of the completion-positive driver call |
| 5 IrqRead | Existing completion IRQ read finished |
| 6 / 7 StandbyBegin/End | Before/after existing standby transition |
| 8 CompletionReturn | Driver about to return completion |
| 9 ControllerCheck | First queued controller check |
| 10 ControllerDone | Controller sees completion |
| 11 ControllerRxBegin | Before controller RX reopen |
| 12 RxSetupBegin | Driver RX setup entry |
| 13 RxRegistersDone | Existing RX setup writes finished, before mode write |
| 14 RxModeWritten | RX mode write finished; not physical acquisition |
| 15 RxReturn | Existing post-mode IRQ read finished; software return |
| 16 ControllerRxReturn | RX call returned to controller |
| 17 / 18 PreambleIsr/SyncIsr | Existing optional RX edge capture |
| 19 PayloadIsr | DIO0 ISR while Receiving |
| 20 PacketRead | Packet-read entry |
| 21 PacketParsed | Parser accepted a frame; no trust assertion |
| 22 NoChallenge | No correlated3C after Execute before timeout |
| 23 PostAuthNoReply |3C observed and3D sent; no final04 observed |
| 24 Response04 | Correlated04 received; not proof of physical movement |
| 25 / 26 CrcError / Unmatched | In-window CRC/correlation observations, not peer identity proof |
| 27 / 28 AdmissionDeferred / DriverError | Local admission or TX/RX servicing failure |
| 29 / 30 AttemptEnd / CaptureExpired | Capture boundary or10s diagnostic cap; incomplete if no outcome |
| 31 PeerRejected | Correlated error response |
| 32 / 33 NoResponse / CaptureStopped | Non-Execute timeout / interrupted capture |

Earlier `rx_ready_delay_us` traces retain their software-observation meaning.
Use the new distinct events to expose previously hidden observer delay. Missing
events, absent pins, partial attempts and trace drops must remain explicit.

Export a console capture and summarize it offline:

```
python3 tools/analyze_radio_timing.py capture.txt > timing-summary.json
```

The tool separates dump segments/attempts, groups a request and3D by their logical owner, detects conflicting TX IDs, rejects unowned scan rows from latency statistics, handles wrap/zero, and reports missing
intervals as JSON null/unavailable, and provides min/median/max (p95 only with
at least 20 samples). It does not infer peer silence, CRC success or trusted
response ownership. Full/drop-affected captures are incomplete. Combine the
timing file with existing per-attempt status/outcome traces and an independent
RF capture; never export keys or authentication bodies.

## Hardware capture procedure

Verify the actual board revision and its startup GPIO report before wiring.
Example **source configurations**, not independently verified tester wiring:

| Environment | DIO0 | NSS | SCK | MOSI | MISO | DIO4 |
|---|---:|---:|---:|---:|---:|---:|
| ESP32-S3 DEV SX1276 | 7 | 12 | 14 | 13 | 15 | 20 |
| XIAO S3 SX1276 | 2 | 5 | 7 | 9 | 8 | 3 |

DIO2 and free debug-marker GPIOs are unspecified. Verify physical connections,
levels and free pins; do not assume another board's map or load RF/clock lines.
Capture DIO0, NSS, SCK and preferably MOSI/MISO plus ground. An optional verified
free GPIO marker can synchronize MCU and analyzer clocks; this patch does not
claim to supply a physical pin-edge or synchronization marker measurement.

Start with a few isolated ordinary status reads, laberning off and background
polls paused through supported controls. `iohcNN status` only prints state; it
is not a transmit command. Use an already authorized single-read path, not an
invented console command. Keep geometry/PHY fixed, then compare rest/travel and
recorder off/on. Log exact commits, build flag, GPIOs, RF budget mode, acquisition
settings and apparatus. Run `iohc duty debug off` and inspect `iohc duty status`
before unshielded field testing; allow budget recovery. Spread larger sample
blocks over the permitted budget or use a shielded environment.

Independently capture the transmitted request and any peer reply with a sniffer
or SDR. A logic analyzer alone cannot establish actuator reception. Keep physical
DIO0 edge, MCU ISR entry, first poll, controller detection, RX mode-write completion
and first actual preamble separate. Compare intervals within one clock domain
unless a synchronization edge exists. No pairing/key changes, speculative 6F
writes, 2E02 probes or safety bypass are part of this measurement.

## Validation and remaining qualification

Native tests compile the actual SX1276 driver in both gated/ungated modes and
exercise delayed service, repeated edges/polls, missing optional pins, RX/TX
ownership, IRQ-only fallback, wraparound and overflow. The offline parser has
separate wrap/missing/segment tests. Existing protocol/radio/corpus checks remain
applicable. ESP32-S3 instrumented SX1276, ungated release and SX1262 module-harness
builds are checked separately; see ROOT-CAUSE for the pinned-dependency integration details.

Hardware A/B latency/overhead remains unmeasured. With recording on, clock reads,
short critical sections and writes have bounded but nonzero overhead; do not
claim a numeric latency bound from native tests. With the build gate off, the
recorder and event path are removed. Only after synchronized captures establish
a causal delay should a separate minimal RX servicing repair be proposed.

## Tester handoff regression (2026-10-30)

The supplied third log retained64unowned RX-scan rows spanning80,593us and
reported24,649drops. Those rows cannot measure TX_DONE/RX latency. The regression
simulates65seconds of5ms scans before the TX and65seconds after its terminal
marker, with zero in-scope loss. Auth continuations, polling fallback, frozen
captures, wrap, exhaustion and serialized ISR/task writing are tested separately.
The writer is forced inline into the driver's IRAM function to avoid Xtensa
COMDAT literal-pool placement errors; final ELF section/call checks remain part
of target validation.

The OAM dedicated environment is `timing_OpenKNX_ESP32_S3_DEV_SX1276_IP`. It
includes Logic/FunctionBlocks using the revisions declared by dependencies.txt.
Do not substitute newer shared module checkouts while retaining an old knxprod.h.
