# P1-10 / P1-16: controller-role pull and hardware qualification

Tatou's **Envoyer la clé** makes the answering Tatou the giver. OFM must search
and pull: `0x28 -> 00FFFB`, then `0x38 -> 0x32`; it must not start a key-giving
responder or push `0x31/0x32` to that controller.

Explicit experimental workflow (not hardware-qualified):

- Select an empty OFM channel, the sharing hub's node ID, and a previously known
  network member from paired/inventory identity evidence.
- Put Freebox into **Partager votre réseau / Share your network**. Upstream's
  successful route used Recover System Key OFF; this is not an OFM setting.
- `iohc keyimport controller-pull CHANNEL HUB MEMBER` starts an outgoing typed
  search, temporary pull and bounded proof exchange. The spare channel is not
  assigned, the imported hub ID never becomes the gateway, and no key is printed.
- `iohc keyimport controller-status` reports a session number and numeric stage.
  Stage 6 means the selected member returned a verified MAC. Mere 0x2B/0x37
  reception is insufficient. A wrong giver MAC rejects the entire session.
- `iohc keyimport controller-commit SESSION` is the explicit commissioning action.
  It requires a current verified session, conflict-free existing device bindings
  and a successful checked network-store commit. RAM changes only afterward.
- `iohc keyimport controller-cancel` wipes the candidate. Sessions expire after
  30 seconds; foreground movement cancels the member-proof phase.

This OFM gate is deliberately stricter than upstream's reply-only roll-call
criterion: it asks the selected member for `0x36/0x37`, then a fresh `0x3C/0x3D`
MAC over that exact 0x37 transcript. Support for that exchange on real Freebox/
Tatou network members is NOT YET QUALIFIED. A member that only answers discovery
cannot authorize storage. No zero-proof fallback is available.

Still required: real multi-channel Tatou and Freebox captures, matching known
member proof, reboot persistence, original remote operation, physical open/
close/stop and external feedback. No OFM Freebox/Tatou support claim follows from
native tests or the upstream reports.

Sources: [Tatou direction](https://github.com/laberning/home_io_control/issues/199#issuecomment-6802740191),
[Freebox](https://github.com/laberning/home_io_control/issues/215),
[upstream ADR 0071](https://github.com/laberning/home_io_control/blob/33e2721727008e5b1f3b6d9bcfaeee45fe42c18e/docs/adr/0071-a-pulled-key-without-an-answer-is-proven-by-the-devices-that-hold-it.md).
