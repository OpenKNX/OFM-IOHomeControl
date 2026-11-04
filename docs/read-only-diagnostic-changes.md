# Extend read-only diagnostics and capture regressions

Implemented against OFM2b9ff36 and OAM2ae2ab7. Commit headings and documentation
use descriptive names. Existing modified/untracked user files are preserved.
All validation is offline; no flashing or RF experiments were performed.

| Change | Software result | Evidence still required |
|---|---|---|
| Typed discovery | Armed, time-limited responder uses exact requested profile; generic requests reset to2/0; normal pairing unchanged | Tatou acceptance and full key proof |
| High functional parameters | Exact43/78 builders, manual console and function-property diagnostic, ownership and raw-only reply handling | Real Calypso/Adelis04 replies |
| Register observations | Full public Calypso/Adelis RX corpus, echo/length validation, empty versus missing outcomes | Unknown firmware revisions/frequencies remain unknown |
| RS100 attachments | Logs4/5 now retrieved and hashed; complete8103/raw8005, exact class-aware redaction and opening metadata retained | Full27-index association, real chunk/timing/A-B qualification |
| Register jobs | Bounded manual reads/ranges, cancel/deadline, peer/key/revision/system identity, release cooldown and unlimited debug RF | Adelis live reads await actual type/subtype; full Calypso pairing/display qualification |
| Read-only decoders | Explicit Calypso diagnostics for mode/tank temperature/lifetime energy; raw suffixes retained; candidate capacity/Adelis interpretations labeled | Fault sentinels/rollover, target temperature and other units; no new KNX values/writers |
| Object transport | Opt-in per-device nonblocking gap and bounded total budget; synthetic431/432-byte completion/closure; exact00/8005 allowlist | Same-device RF success/airtime/main-loop latency measurements; remote cancel/repeat semantics |
| Original library | Ghidra12.1.4 confirms manufacturer selector plus object ID and read request assembly, with addresses/references | Model-specific A6xx confidentiality and8100/8103/Dynamic2001 schema |
| Recovery and garage | Literal safe03/04/00 fixtures; synthetic authentication replay; no false completed delivery from a sent proof | Real motion and post-STOP controlled A/B trials |
| Endpoint presentation | Production harness preserves99.66/0.25/98/60 raw values and tests one inversion in scenes | Physical tracker/exhaustion behavior and any type-derived inversion default |
| Existing behavior | Existing missing33/GetName proof recovery, moving poll grace/ladders, three-channel ACK ordering, coarse battery and foreign DEBUG/trace policy retained | Physical BUSY-preemption branch, RF setup, multi-vendor/KLF and low-power campaigns |

See [typed discovery](typed-discovery.md), [high-FP reads](high-functional-parameter-read.md),
[capture provenance](register-capture-provenance.md), [register jobs](atlantic-register-reads.md),
[object timing](object-read-timing.md), [native evidence](original-object-selectors.md)
and [recovery/physical qualification](recovery-and-hardware-qualification.md).

## Remaining evidence work

- Recover model-specific original host capability/event schemas and the complete
  application subscription chain. Manufacturer selector2 does not make an
  entire object namespace safe; A607 and unknown A6xx readback remain gated.
- Collect explicit73 subscribe/event/unsubscribe/reboot exchanges and paired
  daytime/shade8005/8103 observations. No immediate reply is unconfirmed, not
  a verified refusal. No normalized-model example justifies lux, W or SOC.
- Obtain actual Adelis identity/display correlations, full Calypso pairing,
  typed Tatou acceptance and readback captures before enabling controls/KOs.
- Perform supervised garage, sleeping-peer, weak-link, correctly configured FEM,
  BUSY-preemption,1W/SIMU and KLF multi-manufacturer tests described in the
  hardware report. Physical success cannot be inferred from software tests.

The previous Ghidra startup error did not recur on today's server. Native
analysis was exported and closed. This does not turn unresolved schemas into
verified facts. No0% encoding, ACK bytes, HMAC algorithm, global preamble,
protocol response timeout, global RX tuning or automatic subscriptions changed.
