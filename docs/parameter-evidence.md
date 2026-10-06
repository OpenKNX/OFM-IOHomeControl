# Parameter evidence and capture gates

KLF means VELUX *KLF 200 API Technical Specification* v3.18, Appendix 2.
OVPd means the [Overkiz parameter and class definitions](https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter).
"Capture" means a native io-homecontrol RF trace, not an Overkiz application
payload. A source that defines a semantic does not necessarily define native
RF serialization.

| Feature | KLF | OVPd / class | Velocet docs | Native capture | Production use |
| --- | --- | --- | --- | --- | --- |
| FP1–FP16 selection | FP1–FP3 profile meanings | FPI1/FPI2 and higher-FP products | selection described | FP1–FP3 only | FP4+/FPI2 representation only; TX blocked |
| D803 secured ventilation | Window Opener semantic | indirect alias evidence | command value | 2W shape absent | 1W path only; 2W blocked |
| MIB bit 5 | not defined | raw byte retained | `SyncCtrlGrp` | behaviour absent | no scheduling/group effect |
| response-time class | 5/10/20/40 ms hint | raw byte retained | 5/10/20/40 s | physical qualification pending | recovered CTRL1/MIB directed timeout selector |
| D400 Ignore | access method | defined | documented | existing protocol tests | distinct sentinel |
| F7FF NoFeedback | feedback sentinel | defined | documented | existing protocol tests | no percentage conversion |
| D8xx aliases | selected semantics | class-specific instantiation | definitions | incomplete | exact profile/index diagnostic lookup only |

## OVPd alias applicability

Raw values are the low 16 bits of Overkiz public parameter IDs. The class
files are the source for *where* those public aliases are instantiated. Class
IDs `C0.N` map to packed KLF profile `N << 6` only for a matching known
subprofile. The production registry enumerates known KLF rows; it does not
use a wildcard. Manufacturer restrictions not expressible by the current
lookup are not installed as generic aliases.

| Overkiz class | Known packed profile/subprofile | Manufacturer restriction | Parameter | Raw | Semantic | Production |
| --- | --- | --- | --- | --- | --- | --- |
| C0.1/2/3/6/10/16/17/19/24 | known KLF rows under those classes | none in class file | MP | D800 | memorized position | exact rows only |
| C0.17 exterior Venetian | 0440 | none | FP3 | D800 | memorized tilt | yes |
| C0.7 gate | 01C0, 01FA | none | MP | D807 | pedestrian position | yes |
| C0.5 garage | 0140, 017A | none | MP | D809 | partial position | yes |
| C0.17, C0.24 | 0440, 0600, 0601 | none | MP | D80A | secured position | yes |
| C0.22 heating/cooling generator | no KLF Appendix-2 row | **not Atlantic** | MP | D80F, D812, D813 | comfort, eco, halted | research only |
| C0.14 and setback examples | no confirmed instantiation in inspected class tree | unknown | MP | unconfirmed | setback/other HVAC | no |

[Class source](https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter/IoHomecontrold/Node/Class) ·
[Public alias source](https://github.com/Velocet/iown-homecontrol/tree/main/docs/parameter/IoHomecontrolOVPd/Parameter/Public)

## Required hardware capture matrix

Do not promote any row to production based on a semantic document alone.

1. Known Window Opener `0x0100`/`0x0101`: capture KLR/KLF/TaHoma native 2W
   secured-ventilation command ID, whole payload, ACEI, challenge/response,
   status and target/current feedback. In particular verify or reject
   `01 ACEI D8 03 00 00`.
2. Nodes spanning response-time classes 0–3: record io address, manufacturer,
   packed profile, raw MIB, power mode, command, TX-end, first response and
   final response timestamps. Validate all selected CTRL1/MIB deadlines after actual TX completion; keep KLF turnaround hints separate.
3. Nodes with MIB bit 5 on/off: capture grouping and scheduling behaviour
   before changing production logic.
4. Products using FP9/10/11/14/15/16: capture native Private/Execute FPI2
   and multi-FP layouts before enabling TX.
5. Retest Somfy shutter 2W, RS100 low-power, available VELUX window,
   interior/exterior Venetian, 1W enrolment/control, key extraction,
   polling, favourite, stop, position, tilt and silent profiles on hardware.
