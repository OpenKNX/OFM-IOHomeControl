# Garage stored partial opening (P2-09)

`iohc garage stored-partial CHANNEL` is an explicit local-console physical-motion test. It requires an assigned 2W actuator with complete matching profile 5/subprofile 0 or 58 metadata. It queues exactly one six-byte EXECUTE body `01 63 D8 09 00 00`, retaining the captured ACEI separately from the stored selector. The queued operation is invalidated by a changed binding, key or product revision.

D809 selects the opening already stored in the drive. It is not 50%, and this command cannot program a stored position. It has one attempt because replay of a special selector can stop or toggle a movement. No KNX object or automatic movement is added.

Source: [MOTO1000 upstream field report](https://github.com/laberning/home_io_control/issues/163#issuecomment-6795202392). The supplied bytes are an application request, not a new OFM RF capture. Host tests verify exact bytes, profile rejection, unsafe-repeat classification and key replacement before transmission. OFM hardware qualification is NOT RUN: use a supervised clear travel area, confirm the previously stored opening and original remote operation, capture authenticated responses and actual travel. Do not infer support for every profile-5 motor from one MOTO1000 report.
