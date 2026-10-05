# Upgrade and recovery contract — OFM 0.7 / ETS application 3.11

This is the implementation's supported policy and an unexecuted acceptance procedure.
It is not a report of a successful ETS download, power-cut test or peer resynchronization.

## Upgrade preparation

Retain the existing ETS project and firmware revision before upgrading. Record channel
nodes, manual recognition permissions, profile overrides, names, group addresses and
scenes. Use the new per-channel **Speicher / Zuordnungsbeleg prüfen** action to record
journal availability, live binding and receipt revision without exposing keys.
Application 3.10 adds a separate product bank at objects 1000..1095. Existing IOHC
objects 600..999, their 25-object stride and 68-byte parameter stride stay unchanged.
Product-object visibility is ETS-only and defaults off. Showing objects cannot grant
RF write or publication permission. A successful project update still needs a download.

The producer validates XML/header integrity, but real ETS import, upgrade, group-address
retention, download, restart and old-client compatibility must be tested before release.
No signed product is available without an ETS installation.

## Identity and assignment authority

The checked `iohcnet` journal owns global 2W controller identity/system key and managed
channel bindings. The legacy OpenKNX image mirrors metadata; it must not restore an old
key over a committed network binding. Assignment receipts are a separate checked store.
Pairing and import prepare a receipt before committing a binding, then update RAM.
Only receipts matching the live key and committed binding can be read/ACKed or restored.
A failed binding commit can leave an orphan receipt; it must not become a pairing.

Receipt revisions advance from the stored per-channel receipt, independently of job
numbers. Unpair replaces the receipt with a key-free tombstone retaining its revision;
re-pair advances the revision again. This invalidates stale ACKs even for the same node
and key. Older firmware does not understand tombstones: downgrade after using this
format is not a supported migration. Do not erase receipts to make a downgrade appear
successful. Cross-store physical interruption tests remain required.

After an ETS exception, read the status/receipt and repeat the existing assignment
resume/apply action. Preserve manual field ownership. An ACK confirms project application,
not programming of the device. Missing metadata can be refreshed separately; it must not
be replaced by guessed capabilities or commercial-model names.

## Failed or corrupt storage

Stop commissioning when persistence fails. Retain diagnostic state and repair the
underlying storage/power condition before restarting. Never clear a journal, change a
key, lower a sequence floor or replay a movement as an automatic repair action.
The record model deliberately fails closed on a partial nonempty corrupt slot; a valid
older slot alone does not authorize rollback. Native cut-point tests do not model ESP32
NVS internals and cannot justify a repair that deletes the corrupt slot.

For 1W, a repaired store and restart do not establish peer synchronization. Restoring an
old backup can reuse a sequence already emitted, so blind counter backup restoration is
unsupported. Reenrollment/resynchronization must follow a procedure qualified for that
actual product. Keep transmitted/unconfirmed enrollment distinct from peer acceptance.
For 2W, restoring a different controller/key/binding state similarly requires explicit
peer/network reconciliation; no generic erase-and-repair workflow is implemented.

## Required recorded qualification

Copy `release-evidence.template.json` into a private evidence directory. Keep every unrun
scenario marked `not_run`. For a completed scenario add the exact 40-character OFM/OAM
commits, operator, date, setup, expected and observed behavior, peer identity (physical)
or ETS version, and nonempty retained artifacts with relative paths and SHA-256 hashes.

Run `python3 tools/check_release_evidence.py /path/to/evidence/record.json`.
Use `--product rgb` or `--product white` when checking a proposed product enablement.
Exit 2 means required records are incomplete; exit 1 means invalid input. The checker
verifies record completeness and artifact integrity. It cannot establish the truth of
operator observations and never enables firmware permissions. Review actual captures,
measurements and peer state against `SX1276-PEER-QUALIFICATION.md`.


## Application 3.11 additions

The eight new recognition-history values are ETS-only and excluded from device
memory/configuration transfer. Old projects start with unknown adoption history;
no past source, timestamp or authenticated receipt is synthesized. The expert
view exposes the records; the normal offline summary shows ownership and the
last two distinct records. Preserve existing parameters, names and group
addresses on upgrade. Module0.7 introduces additive combined-read capability
bit6/API32 plus explicit expert sensor subscription and opt-in volatile polling.
Legacy channel stride68, KOs600..999 and product KOs1000..1095 are unchanged.
All physical/ETS migration acceptance gates remain unrun. Optional sensor
qualification is checked with `--product sensor`; neither evidence checker nor
expert visibility enables high-FP writes or product KNX publication.
