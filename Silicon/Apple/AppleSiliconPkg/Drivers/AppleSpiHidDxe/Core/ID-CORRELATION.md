# Request ID evidence and production correlation policy

Reference: Asahi Linux commit `549e8b442b485c03e3e766635e2203c75362e312`, [spi-hid-apple-core.c](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c).

- Lines 150–157: message header has three unknown bytes, then one-byte ID. Production sh_request writes m[3], and sh_feed reads bytes[3].
- Lines 113 and 245: one device-global `u8 msg_id` is assigned with `hdr->id = spihid->msg_id++`. Production retains its global postincrement and existing exhaustion guard; neither fixed-zero nor per-interface transmit IDs are introduced.
- Lines 652–675: reply dispatch selects unknown0/type/interface without checking hdr->id; ID is printed at 716/725.

Reported hardware before this change: boot/device progressed; IFACE1 expected ID 01, actual ID 00, pending type/interface 02/01, rejection mask 0x20 only, then init timeout. This proves the ID equality gate blocked that reply, not that its payload structure or HID descriptors were valid: those checks had not yet run. ACK D56827AC represents bytes AC 27 68 D5 and does not establish ID semantics. No post-change hardware success is claimed.

## Current production policy

With explicit approval, production removes only reply-ID equality, following upstream dispatch. The single pending type/interface, flags, device, unknown0, response length, payload length, both transport CRCs, framing/reassembly, device/interface structure, HID descriptor validation, stage deadline and event/request budgets remain unchanged. Rejection bit 0x20 is reserved and no longer emitted. A reply with any ID may advance only after all remaining init checks; publishing still requires the real HID descriptor validator.

Accepted ID differences produce firmware DEBUG_INFO `SPI-HID init accepted ID difference tx/rx=01/00 t/i=02/01`. This is an observation after init structural checks, not a rejection or a claim of HID validation/publication. At most five replies can advance per init attempt. No snapshot ABI or GUI changes are needed. Existing v4 rejection ID fields retain their names/layout: they capture TX/RX IDs as context only for an actual header/length rejection (e.g. wrong interface), clear on acceptance, and freeze on first fatal error. `ID e/a` in the existing summary is historical field naming, not an equality requirement; `ID=n/a` means no valid rejected-correlation context.

Within the no-retry five-request chain each pending type/interface pair is distinct, so duplicate previous-phase replies cannot advance. IDs are not an epoch: same-type/interface stale replies across restart/reset cannot be distinguished by this policy. Callers must quiesce transport and drain stale RX/reassembly before a new attempt. Preserving global TX IDs does not itself prevent stale reply acceptance. Historical Experimental implementation/tests are unchanged and do not define production policy.

## Validation

`python3 -m unittest Tests.test_spihid_safe_entry Tests.test_j313_snapshot_summary -v` passes five tests: AArch64 freestanding compilation, ASan/UBSan production session and entry, and summary tests. Production session covers five differing reply IDs with unchanged TX IDs 0..4 through real HID validation/publication; invalid descriptor still fails without publication; wrong type/interface, zero length and CRC fail or reject; duplicate previous phase cannot advance or refresh its deadline; event budget and first-fatal diagnostic lifetime remain enforced. The IFACE1 actual-zero case now advances when its structure is valid; invalid structure remains rejected. These are synthetic host tests, not hardware validation.
