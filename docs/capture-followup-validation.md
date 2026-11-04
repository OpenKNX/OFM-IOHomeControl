# Validate new captures and the failed REG1 release target

The audit baselines were OFM24496af and OAM4bd120b. The exact historical OFM
workflow completed successfully. The exact historical OAM workflow completed
with failure: its commit job overflowed REG1 DRAM by552bytes; its branch job
also failed. The independently compiled ESP32-S3 targets were insufficient to
establish that board's build result. Optional diagnostic buffers now allocate
on request, and the same REG1 release target links locally with the pinned
inputs. Historical remote results are retained as reported, not relabeled.

| Change | Software evidence | Remaining gate |
|---|---|---|
| High-FP replies | Literal Adelis04 packets, compact raw-mask/length matcher, key/owner/generation/frequency/deadline and negative state tests | Other models and parameter meanings |
| Adelis sweeps | Earlier plus new register observations, BOOST/display report, distinct model gates;0137 temperature candidate withdrawn | Device type/subtype and controlled displayed temperatures; no new writer |
| Garage | Real FE70/71/0D packets and synthetic proof replay; no false completion/reexecute; moving survives unknown current | Supervised real door movement and RF A/B trials |
| RS100 daylight | Exact logs6/7, consistent4–7 importer, complete safe dumps and byte diffs, external meter geometry | No raw4A/4B stream in these logs, sensor units or full subscribe/reboot lifecycle |
| Foreign transfers | Literal headers/closures, no automatic response, token/state/key preservation during own reads | No inferred complete foreign session or namespace unlock |
| Typed discovery | Actual application TX summary and menu outcomes, negative proof/assignment and re-arm regression | No full29 packet or authenticated subsequent pairing stage |
| Release validation | CI path coverage for importers/tests, fresh native fragments, new cases in both build modes, storage/corpus suites | New remote workflows require publishing the commits; physical acceptance is separate |

The current workflow now triggers on importer/Python-test changes and runs the
new capture checks in both release and debug modes. RF-admission bypass remains
permanent only in debug; a passing release regression cannot depend on it.
No new channel/ETS mapping, automatic subscription, private key readback,
unqualified Atlantic/garage writer or global RF timing was introduced.

The original manufacturer-selector findings remain the reference for object
addressing. Full4016→host15→RF73 application selection and8100/8103/Dynamic2001
schemas are still open; no class2/A6xx raw readback was enabled. Existing manual
ETS direction inversion and profile polarity were retained rather than adding
a second automatic inversion. Physical pairing, FEM/board setup, BUSY
preemption, qualified KNX telemetry and multi-vendor behavior remain supervised
acceptance work documented in the earlier hardware reports.

[Historical OFM workflow](https://github.com/OpenKNX/OFM-IOHomeControl/actions/runs/41880992429),
[historical OAM workflow](https://github.com/OpenKNX/OAM-IOHomeControl/actions/runs/41881011057).
Exact source hashes, call evidence from previous binary work, private original
inputs, build inputs and test logs are retained in Velocet/Findings. No RF or
flashing was performed during this software work.
