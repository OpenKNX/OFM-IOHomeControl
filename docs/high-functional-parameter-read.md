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
is source evidence. No real high-FP reply or physical-device acceptance is
claimed. Native tests cover exact bytes, limits, busy ownership, unchanged
mask semantics and orphaned replies.
