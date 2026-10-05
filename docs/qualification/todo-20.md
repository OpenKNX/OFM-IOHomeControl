# TODO 20: Exact commercial/generation binding

Status: **blocked on unrecovered protocol/product evidence**, after reviewing the dated 2026-10-04 reports supplied in OAM docs.

IoHomeProductBinding combines complete profile/subprofile/manufacturer evidence and GI2 signatures, rejects conflicts and binds only recovered semantic families. The dated report explicitly leaves exact commercial model and generation/database bit32 discriminators unresolved.

Required evidence: Obtain uniquely identifying variant/generation and capacity/signature evidence for each commercial model, including collisions/conflicting observations. Require a unique match; retain expert overrides and never bind from a display name.

Current gate: commercialModelKnown and generationKnown remain false; family matching never grants rfWrite or knxPublish.

Do not mark this item implemented or qualified merely because the read transport, representation model or build passes.
