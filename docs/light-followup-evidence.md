# Original follow-up evidence and remaining qualification

Sources: [RS100 afternoon observations](https://github.com/laberning/home_io_control/issues/156#issuecomment-6665992911), [maintainer object/subscription correlation](https://github.com/laberning/home_io_control/issues/156#issuecomment-6640070627), [garage A/B originals](https://github.com/laberning/home_io_control/issues/163#issuecomment-6662545548), [directed pairing](https://github.com/laberning/home_io_control/issues/187#issuecomment-6655235183), [window travel](https://github.com/laberning/home_io_control/issues/205), [remote half duplex](https://github.com/laberning/home_io_control/issues/209), [unheard window movement](https://github.com/laberning/home_io_control/issues/207), [upstream correction](https://github.com/laberning/home_io_control/commit/da10f29ba448a68489eb1e38c131a38fc1283a5d).

`tools/import_light_followup.py` reproduces the sanitized evidence from a private original directory containing `downloads.json` and the attachments. `--check` validates the public JSON and generated header without requiring private inputs. Public source URLs, original SHA-256, line numbers, literal clock/frequency and evidence class accompany each observation. Authentication and unknown object bodies are omitted. Application summaries remain B even if they print a full envelope, and do not enter generated class-A RF vectors. Masked or incomplete frames remain metadata only. No missing nonce/MAC/header is reconstructed.

| Original | SHA-256 |
|---|---|
| [somfy-log.txt](https://github.com/user-attachments/files/34441214/somfy-log.txt) | `44a1e388b5e8731039d58b0241494dc6c764a05a7c2b8bde356107f706cda9b8` |
| [rs100-log-8.txt](https://github.com/user-attachments/files/34391251/somfy-io-hub-logs-8.txt) | `43d2a521e653885d98b8341dc8a12757078269f13d60d83ac52dbdf9979ecc66` |
| [garage-34433992.txt](https://github.com/user-attachments/files/34433992/hioc-heltec-v3-logs.7.txt) | `b7c1885a99226b8d472788f8cd1ea7b5675e3a2337c9bdf6a734bf992b3ba049` |
| [garage-34433993.txt](https://github.com/user-attachments/files/34433993/hioc-heltec-v3-logs.3.txt) | `c6339ccd7ef87fd8c09badbc042aae3185389ea8870d1e32017d8ae824867ede` |
| [garage-34433994.txt](https://github.com/user-attachments/files/34433994/hioc-heltec-v3-logs.4.txt) | `8766b4a28932062b790a0e035c9ccf6beb539adf5057e76fda0c4007bfdfb6bf` |
| [garage-34433995.txt](https://github.com/user-attachments/files/34433995/hioc-heltec-v3-logs.2.txt) | `8a62eb7840232be05514c51eb0686805666f108f9e5f764ea71506861e9395b5` |
| [garage-34433996.txt](https://github.com/user-attachments/files/34433996/hioc-heltec-v3-logs.5.txt) | `c7be5cec41c4aea0b7a58ea7c32d3c090f1d255daf205599bea0373e3095f8ac` |
| [garage-34433997.txt](https://github.com/user-attachments/files/34433997/hioc-heltec-v3-logs.txt) | `af0f834658d7f1ec71ab79910b009f0b9537eca25bf71e5eee23bc925c2db1f1` |
| [garage-34433998.txt](https://github.com/user-attachments/files/34433998/hioc-heltec-v3-logs.1.txt) | `fe334cbe604f343bbc3c518baf756af5bfc05205c62722dfeb254d56aa638365` |
| [garage-34433999.txt](https://github.com/user-attachments/files/34433999/hioc-heltec-v3-logs.6.txt) | `feaa30dc2ea2dee518eb4b642453041b4d46b9fc249e58761e586d16c75fd53c` |

All 15 supplied 0x2005 bodies match the afternoon log's Last Device Event lines exactly. These are application observations, commonly following a subscription attempt, not proof of unsolicited reporting or successful inbound authentication in OFM. The evening log reports separate 0x2001 DATA `05 60 20 01 01 64`; it is not a light sample or a validated battery-percentage conversion.

Private class-2/A601 (index 13) reconstruction yields three complete 366-byte dumps: afternoon attachment lines 27 (00:01) and 92 (04:00), evening attachment line 45 (20:54). Only zero-based offsets 327 and 328 differ between adjacent dumps. The public report publishes offset differences, not the unreviewed bodies. There is no matching same-device 0x2005 measurement within seconds of those object reads, so neither a light-field offset nor a voltage/history interpretation is confirmed. The maintainer's raw-744 same-object observation and 11.2→11.6→11.7 V / 25-history interpretations remain externally reported hypotheses pending matching captures. No class2 read allowlist is widened.

The external lux series is from another sensor, displaced 3–4 m horizontally and 1 m vertically, with earlier orientation differences. Raw 25 paired with 3125 external lux and raw zero paired with 10 lux exclude a simple universal factor. No interpolation at missing slots or reference for the 15:00 sample is invented. Obtain the same RS100's timestamped TaHoma `core:LuminanceState` and `core:SunEnergyState`, or colocated identical-geometry lux and irradiance measurements spanning darkness, clouds and shading, before adding physical units or a KNX measurement object.

The new garage Private2 D800/D807 replies `6D D4 00 00 00` and D809 reply `05 64 00 00 00` are opaque read observations. No partial/pedestrian command or percentage meaning follows. FE70 dispatch already records a per-exchange rejection and does not call the channel's persistent error-status setter. It is excluded from the explicit environmental-limitation error classifier and battery fault observation; no permanent-fault behavior needs porting from upstream's separate Active Issue sensor.

Adelis and Tatou remain gated: [Adelis maintainer notes](https://github.com/laberning/home_io_control/issues/202#issuecomment-6640592382) do not establish its packed actuator identity or general heater-write eligibility. BOOST/schedule/display correlations require additional states and physical observation. [Tatou latest application observation](https://github.com/laberning/home_io_control/issues/199#issuecomment-6640230511) and [expert menus](https://github.com/laberning/home_io_control/issues/199#issuecomment-6644677248) distinguish Jumelage, Connexion and Send/Receive key; they do not supply a complete subsequent key exchange. No reset, key replacement, model-wide writer or successful key-extraction claim is introduced. Retain existing foreign-node tests for [neighbor traffic](https://github.com/laberning/home_io_control/issues/201); [LLCC68 initialization](https://github.com/laberning/home_io_control/issues/177) alone does not justify a PHY change.

## Physical acceptance still required

- Same-device RS100 unit/calibration comparison; direct 0x73 response authentication and reboot/unsubscribe costs.
- Controlled garage (DEBUG, INFO) × (preamble 8, 32) with fixed retries and position, measured radio edges and no automatic movement replay.
- Two real remote controls and grouped actuators, with the second remote intentionally unheard; periodic ETS read must correct unseen KLR motion without OPEN.
- Same-window travel and qualified rain-safe operation for the optional main-only mode.
- Same RS100 PROG session comparison of CH2 and discovery-RX directed TX, without reset or premature key persistence.

Native tests and local builds establish software behavior only. Historical successful OFM ef684d9 / OAM 14c948c CI runs validate the baseline, not these new local commits. No fresh remote CI or physical acceptance is claimed.
