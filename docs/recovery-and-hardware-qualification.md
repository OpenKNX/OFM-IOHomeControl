# Replay recovery exchanges and retain physical qualification gates

The reviewed corpus contains literal public moving-poll03/04 packets and
Garage MOTO1000 Execute/04 packets. Source hashes, line numbers, clock times,
frequency and preamble where logged accompany the frames. Challenge/proof
bodies are excluded; controller replay uses explicitly synthetic keys/nonces.
The three linked pairing attachments are now archived privately with hashes;
only phase/outcome summaries are published. Their missing-confirmation failure
messages do not by themselves establish whether a peer retained the key.

New regressions replay a missed closing poll followed by the captured stopped
reply, and garage challenge→proof→silence versus challenge→proof→short04.
Neither continuation nor an Execute ACK can publish measured position or prove
physical door movement. Poll miss grace and authenticated/silent ladders are
checked separately. Existing tests already cover challenged GetName recovery
with missing33, wrong/refused proof, unchanged keys and all three receive-first
status-ACK channel orders; those implementations were retained.

A production-channel harness verifies 99.66%,0.25%,98%,60% presentation while
preserving measured fractional values. It also exercises scene inversion in
both1W/2W modes. Existing ETS direction inversion remains the explicit policy;
a type-derived awning default still needs protocol evidence and an upgrade
policy. No second inversion was introduced.

The controller audit found foreign traffic on DEBUG/opt-in traces, not a generic
warning flood. Existing event trace on/off is the diagnostic verbosity control;
owned authentication failures retain their handling. There is no opcode-wide
blacklist hiding Atlantic beacons.

Sources: [endpoint application observations](https://github.com/laberning/home_io_control/issues/151#issuecomment-6450246295),
[moving poll trace](https://github.com/laberning/home_io_control/issues/178#issuecomment-6451387197),
[missing key confirmation](https://github.com/laberning/home_io_control/issues/161),
[garage outcomes and attachments](https://github.com/laberning/home_io_control/issues/163#issuecomment-6599867061).

## Supervised physical checks still required

- Garage: compare post-STOP failures and successful finals, known/unknown
  midpoint, endpoint-only controls and explicit supervised low-power/preamble
  experiments. Partial/pedestrian remains unqualified; do not automatically
  repeat an uncertain movement. A captured ACK is not a movement sensor.
- Sleeping RS100/VELUX: compare wake/directed flags and all channels on the same
  device. Discovery success followed by directed silence does not justify a
  global LOW_POWER, HMAC, preamble or timeout change. Record actual RF evidence.
- SX1262: exercise the actual preempted BUSY-wait branch. Quiet runtime with zero
  overruns is not branch coverage. LightVar/WhiteLED results remain specific to
  their setup; silence is distinct from CRC/parse failure and foreign1W traffic.
- Heltec V4/FEM: verify official board pins, separate TX/RX FEM switching and
  permitted power. V3 pin configuration on a V4.3.1 board invalidates RF
  conclusions. No global OpenKNX tuning follows from that misconfiguration.
- KLF: preserve keys, correlate extraction and roll call, then physically verify
  VELUX and Somfy motion and reboot retention independently. Repeated search
  success does not establish movement compatibility with every controller.
- LightVar outputs and SIMU/1W enrollment: validate each physical output and the
  manufacturer/manual destination policy, using the documented pairing gesture.
  An ACK with an unconnected load is not a load-state observation.
- Calypso/Adelis/Tatou and RS100 sensors: establish model identity, complete
  pairing, actual display correlations, raw daytime/shade samples and explicit
  subscribe/unsubscribe/reboot behavior before adding controls or sensor KOs.

No physical trial, flashing, RF transmission or global tuning was performed in
this software work. Debug RF remains unlimited.
