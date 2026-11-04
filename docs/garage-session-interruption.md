# Distinguish interrupted garage sessions from movement

The [new garage tests](https://github.com/laberning/home_io_control/issues/163#issuecomment-6635663656)
and five original attachments are retained privately with hashes. Reviewed
00/04/71/FE/0C/0D frames retain their actual bytes and radio metadata. Masked
proof bytes are excluded; controller authentication replay is synthetic.

The regression exercises STOP→3C→3D→silence, a real START71 during that wait,
then a status poll receiving literal FE70. The existing error decoder reports
SESSION_INTERRUPTED in the FE namespace; opcode70 pairing/configuration ACK is
a different message and cannot complete that poll. There is no automatic second
Execute and no measured-position feedback from a sent proof or short reply.

The read-only D809 query and reply0564000000 remain raw observations. The value
does not establish pedestrian movement and is not applied as a channel value
when unsolicited. No new partial/pedestrian command was enabled.

A constructed controller case with target100%, currentD400 withheld and an
explicit moving flag retains moving status without fabricating a position.
The actual production channel method also publishes movement independently of
a position callback. Both already behave correctly, so no guessed UI-state fix
was applied. The upstream Home Assistant direction animation is a different
presentation path.

Low-power-off and48-symbol settings did not establish a general fix in these
reports. Supervised physical trials, real door motion and channel visibility
remain separate acceptance gates; no global RF timing was changed.
