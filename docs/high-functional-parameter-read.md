# Read high functional parameters manually

`iohc status-fp-high CHANNEL HEX_BITMAP` queues a manual raw function-01 read.
Bitmaps 43 and 78 produce `01 00 43 01 01 01` and
`01 00 78 01 01 01 01`. Empty selections and selections over seven bits are
rejected. Function-property diagnostic 3E takes channel, bitmap and expected
node, with the existing commissioning and management gates.

The existing function-03 mask getter is unchanged. Its argument uses logical
FP bits; logical C200 produces wire `03 00 43`. Passing 4300 is a different
logical selection. Replies remain diagnostic and cannot update position,
product state, rain attribution or KNX values. Key/revision ownership is bound
through the existing queue. There is no sweep or automatic polling.

[Upstream serialization](https://github.com/laberning/home_io_control/commit/febcbcfb5e3b66f543b11f4a92eebb4a302c701f)
is source evidence. The new [Adelis observation](https://github.com/laberning/home_io_control/issues/202#issuecomment-6637212886)
contains a literal868.95MHz high78 reply and a separate standard reply. This
establishes that reply on that device, not another model's parameter meaning.

The compact high-FP reply matcher retains its first four bytes as opaque and
checks the captured zero first mask, requested second mask and exact sparse-word
length. It is separate from the fixed-prefix function03 decoder. Unknown reply
layouts cannot complete this supported diagnostic. No temperature, energy,
position or motion meaning is assigned to the opaque prefix or raw words.

The queue binds controller/system key, channel key/product revision and request
generation. Replies additionally require the active peer, frequency and deadline.
Tests use exact captured addresses/bytes with synthetic keys; wrong masks,
source/destination, frequency, identity/key/revision, expired/truncated replies,
another pending read and orphan replies cannot publish state or take ownership.
