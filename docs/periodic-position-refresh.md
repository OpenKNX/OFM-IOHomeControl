# Periodic position refresh

Source: [unheard KLR200 movement leaves stale position](https://github.com/laberning/home_io_control/issues/207). The log's absence of a heard frame is modeled as a synthetic scenario, not a reconstructed capture.

The existing ETS “Status-Abfrageintervall (Fallback)” parameter (`cPollInterval`) defaults to 300 seconds. Zero disables periodic polling; low-power 2W devices clamp shorter configured intervals to 60 seconds. The channel's existing timer runs outside active tracking and respects background-poll pauses and the ordinary status-request RF/ownership checks. Battery devices should use infrequent intervals, such as one hour, or disable it.

The production-code harness now executes this exact periodic branch: an unseen foreign movement leaves the cached position unchanged, a delayed status read is submitted without OPEN, and only observed feedback corrects position. It also checks pause, disabled polling, the low-power minimum and active-tracking suppression. No additional timer, RF write, jitter engine or automatic movement is introduced. Channel staggering/jitter remains optional future work; physical correction still requires the device to answer.
