# ETS 2W key-import script compatibility

ETS function-property responses may be indexed host collections rather than JavaScript arrays. The API boundary copies every validated byte into a JavaScript array before callers use array methods. Invalid lengths or non-byte entries fail explicitly.

The script uses loops for formatting, byte comparisons for snapshot identity, and a numeric preview hash. It does not require JSON, Array.map, or Math.imul. The preview hash includes candidate and channel selection, channel count/occupancy/node IDs, and each planned assignment including its existing-node flag. Boot, generation and result revision remain part of the token. A changed job is rejected before assignment.

Import/status/continuation failures show the failing function-property command or ETS parameter stage and preserve the exception. Assignment receipts are acknowledged only after ETS channel parameters have been applied. A failed project write leaves the receipt available for channel synchronization; retrying does not require restarting key extraction. Existing assignments also attempt receipt recovery.

## Validation

The JavaScript regression harness exercises preview, confirmation, three-channel import, commissioning status and receipt recovery with host-like responses and JSON/Array.map/Math.imul disabled. It also checks stale preview rejection, malformed responses and connection cleanup. UI tests check compatibility constraints; OpenKNXproducer checks script entry-point integrity.

Actual ETS/hardware acceptance remains required: import the rebuilt application with matching firmware/application version, extract the key and discover devices, read status, review the preview, confirm on a second click, then program the application. Verify channel settings and pairing persistence after reboot. This compatibility change does not establish that every generic ETS script failure has the same cause.
