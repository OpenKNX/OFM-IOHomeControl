# Retain new Adelis register and BOOST observations

The [new tester comment](https://github.com/laberning/home_io_control/issues/202#issuecomment-6637212886)
adds80 register observations to the earlier corpus. Source order and individual
clock times are preserved, including empty values. The tester reports a physical
change after a climate setpoint command and later BOOST with no temperature
visible on the display. That report supersedes an unconditional claim that
Adelis setpoint writing cannot work.

0103 and0138 both contain C300 in these tests;0115 changes from the earlier
E907 to6608. 0137 now includes0100. These are raw relationships, not independently
confirmed displayed temperatures. The earlier0137 temperature candidate is
withdrawn. Unknown mode/flag values remain raw; the observed mode3 and room
value remain explicitly tentative and cannot publish KNX values.

The pinned upstream climate source has both a profile-dependent main-parameter
route and a SET_TEMPERATURE route with readback. The tester comment supplies no
actual setter TX or device type/subtype proving which branch it selected.
No duplicate writer was added. OFM's existing AGILIA52/1 write gate is retained;
Calypso and unclassified Adelis cannot inherit that register writer.

[Upstream control source](https://github.com/laberning/home_io_control/blob/febcbcfb5e3b66f543b11f4a92eebb4a302c701f/components/home_io_control/platform_climate.cpp)
was compared with OFM's qualified heating path. Golden tests cover the new echo,
raw value relationships, earlier observations, model separation and unknown
mode rejection. Live Adelis identity and controlled display correlations remain
required before model-specific reads or writes are enabled.
