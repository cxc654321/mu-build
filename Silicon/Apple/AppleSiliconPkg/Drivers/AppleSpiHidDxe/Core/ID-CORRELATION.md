# Request ID evidence and v4 diagnostic

Reference: Asahi Linux commit `549e8b442b485c03e3e766635e2203c75362e312`, [spi-hid-apple-core.c](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c), retrieved with web_fetch (HTTP 200).

- Lines 150–157: message header has three unknown bytes, then one-byte ID. Production sh_request writes m[3], and sh_feed reads bytes[3]. No encoding/offset discrepancy was found.
- Lines 113 and 245: a single `u8 msg_id` on the device is assigned with `hdr->id = spihid->msg_id++`. Production starts at zero and postincrements one global next_id across requests. The comment at 137–138 about a separate counter per message type does not describe this actual request implementation; it is not evidence for changing production to per-interface IDs. Production intentionally refuses reuse at exhaustion instead of upstream wraparound; this cannot explain initial IFACE1.
- Lines 652–675: reply dispatch selects unknown0/type/interface without checking hdr->id; ID is printed at 716/725. This establishes a policy difference, not proof that reply IDs echo requests or that our transmitted ID is wrong.

Reported hardware: boot/device succeeded; IFACE1 rejects correlation mask 0x20 (ID only), then likely timeout -3. No actual reply ID has been observed. An actual ID of zero is a synthetic test case only, not a hardware finding. ACK D56827AC represents bytes AC 27 68 D5; ACK success does not establish message-ID semantics.

This change retains strict ID, CRC, framing, type, interface, response length, payload size and descriptor checks. No keyboard publication path is relaxed. Snapshot version 4 appends rejection ID validity, expected ID, actual ID, request type and request interface. These are captured together at the rejected message, retained through timeout, invalidated by other rejection classes, and cleared on accepted reply/reset. The driver freezes them with the first fatal diagnostic before teardown. Old versions/sizes are UNKNOWN rather than reinterpreted.

The seven-line summary retains its line count. Its counter line is `HID p/r/rx/tx/d=... ID e/a=01/00 t/i=02/01` (hex); p=polls, r=ready, d=drops, e=expected, a=actual. `ID=n/a` means no valid rejected correlation snapshot. The type is pending unknown1; the interface is pending unknown2. Example IDs above are synthetic.

Next hardware run should capture this line plus phase/domain/error/rejection mask. A future explicitly reviewed compatibility policy could correlate by validated type/interface and remaining framing/length/descriptor gates, like upstream, but must address stale replies/restarts and be approved on the main line. No compatibility mode is enabled here, and no hardware success is claimed.

Validation: `python3 -m unittest Tests.test_spihid_safe_entry Tests.test_j313_snapshot_summary -v` passes five tests, covering AArch64 freestanding production compilation, ASan/UBSan session/entry/summary tests, synthetic boot/device ID0 then IFACE1 expected1/actual0 rejection 0x20, timeout retention, valid reply recovery clearing diagnostics before IFACE2, immutable first-fatal driver snapshots, v3 rejection and seven summary lines.
