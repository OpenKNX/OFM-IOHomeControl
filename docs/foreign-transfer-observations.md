# Keep foreign transfer traffic outside owned transactions

The [published foreign traffic](https://github.com/laberning/home_io_control/issues/201#issuecomment-6635803208)
contains complete46/47/48/4A/4B headers and closures on multiple frequencies.
The reviewed corpus preserves each observation separately; unknown value bodies
are omitted. A manufacturer selector and object ID are retained together, and
BE offset fields are checked without reconstructing a missing transfer chain.

Controller regressions inject every retained foreign observation during an
owned opening and during a pending chunk. Token, transport stage, stored key,
channel/KNX callbacks and TX count remain unchanged. Wrong handle, count, token
and peer are rejected in explicitly synthetic transport tests. Captured48 frames
do not enable writes, and manufacturer0C/object1002 is not added to the allowlist.

Existing demux behavior already passes these cases. No opcode blacklist or
spontaneous response was added; raw observations remain available through the
existing explicit trace controls.
