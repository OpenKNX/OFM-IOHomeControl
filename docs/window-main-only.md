# Window main-only option

Source: [same-window KLR200 and hub observations](https://github.com/laberning/home_io_control/issues/205). Published payloads are class B: normal `01 63 C8 00 00 00` (44.5 s), fast `01 63 C8 00 80 D8 06 00` (26.5 s), silent `01 63 C8 00 80 D8 05 00` (about 65 s). No complete RF envelope or independently verified hardware result is supplied by these payload tests.

`iohc window main-only CHANNEL on|off` is a local, volatile opt-in for paired 2W VELUX identities with actual Window profile 4/subprofile 0 or 1 (packed 0100/0101). A manual profile override alone cannot qualify a device. Default ETS/MVS configuration stays unchanged. Node/key/mode/product-context changes erase the option. Current effective and native profile must agree.

Ordinary numeric or special Execute requests then use the existing six-byte main-only builder with the configured controller ACEI and already-selected wire target. No polarity or percentage encoding changes occur. Explicit movement-speed FP requests are rejected rather than silently stripped; ordinary FP-only commands keep their existing behavior. Challenge HMAC covers the selected actual six bytes through the existing queue/exchange path. No new silent/fast presets or automatic motion are enabled.

Existing eight-byte defaults depend on ETS movement mode and MVS, including D300/D805/C800/D806; OFM is not universally FAST. Validate normal/silent/fast over identical travel on the same physical window, with rain limits respected, before claiming measured speed behavior.
