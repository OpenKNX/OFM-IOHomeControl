# TODO 16: 8100/8103 member serialization

Status: **blocked on unrecovered protocol/product evidence**, after reviewing the dated 2026-10-04 reports supplied in OAM docs.

The dated 2W report sections “exact selectors are unique to discovery orchestration” and “Actuator-capacity object contract” prove the read order 8100 -> 8103 -> event subscriptions. They explicitly do not recover byte ownership for sensorTypesIdentification, listenedRadioChannels, eventingSystem, parametersManagement or fullContentEncoded. Searching normalized Lua consumers cannot recover a mapping already erased below that layer.

Required evidence: Obtain raw replies paired with independently decoded capacity members, or the native/peer serializer. Record field lengths, endian order, sentinels and supported product classes. Add byte-exact vectors before exposing normalized capacities.

Current gate: Allowlisted diagnostic reads remain available; RF writes and subscriptions derived from guessed capacities remain disabled.

Do not mark this item implemented or qualified merely because the read transport, representation model or build passes.
