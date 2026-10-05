# TODO 18: A607 key/state bridge

Status: **blocked on unrecovered protocol/product evidence**, after reviewing the dated 2026-10-04 reports supplied in OAM docs.

The dated report “A607 is a multi-record SOFOPU/TLV object” recovers generic record separators FF and named fields, but not field-specific widths. RemoteControllerNode::key and native 7FFF1236 accepted sequence state are not proven equivalent to A607 key/sequence representations.

Required evidence: Obtain real A607 records and peer/native serialization linking each named field to persistent key/state. Compare reboot reconstruction with STM direct-send records; distinguish live, wrapped and accepted rolling-code representations.

Current gate: OFM durable allocation remains its own journal. No guessed A607 import can replace keys, sequence or durable high-water.

Do not mark this item implemented or qualified merely because the read transport, representation model or build passes.
