# Preserve daylight logs without assigning sensor units

[Logs6/7 and the tester's readings](https://github.com/laberning/home_io_control/issues/156#issuecomment-6629018375)
were downloaded byte-for-byte and hashed. The timelines retain separate log
clock/uptime, object selector/ID, open status/handle/length, reported block
attempts, complete/partial dumps and external reference observations. Logs4/5
were regenerated with the same parser; their original source hashes are intact.

Both new8005 objects are still06 followed by fifteen zeros. Both8103 dumps are
complete67-byte observations and differ at offsets42,45,48. No earlier/later
partial data was concatenated. Complete class2 records are metadata-only:
log7 has A613431/431 bytes; log6 A615 is0/341 incomplete. This does not establish
that any unknown A6xx body is safe to publish.

The independent sensor faces south and the motor/solar assembly west. Reported
2818/4675/1388/992→702/682lx values are external reference metadata, not calibrated
motor values. Byte differences do not establish lux, watts, SOC or a schema.

The logs report subscribe73 followed by2005 replies; the original packet
observations are retained separately from application summaries. Redacted
proof material and absent unsubscribe/reboot evidence do not establish motor
persistence or a complete authenticated subscription lifecycle. A missing reply
remains unconfirmed rather than a verified refusal.

No literal4A/4B packet bodies occur in these two files: continuation attempts
are message-level records. They cannot be promoted to a captured block stream,
chunk offsets or close exchange. The private originals retain everything;
public fixtures replay only actual reviewed packets and complete safe dumps.
New tests verify exact objects, byte differences, selector-aware confidentiality,
header reproduction and the distinction between external and motor measurements.
