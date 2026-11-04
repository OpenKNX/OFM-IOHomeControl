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

## Reported typed-response follow-up

The [new tester report](https://github.com/laberning/home_io_control/issues/199#issuecomment-6635970776)
contains an application TX summary for peerF96718, temporary node7BC715 and
advertised type1023. Connexion/Jumelage produced that responder log; the other
Connexion and Envoyer-clef menu paths produced no useful follow-up in those
runs. The [subsequent report](https://github.com/laberning/home_io_control/issues/199#issuecomment-6636254172)
also reports silence. These menu observations are retained separately.

No complete29 RF bytes or next2C/31/key-proof stage are supplied. The typed
response log therefore establishes that metadata was selected, not successful
Tatou pairing or key import. The new native replay uses explicitly synthetic
request/key context with the reported peer address and verifies unarmed silence,
unchanged system key, pending proof and re-arm fallback. Existing peer-lock and
recipient/mode regressions retain their broader exchange coverage.
