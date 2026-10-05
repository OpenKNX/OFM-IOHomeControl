# TODO 23: KNX publication eligibility

Status: advanced product publication remains gated pending exact product and physical qualification.

The RGB/white publication path requires the bound semantic family, `knxPublish` permission, operational identity, authenticated observations and freshness. RGB additionally requires MP/FP10/FP11 from the same nonzero generation. Correlated object-read bytes and diagnostic FP reads do not become authenticated observations. Changing node/key/profile/manufacturer invalidates channel context and bound observations.

Current `IoHomeProductAccess` grants no advanced product publication. Exact commercial/generation binding and original-peer physical units are unqualified. Existing standard profile KOs use the recovered profile registry; their availability indication describes a fresh supported value, not cryptographic authentication of every response byte. Invalid or expired values withdraw availability, rather than inventing a replacement physical value.

Before enabling an advanced product KO, retain unique identity evidence, measured units/range/sentinels, the corresponding DPT, authenticated same-generation observations and original-peer validation. Specify how unavailable values are indicated, verify expiry and identity changes, and run the real ETS migration campaign. Never enable a publication permission solely because a representation encoder or raw read exists.
