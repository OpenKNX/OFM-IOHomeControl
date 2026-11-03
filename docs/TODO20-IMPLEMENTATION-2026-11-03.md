# TODO (20) implementation and evidence ledger — 2026-11-03

Input: user-supplied `todo (20).md`. Base: OFM 08b491f, newer than the public
revision used by that document. Existing fixes were audited rather than
reintroduced. All tests here are offline. No RF, flashing or physical device
qualification was performed.

| Area | Result | Evidence / remaining gate |
|---|---|---|
| RS100 #156 import | Added reproducible, fail-closed secret-aware importer, reviewed fixtures and timelines | [Provenance](ISSUE156-CAPTURE-PROVENANCE.md); raw originals remain private |
| Complete/partial capabilities | 8100 is 150/150; 8103 is 54/67; incomplete cannot publish as complete | Captured object dumps; transfer envelopes in regression tests are synthetic because no raw 4A/4B stream was supplied |
| Identity reads | Provider 00 objects 8001/8002 explicitly allowed, 14/16-byte captured contents tested | Read-only exact allowlist; key objects remain blocked |
| Transfer cancellation | Fixed ring cleanup dropping unrelated work admitted after cancellation | Actual controller regression across every ring position verifies FIFO and no cancelled 46; remote abort semantics unknown |
| 71 concurrency | Sensor/unsolicited 71 no longer completes pending Execute by opcode alone | Actual controller repro and frame-aware regression; conservative supported final-position association |
| Last Device Event | Existing short-envelope authenticated path retained; added original packet-read timestamp alongside proof timestamp | [Event behavior](STATUS-UPDATE-EVENTS.md); no extra challenge during outgoing exchange |
| 73 registration | Existing explicit experimental controls retained | No automatic writes; successful subscribe/unsubscribe/reboot sequence still needs paired captures |
| Battery | Existing coarse evidence and optional low-battery KO retained | Raw 71 status byte is not promoted to a new battery publication; no numeric SOC or solar units inferred |
| Low-power retries | Existing bounded wake diagnostic retained | Reported first failure then success is evidence to collect, not a basis for copying 400 ms or 1960 ms constants |
| SX1262 scan | Existing final BUSY sample and parse/admission tests pass | Actual RF scan, parse-failure causes, channel reliability and sleeping VELUX remain hardware gates |
| Atlantic | Added exact #199 fixtures, raw 21 observation and conservative 33/34 control gates | [Atlantic report](ATLANTIC-ISSUES-198-199.md); #198 timeline is not a raw pairing capture |
| Original firmware | Existing retained original-binary findings constrain interpretation | New Ghidra startup blocked; portable export inventory does not establish schema or call-chain semantics |

## Binary analysis provenance and limitation

REA attempted the original `docs/parameter/IoHomecontrolOvpd.so` with Ghidra.
Provider startup failed `provider_unavailable/runtime_unavailable`: the Java
probe reported process identity snapshots unavailable for 555 live processes.
The configured Temurin 21.0.13 Java executable works in the terminal. This is a
REA startup failure; no new Ghidra decompilation or function-address finding is
claimed. Portable `rea-native-exports` inspected the ARM32 little-endian ELF
(SHA256 1f765f3e10758cd5434fd252dd330df992aa9be53c19ca90e5f5b32e7c76d6c4),
evidence ev_d56f8d68042151b41e40405571c9f90bd017b1ae8543c43ba6089c83062eda10.
No exported Dynamic2001 name was found; absence from exports cannot prove the
schema. Full inventory and provider failure are retained in Findings.

Prior original 2022 Cortex-M/Thumb analysis traced RF 71 at 08005C80 to host
forwarding and experimental 73 through host action 15 to its three-byte
serializer. Prior shared bytecode exposed event names 2001/2005 with an OVP
caller. These are bounded prior findings, not a completed VELUX host parser or
8100/8103 member mapping. Consult the dated Findings reports for addresses,
references and confidence. The earlier alleged literal 72 EC00 was withdrawn;
no ACK change follows from that invalid reading.

## Prioritized remaining work

1. Restore REA/Ghidra startup and recover the native host event/capability call
   chain, recording binary hashes, addresses, callers and confidence. Do not
   infer schema, ACK or authentication changes from strings/export absence.
2. Collect paired RS100 46/47/4A/4B raw exchanges, complete 8103, lost/duplicate
   part behavior and cancel/reconnect behavior. Replay exact on-air chunks;
   keep synthetic transport cases distinct. No retry-mode change before proof.
3. Collect 71/3C/3D/72 and explicit 73 transactions with controller identity,
   frequency and timestamps, including second-controller activity and reboot.
   Test wrong key, stale proof, foreign controller and unsolicited events during
   Execute. Record physical shutter state and controller registration state.
4. Compare low-power first attempt and success under controlled RF conditions;
   retain raw timing rather than adopting another implementation's constants.
5. Run Atlantic physical gates listed in its report before model controls or
   gateway behavior. Correlate sensor values to physical observations before
   assigning battery percentage, energy or illumination units.

No 0% encoding, key/authentication algorithm, ACK bytes, preamble, protocol
response timeout or debug RF limit was changed. Debug RF remains unlimited.
Firmware builds and native tests establish software behavior only.
