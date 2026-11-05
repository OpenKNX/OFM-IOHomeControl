# Light subscription preparation

Sources: [maintainer subscription and object observation](https://github.com/laberning/home_io_control/issues/156#issuecomment-6640070627), [upstream subscription builders](https://github.com/laberning/home_io_control/blob/da10f29ba448a68489eb1e38c131a38fc1283a5d/components/home_io_control/proto_commands.cpp), [published afternoon log](https://github.com/user-attachments/files/34441214/somfy-log.txt).

The offline builder prepares only 0x73 DATA `01 20 05`. It does not send, arm, schedule, persist subscription state or grant trust to a response. Existing `sensor subscribe-default` uses another recovered format. Battery event 2001 and experimental `00 20 05` unsubscribe remain unavailable through this builder.

A direct 0x71 response after a challenged subscription and a later spontaneous 0x71 are distinct paths. The current raw-light cache requires its own inbound challenge proof. None of the supplied application summaries proves that outbound correlation alone authenticates the direct response to the same standard. Failed/silent subscription cannot refresh a cache, claim success or establish that no subscription exists.

Live TX remains withheld until that response policy is separately qualified. A future live diagnostic must require explicit local arming, a single-use peer/channel/key/revision/gateway snapshot, short expiry, one attempt and bounded rate/RF admission. No automatic boot/KNX renewal, retry of potentially executed writes, unknown battery subscription or unsubscribe. Reboot/session expiry invalidate local authorization; device-side persistence, unsubscribe effect, battery cost and RF cost require physical observation. This preparation introduces no live subscription command.
