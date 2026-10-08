# 2W sender identity and imported keys

The own gateway NID, remote/hub NID, actuator NID and serial number are separate
identities. Earlier key import explicitly copied the hub NID into the own NID;
this could explain equal RF source addresses. Equal addresses are a possible
cause of interference, not proof of the reported VELUX non-movement.

New imports retain the own controller NID and import only the network key.
New ESP32 identities use a random unicast 24-bit NID, persisted by the existing
network journal before transmitting. Restored identities remain unchanged through
reboot, ETS download, key import and channel changes. No MAC/serial suffix or
foreign controller ID is used to provision a new identity. 1W controller cloning
and sequence reservations are unchanged.

An observed 2W frame with our source NID, or an imported hub with that NID, latches
an identity-collision guard. Active 2W TX is blocked; 1W is unaffected. A received
self-source frame is evidence of a possible collision, not proof of which remote
sent it (exclude local RF loopback in bench testing). Re-importing the same hub
re-detects the collision; observing such traffic after a reboot also blocks TX.
The collision flag itself is runtime-only. Installations with an earlier cloned
hub identity are NOT silently migrated: record bindings/key status and arrange
explicit commissioning/re-pairing of an independent controller identity. No unsafe
console address replacement or automatic pairing-loss migration is introduced.

A shared network key alone does not establish authorization for a different NID.
Existing request/challenge/response crypto and peer/destination correlation remain
in use. Authenticated discovery may fail for a newly independent sender; report
this as verification failure, never claim that key capture authorized every peer.

Hardware acceptance remains pending: alternate KLR and gateway commands on the
same actuator; capture distinct source NIDs, authentic replies and actual movement,
then reboot and repeat. Do not declare success from a completed exchange alone.
