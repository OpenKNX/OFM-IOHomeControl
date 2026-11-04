# Pace object reads without blocking the main loop

`iohc object-timing CHANNEL GAP_MS BUDGET_MS` configures volatile, device-bound
read diagnostics. The gap is 0–2000 ms and the total budget is 30000–120000 ms.
The default remains no extra gap and a 30000 ms total budget. A commissioning
operator can select 650/60000 to compare the reported RS100 continuation pause.
Settings bind node, key and product revision; changed identity restores the
default policy. They are not global RF timing parameters or persisted ETS data.

The gap runs asynchronously after an accepted transfer opening or block and
also before zero-count closure. The transport deadline runs independently;
capacity remains 1024 bytes, per-exchange timeouts and retry modes retain their
existing bounds. No sleeping delay, automatic retry-mode change or arbitrary
class/provider access was added. Native tests reconstruct synthetic431/432-byte
transfers with24blocks, exact boundary timing, zero-count closure, byte retention,
deadline expiry and timer wrap. These are transport tests, not captured chunks.
Physical same-motor A/B success, airtime and KNX latency measurements remain open.

Provider00 object8005 is an explicit read-only choice. Class2/A6xx access remains
withheld pending the original provider/class cross-check and a key-safe schema;
A607 cannot be exposed on general readback surfaces. Tests reject class2 with
8005/A601 and retain explicit status3/zero-length rejection.

[Upstream block-pacing change](https://github.com/laberning/home_io_control/commit/2495df832ca5a14643a48ae4588940a5e6775a79)
and [tester follow-up](https://github.com/laberning/home_io_control/issues/156#issuecomment-6597802482)
motivate the opt-in experiment. The new attachments establish complete8103 and
raw8005 observations; neither establishes subscriptions or sensor units.
