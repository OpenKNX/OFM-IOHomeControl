# Empty object reads

Source: [laberning object status correction](https://github.com/laberning/home_io_control/commit/da10f29ba448a68489eb1e38c131a38fc1283a5d), reported RS100 object 0x6105. Tests use synthetic transport replies, not invented RF captures.

An owner/token-correlated READ opening with disposition 7 and length zero ends as `Empty`. No chunk request, close, data publication or retry follows. The appended enum value 12 preserves all previous diagnostic API values. Disposition 3 remains rejected; disposition 7 with positive length is invalid. WRITE behavior, allowlists and authentication stay intact. A later independent read can start normally.
