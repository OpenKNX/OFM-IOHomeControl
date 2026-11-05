# Challenge reply timing

Sources: [latest eight garage logs and A/B report](https://github.com/laberning/home_io_control/issues/163#issuecomment-6662545548), [maintainer timing assessment](https://github.com/laberning/home_io_control/issues/163#issuecomment-6640064057).

A reports 6/6 OPEN presses without movement with DEBUG, response preamble 32 and one try. B reports 18/18 eventually successful, 13 first-attempt and five second-attempt, with INFO and preamble 8. Multiple settings changed; this does not isolate logging as the cause. No global preamble, authentication, reply deadline or timeout changes are justified.

`iohc challenge timing on` allocates an eight-entry session-only ring while idle; `off` frees it and `status` prints it later. No keys, nonce or MAC bytes enter the ring. It records local complete packet-read return, HMAC start/end, preparation start/end and result, TX call request/acceptance and result, and observed TX completion with cached IRQ. Valid flags distinguish missing stages from a legitimate zero microsecond clock. Unsigned differences handle clock wrap.

The first radiated preamble symbol and individual hardware BUSY/standby/CCA/IRQ edge times remain unknown. TX-call time is not RF-on-air evidence; observed completion is not the physical IRQ edge. Native tests verify sequencing and unchanged challenge-response behavior, not physical latency.

The correlated challenge path suppresses synchronous controller diagnostic streaming, response-timing prints, pair traces and TX acceptance logs. Raw module frame formatting is skipped for that challenge; structured redacted controller observations remain. Persistence is checked against an already committed identity snapshot rather than writing flash during the reply. Ring allocation occurs only on explicit enable before an exchange. RF admission, immediate-reply qualification and expiry still apply.

Hardware acceptance requires the same motor position and firmware, with fixed retries/power/timeout: (DEBUG, INFO) × (preamble 8, 32), plus a build without frame logging. Capture both ring and physical radio signals; never automatically replay an authenticated unconfirmed movement. FE70 remains an exchange rejection, not a permanent battery/device fault; existing position and error publication regression coverage is retained.
