# TODO 17: 4300/4302 writable schemas

Status: **blocked on unrecovered protocol/product evidence**, after reviewing the dated 2026-10-04 reports supplied in OAM docs.

The dated 2W report proves 4300 data is CBOR-consumed as discovery-time sensor configuration, and 4302 is a separate settings object with vibration threshold readUInt8(13). It explicitly states that 4302 does not establish 4300 schema or generic write authorization.

Required evidence: Obtain full original object bytes plus independent sensor configuration, native/peer producer, manufacturer header ownership, write request and ACK/disposition captures. Qualify the two objects independently.

Current gate: Retain raw reads; the empty object-write allowlist rejects both schemas. No sibling-object extrapolation.

Do not mark this item implemented or qualified merely because the read transport, representation model or build passes.
