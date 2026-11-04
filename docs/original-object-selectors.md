# Verify object selectors in the original library

REA/Ghidra **12.1.4** successfully analysed the actual original
`docs/parameter/IoHomecontrolOvpd.so` on 2026-11-04. The ELF is ARM32 little-endian,
SHA256 **1f765f3e10758cd5434fd252dd330df992aa9be53c19ca90e5f5b32e7c76d6c4**.
These observations come from native decompilation, instructions, vtable bytes and
cross-references, not source-code searches. The full records are in
`ovpd-final.json`; decompiled functions are also saved individually. The session
was exported and closed with an evidence snapshot (28 records; no primitive/workflow replay bindings).

The previous Java process-identity snapshot error did not recur in today's
new REA server. `binary_session` reported Java 21.0.13/aarch64 and Ghidra12.1.4
available, and the first new open/decompile succeeded. No installation or
configuration change was made. There was no connection/provider error in this
pass. A no-target session has no active analysis provider; the successful native
records themselves bind Ghidra12.1.4 to this original artifact.

## Verified selector and request construction

| Finding | Original function/address and references | Confidence |
|---|---|---|
| Native selector is manufacturer, separate from object ID. | `IoDataTransferObject::getManufacturerId` **0x1a2ff0** returns `blob[0]`. `getObjectId` **0x1a2ffc** returns BE16 `blob[1],blob[2]`. Deduplication function **0x1a31d8** calls them at **0x1a335c→0xb66b0→0x1a2ff0** and **0x1a336c→0xb77c0→0x1a2ffc**, and logs both manufacturer and object. | Verified native / high |
| The selector survives request serialization at byte1 after originator. | `getIoDataTransferObject` **0x1a3478** reads at least three blob bytes, optionally BE offset/span, then constructs **originator, manufacturer,00,objectHi,objectLo,offsetHi,offsetLo,spanHi,spanLo**. The originator comes through **0x1a35ac→0xb8900**; literal zero is appended at **0x1a3600**; object/offset/span byte stores include **0x1a3614..0x1a36c4**. | Verified native / high |
| Read access dispatch is separate from writes. | `create` **0x1a4018** gets command access and tail-jumps through **0x1a405c→0xb59a8→0x1a3478** for nonzero/read access. Zero access branches to the separate put function. Ghidra models the tail thunk as a computed jump, so `procedure_callers` alone returns an empty direct-call list; incoming references preserve this edge. | Verified native / high |
| Actual virtual actuator/sensor targets were resolved from constructor/vtable bytes. | Constructor **0x1a30ac** writes vtable **0x2cbedc**. Slot+0x14 is **0x16decc Parameter::fillActuatorFoldable**; slot+0x18 is **0x16d9b8 Parameter::fillSensorFoldable**. The read function invokes actuator host command **10** at indirect site **0x1a384c**, or sensor host command9 at **0x1a3740**. Both fill paths set data and protocol/node/key context. | Verified vtable/argument observations / high; this pass does not independently close every host-command→RF bridge |
| Generic object data is accumulated without a per-object semantic schema here. | `getIoDataTransferObjectAck` **0x1a406c** appends received bytes from index3 into the vector at object+0x4c, calls `CommandManager::restartTimeOut` after data, and on zero transfer word builds an IoBlob State. `readWriteDataAck` **0x1a2738** dispatches listeners using selector extracted from stored token+0x58 and object ID. | Verified native / high for this generic layer; not proof of end-to-end object safety |

## Exact simplified pseudocode

This is an analyst simplification of the separately saved actual Ghidra
pseudocode at **0x1a3478**, not original source:

```c
blob = command.value.asBlob();
require(blob.size() >= 3);
manufacturer = blob[0];
object = be16(blob + 1);
offset = blob.size() >= 5 ? be16(blob + 3) : 0;
span = blob.size() >= 7 ? be16(blob + 5) : 0;
data = {command.originator, manufacturer, 0,
        hi(object), lo(object), hi(offset), lo(offset), hi(span), lo(span)};
if (node.kind() == 1)
    fillActuatorFoldable(foldable, command, node, 10, 9, data);
else if (node.kind() == 3)
    fillSensorFoldable(foldable, command, node, 9, 9, data);
else
    throw SendUnknownNode(command);
storedToken = 0x05000000 | (manufacturer << 16) | object;
storedOffset = offset;
```

Numeric node kinds and host commands are kept numeric, rather than expanding
inferred role names. Decompiled exception/non-return flags and unresolved
indirect calls remain recorded limitations; vtable target resolution above is a
specific constructor-backed inference, not a claim all indirect targets were
resolved automatically.

## Consequences for safe diagnostics

The wire byte called `class` by the upstream probe and `provider` by OFM occupies
the same **manufacturer-selector byte** in this generic request shape. Retain
selector+object together. A class-2 A601 observation must not be silently treated
as class-0 A601, nor should the entire manufacturer-2 namespace be unlocked.

This generic transport contains no recovered per-field schema proving that
manufacturer2 objects **A601, A613 or A615** are free of keys/secrets. Raw captures
are insufficient to establish that property, and this bounded native analysis
has not established it. **A607 remains blocked**. Do not expose unknown A6xx
readback merely because it fits the bounded buffer or its bytes look numeric.
An explicit safe object decision needs an independently recovered model-specific
producer/parser or reviewed confidentiality contract. No firmware change is
proposed by this report.

## Event and capability limits

- Native procedure-name search for `8100|8103|Dynamic2001` returned no named
  procedure, evidence **ev_a6350435c9fcfe3dfe4c0e197f8f6ce20a1314f529ed588a7f88e949d8e51bd3**.
  This is a bounded name observation, **not** proof that the schema is absent.
  Hidden, computed, generic or embedded-bytecode handlers remain possible.
- `IoSubscribeSensorEvent::putSubscribeSensorEvent` **0x19c644** was re-decompiled.
  It uses `FoldableRemoteSensorToSend`, a17-byte body, host/foldable command0 and
  remote sensor info/key context. Its constructor/creator path has incoming
  tail edge **0x19cc28 via0xb4e34→0x19c644** plus symbol-table reference2d2230.
  This is a different family, not evidence that this routine produces RF73.
- No new end-to-end internal4016→host15→RF73 application edge or VELUX-specific
  subscription selection was recovered in this pass. Retained original STM32
  host15→CAA4 three-byte serializer evidence remains valid with its prior
  limits; persistence/unsubscribe/auth policy remain unknown.
- Full **8100/8103/Dynamic2001** member mapping and authenticated event units are
  still unresolved. No numeric battery, lux, ACK or HMAC change follows.

## Evidence

`evidence-index.json` lists complete IDs/parameters/limitations. Principal IDs:
selector getters **ev_45204e531bba91aacca698be36cd6a33365e0be85cffe8ac70d543b40aecd4f1**,
**ev_45a410f454bdaa098ef086cfbfdb4f95a3a8371b3494ee46f7a5e4ea73cc87ef**;
request **ev_0d6cabbc11d50635a5eff6487603b6dcc090e4f11a447cd99c23ba80c634eb15**;
dedup **ev_73aa45c77902a93ea94057fe00a18740de9cb2cb9ff4d0d2ca2be674ca9d87f2**;
vtable **ev_684d91b136bc39ad83f2252b7434d29644946acad60dd4727b37935f067c279d**;
request instructions **ev_89640183cacb7ffca13c41075f656e93303a225ccba446a3b316b532bd1153c0**;
request outgoing refs **ev_0afdd76774b29c1c9d73b9d88f8f704eae0972ea2e3efc2a637bfb59f7dd5839**;
dedup outgoing refs **ev_4bb107f9fa2c5b17267a637d3accfed020e66f916c894a3d9bd43fa68dc32593**;
subscription **ev_316aaecf2953adedba9bda38e062f401f59cfe3264f503b36c2d68ea9bf9e40d**.
