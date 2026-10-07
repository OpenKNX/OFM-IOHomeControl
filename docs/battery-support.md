# Battery support and remaining qualification

Implemented 2026-10-06 against Velocet/iown-homecontrol commit
`b7a526d07506f5192548aadfecf68d0f6e5497fa`.

## Proven semantics

Normal PRIVATE function 03 responses use execute-status byte `data[1]`.
OVPd `IoMainAndFunctionalParameter::mpFpAck` loads `getData()[1]` at
`0x170EB8` and passes it to `sendDetailedExecuteStatusAsState` at `0x170EF4`.
The Velocet utility `Utils/io-utils.lua:getBatteryLevelValue` extracts bits 6:5:
0 unknown, 1 low, 2 normal, 3 full. OFM accepts this only from a complete normal
position/status reply to its own correlated function 03 request (default
selectors); immediate Execute ACKs, private 06/09, FP reads, short responses,
foreign peers and changed key/profile contexts cannot update it.
The legacy byte-60 low-power inference is removed: these bits describe battery
class and cannot establish RF power mode. Discovery MIB and explicit ETS energy
class configuration remain the authority.

A601 PID 1 uses 0 very-low, 1 low, 2 mid, 3 high, 4 unknown. Only the first four
values update the actuator alarm. Unknown/out-of-range values retain previous
confirmed evidence, and neither this enum nor status bits produce percentages.

A607 PID 7 identifies controller role (0 micromodule, 1 one-way controller,
2 standalone controller). PID 8 uses 0 critical, 1 low, 2 medium, 3 high,
4 unknown, 5 notSupported. Diagnostics join role and address from the same
record regardless of field order. Only role 0 with state 0/1 is labelled
micromodule-low. These are paired-controller observations, never actuator SOC.
PID 9 remains raw. A607 PID 3 keys are never printed, and generic ETS object
slice readback is blocked for the entire key-bearing A607 object.

## ETS and validity

A separate BAT bank appends Battery low KOs at 1700..1715 (DPT 1.005).
The existing K12 percentage KO and all legacy parameter/KO offsets are preserved.
Battery monitoring defaults to Disabled; Status only enables passive evidence;
Extended diagnostics permits manual active probes. No startup/pairing battery
object reads or periodic battery polling are enabled. Normal configured status
polling is independent; battery monitoring does not add wakeups.

The alarm sends 1 for confirmed low and 0 for confirmed normal/full. Unknown
never publishes a fabricated OK value. With standard KNX v1dev, marking the
communication state Uninitialized cancels queued publication but does not erase
an already initialized cached value: group reads may still return the last known
value after reassignment or disabled monitoring. Before any confirmed value has
been published, no read response is available. Runtime battery evidence is reset
independently; the first confirmed alarm in the new context is always transmitted,
even when it matches the previous cache. A reboot discards the volatile cache.
Known evidence is retained until a conclusive replacement or context reset; it
has no invented battery-expiry interval. Console ages identify old observations.
No custom KNX core patch is required.

The newest conclusive evidence wins, with wrap-safe millis() comparison. For
equal timestamps, priority is A601 enum, normal status enum, then correlated
result 12 warning; no conclusive evidence means unknown. Both independent enum sources remain visible; conflicts are logged.
The percentage publisher requires percentValid and a matching 0..100 value;
there is no code setting percentValid because no converter is qualified.

## Console and raw object transport

- `iohc battery CHANNEL status`: all retained evidence, ages and raw object fields.
- `iohc battery CHANNEL probe`: one Private06 query.
- `iohc battery CHANNEL probe09`: one Private09 query, after the prior query ends.
- `iohc battery CHANNEL objects`: one explicit read sequence. Somfy manufacturer
  02 starts with A601/A607/A60E, then generic 0009/4003; other manufacturers only
  probe generic objects. Object transfers are bound to node, key and channel profile revision, so a
context change cannot publish a stale completed object. Unsupported replies are preserved as transport results;
  no support is inferred from manufacturer alone.

Object reads use the existing bounded 46/47 + 4A/4B transport. Selection is
provider 02 for Somfy objects and 00 for generic objects, offset zero, up to
1024 bytes. This wiring is software tested, not physical device qualification.
The parser validates the whole object before delivering fields, rejects duplicate
PIDs/truncation, and handles separators only at A607 TLV boundaries (FF inside
values is data). Ordinary fields use PID/lengthMinus1/value; Somfy PIDs above
127 additionally carry a format descriptor and extended size encoding, as
`io-somfy-utils.lua:getParamValue/getParamSizeInformation` proves.

Complete raw snapshots for all five objects are allocated only when queried.
A601/146 (LastBatteryVoltageRaw), 147, 148, 134; 0009/0,1; 4003/128;
A607/9; A60E/2 retain exact bytes/widths. A60E address/profile fields 0/1 are
logged with the same record index. Integer candidates are big-endian signed and
unsigned interpretations, not qualified physical units. High-PID format/scale
and unit descriptor bytes are retained as raw data rather than used to claim
volts, energy or SOC.

Private 06/09 replies log source, destination, command, full payload, RSSI,
RF index/frequency, timestamp, response delay and profile/subprofile/manufacturer.
They bypass legacy position, power-class, product-cache and percentage decoding.

## Dynamic state investigation: still open

The product database names raw state 67362619648 (`0x0FAF200100`) as
core:BatteryLevelState; eventingSystem 2001 enables this and BatteryState.
The supplied KizBox2 libIoHomecontrol.so, OVPd and daemon were checked for the
full/low-word literal and relevant exported battery/execute/event handlers.
Neither a matching literal nor a named battery producer was found; this bounded
reconnaissance does NOT establish that a computed producer is absent.
The exact RF/event envelope, width, valid range and numeric scale remain open.

Extended diagnostics retain complete addressed 71/unknown-opcode candidates as
UnclassifiedRx. They are not labelled event 2001 and never update SOC or the
battery alarm. The 71 execute-status placement has not been independently tied
to the recovered native field; its battery candidates remain raw. No event
subscription is synthesized from a capability ID alone.

## Physical capture checklist (requires devices)

Collect a mains actuator, battery actuator, solar actuator, 1W remote/micromodule,
and a 1W sensor where available. For several real charge conditions record:

- model/profile/subprofile/manufacturer, device and paired-controller addresses;
- timestamp, normal status class, A601 PID 1, all raw battery/solar object fields;
- both private query replies and dynamic candidates;
- simultaneously measured battery and solar voltages, charger/solar state.

Check A601/146 and 147 against several physical voltage levels. Check successive
148 values for cumulative/period/reset behavior. Establish PID134 meaning,
generic 0009/1 range/sentinels, paired A607/9 and A60E/2 widths/scales, and the
2001 producer before writing any numeric converter. Future peer firmware may
resolve schemas that gateway binaries do not describe. Hardware capture and
full SOC qualification remain pending; no voltage-to-SOC curve is implemented.
