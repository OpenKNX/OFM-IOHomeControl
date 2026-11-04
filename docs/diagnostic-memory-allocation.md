# Allocate optional diagnostics when requested

The OAM workflow for4bd120b finished with failure, not queued or success.
The exact-commit REG1/SX1276-IP link exceeded DRAM by552bytes; the branch build
also failed (584bytes). The earlier successful ESP32-S3 checks did not exercise
that board's smaller static-memory budget.

Register-read sample storage and per-device object-timing settings are now
allocated only on an explicit diagnostic request. Both allocations stay bounded
and use non-throwing allocation; failure rejects the request. The empty public
snapshot is immutable. Identity, cancellation, cooldown, timing and debug RF
policy retain their behavior. No channel/ETS storage format changes.

The exact REG1 release target now links successfully with the recorded pins.
Existing register-read and object-transfer regressions pass with lazy storage.
See the local Findings archive for CI job logs, generated inputs and build output.

[Original failed workflow](https://github.com/OpenKNX/OAM-IOHomeControl/actions/runs/41881011057)
is historical evidence; a local successful build does not change its result.
