# Answer typed discovery in armed key extraction

The time-limited key-extraction responder selects the advertised profile from
an exact typed discovery address, including `00FFFB` requesting 1023/0.
Open, unsupported and malformed targets retain the existing 2/0 response.
Selection is local to each request; manufacturer and stored identity are kept.
Normal controller discovery, peer locking and key-proof requirements retain
their existing behavior.

[The upstream change](https://github.com/laberning/home_io_control/commit/febcbcfb5e3b66f543b11f4a92eebb4a302c701f)
and [Tatou traffic](https://github.com/laberning/home_io_control/issues/199)
support this controlled experiment. A transmitted answer does not prove Tatou
acceptance or successful key recovery. Native tests cover address validation,
packed response bytes, typed-to-open reset and a newly armed session; existing
unarmed, foreign-peer and ordinary pairing regressions remain applicable.
