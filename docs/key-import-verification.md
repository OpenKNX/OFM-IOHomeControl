# Key capture, inventory and assignment are separate

Successful extraction means a network key was captured. Before authenticated
inventory starts, the existing checked network journal durably stores the key
with the unchanged own gateway NID. A failed durable write aborts before discovery.
An identity collision also blocks discovery; it never causes a silent address change.

The final console summary reports key capture, `nodes_found` versus `no_node_ids`,
passive candidates, successful/failed directed verification, own sender and hub.
Zero nodes does not undo captured-key evidence, and does not verify a usable
actuator/controller relationship. Assignment is separate; its existing status 1
means already assigned/no change, not a second key capture or a new pairing.
Existing assignment conflict and checked-persistence rules remain intact.

The local historical `v1dev` implementation and current implementation both have
explicit discovery windows. The anecdotal 1-versus-3 device count has no matching
raw-frame corpus, peer list, radio/wake state and equal sender-authorization baseline.
No causal scan-time/SPE regression is established by that count. This change therefore
retains current windows, listen-channel/recent-channel selection and SPE policy.

Compare the same gateway identity/authorization, key, devices and RF hardware:
run historical and current discovery; record all discovery request/response headers,
MIB, channels, start/end times, accepted/duplicate/malformed counts and passive
versus directed verification. Separate hub/KLR, gateway and actuator NIDs. Only
then adjust a timing/channel policy with an equivalent captured regression.
Physical verification and device-count comparison remain pending.
