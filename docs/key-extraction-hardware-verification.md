# 2W key-extraction hardware verification

Run this checklist separately with a KLR200, KIG300 and TaHoma. Use an owned
test installation and record radio traces without publishing the extracted
network key.

## Preparation

1. Start with the OpenKNX device and gateway on the same test bench.
2. Record the current OpenKNX 2W controller ID and firmware revision.
3. Remove any stale OpenKNX test-device entry from the gateway.
4. Keep the default extraction preambles for the first run. `iohc extract
   status` must report cold `80` and the radio-specific response default
   (`8` on SX1262, `12` on SX1276).

## Extraction and verification

1. Start **2W-Schlüsselextraktion** in ETS.
2. Start **Gerät hinzufügen** on the gateway.
3. Confirm that `iohc extract status` reports a temporary extraction-device
   ID, then a locked hub only after `0x31`.
4. Confirm successful `0x32` capture and, where the gateway uses it, the
   `0x36 -> 0x37 -> 0x3C -> 0x3D` address-verification sequence.
5. During the 60-second verification window, send a normal KNX movement
   command. It must execute without waiting for the window to expire.
6. Confirm that periodic status polling and discovery/scan operations do not
   take over the radio during the verification window.
7. Wait for authenticated SPE discovery and apply the ETS result.

## Persistence

1. Record the recovered gateway/system node ID and the separate temporary
   extraction-device ID shown after extraction.
2. Power-cycle the OpenKNX device.
3. Confirm that the recovered gateway/system node ID is restored. The
   temporary extraction-device ID must not be persisted as the regular
   controller identity.
4. Run authenticated discovery and send a command to an imported actuator.
   Both must work without enrolling the OpenKNX device again.
5. Power-cycle once more and repeat the command to exclude a one-boot-only
   flash restoration result.

## Preamble tuning

If the default timing fails, change one value at a time:

```text
iohc extract preamble cold 96
iohc extract preamble response 24
iohc extract preamble cold auto
iohc extract preamble response auto
iohc extract preamble reset
```

Overrides are runtime-only. Record the lowest reliable value for each gateway
and radio combination, then reset to defaults before testing the next device.

## Acceptance record

For every gateway/radio combination record:

- gateway model and firmware;
- SX1262 or SX1276 and antenna setup;
- cold and response preambles;
- recovered gateway/system node ID before and after both reboots;
- temporary extraction-device ID used only during extraction;
- whether address verification completed;
- whether a KNX command executed during the 60-second window;
- whether authenticated discovery and an actuator command succeeded after
  each reboot.

The test passes only if the gateway continues to accept the persisted
controller ID after both reboots. A successful extraction before reboot alone
does not establish compatibility.
