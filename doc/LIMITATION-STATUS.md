# Read-only limitation status (0x25 / 0x26)

Evidence: [laberning/home_io_control commit 215457eb51730429b75a93ef5af29fb45a018b5e](https://github.com/laberning/home_io_control/commit/215457eb51730429b75a93ef5af29fb45a018b5e), plus Velocet OVPd originator and limitation semantics. The capture-derived transport and unrestricted boundaries are observations; the complete five-byte layout under an active limitation is still provisional. No limitation write/set operation is implemented.

## Operation

Enable **Begrenzungsstatus lesen (vorläufig)** in a supported 2W channel's **Funktionen und Rückmeldung**. Default is disabled. It exposes the read-only **Begrenzung aktiv** KO (DPT 1.002). A first refresh is attempted when a paired, recognized actuator becomes available; periodic refresh defaults to 300 seconds. Interval choices are off, 60, 300, 900 or 1800 seconds. No deferred movement refresh is added; that item is optional.

Each refresh issues MP minimum `25 80 00 00`, then MP maximum `25 C0 00 00`, through existing START, LOW_POWER, RF media access and recovered directed deadlines. There is no guessed authentication requirement. FP reads are rejected by the production builder/API. The logical pair holds managed-operation ownership and expires after 5 seconds; this is an OFM bound, not a protocol constant.

Replies must be directed from the expected peer to this controller, belong to the active request/token and unchanged node/key/product revision, and contain exactly five data bytes. Assumed fields are parameter, BE16 raw value, originator and raw timer. Only MP enters the paired state. Overheard or mismatched replies cannot replace authoritative data.

A pair needs both valid, ordered, normal-range MP values from one refresh. `0000 / C800` is unrestricted; a higher minimum or lower maximum means active. Unknown, out-of-range, contradictory, stale or partial values do not mean inactive. Samples expire after 5 minutes independently of the polling interval; longer intervals intentionally leave an unknown gap. Originator and timer do not determine the active state.

First coherent state and changes transmit; repeated equal states only restore/cache the value without a telegram. RF failure never writes a false 0. A validity callback is attached only to each limitation KO; GroupValueRead rechecks identity and freshness at read time. Other group objects keep their normal behavior. Writes are disabled in ETS and rejected by the IOHC input dispatcher.

Selecting minimum or maximum alone is diagnostic and resets the logical pair. It cannot combine with an earlier sample to publish a state.

## Console

- `iohc limitation 1 refresh`: one logical minimum/maximum refresh.
- `iohc limitation 1 min` / `max`: individual diagnostic read.
- `iohc limitation 1 status`: node, context revision, refresh token, result, coherence/active/unknown, raw bytes, normalized percentage where applicable, originator, timer and sample age.

Reads require enabled ETS support and a paired 2W actuator. Controller ownership defers/refuses reads during pairing, key extraction, RCM and exclusive management jobs.

Timer interpretation is KLF/API-derived: 0..252 = (n+1)*30 seconds; FD = unlimited; FE/FF = special/delete. It remains provisional and is not used for cache freshness, polling or automation.

## Software verification

Native tests cover exact requests and LOW_POWER, strict reply sizes/opcodes, preservation of unknown raw fields, originators/timer, coherent ranges, missing/stale/replaced pairs, clock wrap, changing node/key/context, wrong source/destination/parameter, single reads and timeout. Publication tests cover first inactive/active states, changes, unchanged samples and unknown timeouts. The KNX test compiles the real GroupObject methods against a stub ETS flag table and verifies fresh reads, expired reads, invalidation, disabled reads and unrelated-object behavior. ETS checks verify disabled defaults, 2W applicability and the output-only DPT/flags.

## Physical qualification — outstanding

The attached TODO's steps 23–26 require hardware and were not performed by this software implementation. Existing upstream unrestricted captures support the decoder fixtures, but do not constitute a new OFM RF bench test. Polling remains disabled by default.

1. Capture unrestricted VELUX MP min/max. Record complete frames, nodes, RF channel, preamble, timing and LOW_POWER; verify 0000/C800.
2. Capture an active rain limitation with actual window behavior. Record all five bytes; test the value/source hypothesis, including possible source 02.
3. Compare successive timer bytes during active protection; check 30-second steps and FD/FE/FF before promoting timer semantics.
4. Repeat on VELUX window variants, Somfy and other 2W coverings. Establish applicable profiles/vendors before enabling polling broadly.

No completion or physical-qualification claim is made for these steps.

## Status-derived rain evidence and KLF diagnostic probe

Additional source: [laberning/home_io_control commit 38f03e651b8418e21685a79f674a8407ed961c8a](https://github.com/laberning/home_io_control/commit/38f03e651b8418e21685a79f674a8407ed961c8a).

For recognized profiles, trusted ordinary PrivateResponse (0x04) and StatusUpdate (0x71) replies may provide a last-command record: three commander bytes plus originator at offsets 8/11 and 11/14 respectively. Short records and zero commander addresses are invalid. Position, product or diagnostic MP/FP replies cannot be substituted for this ordinary layout. Immediate Execute acknowledgements do not update last commander or create, clear or refresh rain evidence.

Originator 02 gives direct rain evidence. For position profiles, a stopped device whose reported target differs from the pending ordinary MP command by more than 100 raw units (about 0.195 percentage points) supplies weaker clamp evidence, **only** while rain evidence remains recent. A mismatch alone never implies rain. A stopped matching target with a valid non-rain last-command record clears the remembered rain evidence. Predictions are kept through intermediate moving statuses and consumed by a stopped report; failed/rejected commands discard the prediction.

The two-hour evidence hold is provisional software policy, not an IO-homecontrol constant. Ages use unsigned subtraction across the millisecond clock wrap. Node, channel key, controller system key and product-context changes make all earlier rain/error evidence unusable. An absent record cannot refresh an earlier rain originator.

The existing KO uses this priority: coherent fresh explicit 0x25/0x26 range (including unrestricted 0), fresh correlated explicit limitation error, trusted status rain originator, then a clamp backed by recent rain. The console preserves the source separately from the binary value. With no usable source the KO remains unknown and does not answer reads; expired evidence never creates a false 0. Explicit command errors expire after five minutes and an independent ordinary position status supersedes them. The feature remains opt-in through the existing ETS setting; rain inference adds no polling.

`iohc limitation 1 status` also shows the evidence source, last commander/originator and age, rain evidence age, rule, predicted/observed wire percentage and validity, stopped flag, and explicit error code.

`iohc probe 1 status_mp_fp` sends the captured PRIVATE function-01 request `01 FE 01 01 01 01 01 01 01 00` using the target's LOW_POWER setting and normal directed transport. Replies are logged as command/raw payload, RSSI, RF channel/frequency and timestamp. The probe is diagnostic-only: its response layout is not decoded and cannot update position, target, movement, last commander, rain evidence, product cache or KOs. Delayed duplicate replies remain isolated until another request replaces the diagnostic context.

### Additional physical qualification — outstanding

The additional TODO steps 27–30 require hardware. No new RF capture or real rain test was performed here. Capture dry VELUX status, rain-closing 0x04/0x71 records with their distinct offsets, rain-at-rest originator, and OPEN during active rain protection (compare the immediate acknowledgement with the later stopped status). Record complete payloads, identities, channel, timing and actual movement; verify source precedence and expiry before relying on the inference for automation. Other device families still need capture qualification.
