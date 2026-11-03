<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Fixed synthetic golden vectors

## Scope and provenance

These are **synthetic offline vectors, not hardware captures**. Passing them proves
agreement with the selected byte layout, not J313 hardware interoperability, HID
report semantics, SPI transport correctness or firmware readiness. No probe, I/O,
firmware change or externally downloaded code execution was performed.

Layout source (fetched HTTP 200, untruncated):
[Asahi SPI HID core, commit 549e8b442b485c03e3e766635e2203c75362e312](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c).
Specifically: spihid_msg_hdr, spihid_transfer_packet, spihid_apple_request.
Copyright (C) The Asahi Linux Contributors. Based upstream on applespi.c:
Copyright (c) 2015-2018 Federico Lorenzi; Copyright (c) 2017-2018 Ronald Tschalär.
The upstream SPDX identifier is GPL-2.0. These added tests, fixtures and this
explanation are GPL-2.0-only; no clean-room or permissive-license claim is made.

[COPYING](<COPYING>) is the complete GPL version 2 text obtained using web_fetch
from the [GNU GPLv2 plain-text source](https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt)
(HTTP 200, untruncated), then written verbatim, without adding an SPDX line or
changing the license. SHA-256 is recorded in the evidence table below.

## Independence from the implementation under test

[golden_vectors.h](<golden_vectors.h>) contains complete, explicit literal arrays,
including every padding byte and both CRC bytes; no partial designated
initializers or fixture generation macros. They were constructed offline from
those public layouts using locally authored TypeScript and an independent
**MSB-first 0x8005 polynomial division**, reversing each input octet and the final
16-bit remainder. The implementation under test instead uses an LSB-first
0xa001 recurrence. Neither sh_request nor sh_crc16 was called to generate any
fixture. The independent calculation checks the CRC-16/ARC check value
ASCII 123456789 -> 0xbb3d (init 0, refin/refout true, xorout 0).

[test_golden.c](<test_golden.c>) never calls sh_crc16. Its independent CRC routine
only **audits already frozen bytes**; it never computes or repairs expected
packets at test time. sh_request output is compared individually at all 256 byte
positions against each of two literal packets, not merely by CRC or round trip.
sh_feed receives literal packets directly, not the request output.

Reproduction algorithm (all integer fields LE16, CRC tail LE16):

~~~text
reverse(v,n): reverse the low n bits
CRC(data):
    r = 0
    for octet in data:
        r ^= reverse(octet,8) << 8
        repeat 8 times:
            r = ((r << 1) XOR (0x8005 if r & 0x8000 else 0)) & 0xffff
    return reverse(r,16)
message(h6,payload) = h6 || LE16(len(payload)) || payload ||
                     LE16(CRC(h6 || LE16(len(payload)) || payload))
packet(flags,dev,offset,remain,data):
    b = flags || dev || LE16(offset) || LE16(remain) || LE16(len(data)) || data
    append zero octets until len(b) == 254
    return b || LE16(CRC(b))
~~~

## Frozen cases

* golden256: flags 40, device 01, offset/remain 0, packet length 17.
  Message h6 = 51 01 09 a7 34 12; payload = 00 ff 80 01 5a a5 7e.
  This deliberately synthetic request exercises payload, nonzero unknown2,
  explicit ID, asymmetric response-length bytes, zero padding, both CRC layers.
  It is not asserted to be a valid hardware command. Outer CRC = 0xc0a3.
* golden_descriptor256: flags 40, device d0, offset/remain 0, packet length 10.
  Message h6 = 20 10 02 5c 00 02; empty payload. This matches the upstream
  descriptor-request field shape with an arbitrary explicit ID 5c, response
  length 512, and target interface 2. Outer CRC = 0x62d0.
* golden_payload: 487 literal bytes, byte i = (73*i + 19) mod 256.
  golden_message h6 = 10 02 7b e3 34 12; payload length 487 (e7 01).
  Total message length 497; message CRC = 0xb590. Unknown2 and response length
  are intentionally synthetic to detect field swaps, not claimed HID semantics.
* golden_part0/1/2: flags 20, device 02. (offset,remain,length) respectively
  (0,251,246), (246,5,246), (492,0,5). Outer CRCs = c756, da12, 9a9e.
  The 5-byte tail includes the last three payload bytes and two message CRC
  bytes; this tests the parser's documented short-tail policy.

Frozen repaired-outer-CRC negative controls (zero-based packet indexes):

| Fixture | Offline mutation | Outer CRC | Expected rejection |
|---|---|---|---|
| golden_bad_message_crc | part2[12] XOR 1; recompute outer CRC only | 36cf | SH_MESSAGE_CRC |
| golden_bad_message_length | part0[14] XOR 1; recompute outer CRC only | 9797 | SH_MESSAGE_LENGTH |
| golden_bad_offset | part1[2] XOR 1; recompute outer CRC only | 59ed | SH_SEQUENCE |
| golden_bad_bounds | part0[6] = 247; recompute outer CRC only | 5ecb | SH_BOUNDS |

The independent audit proves each negative's outer CRC is valid, preventing an
outer-CRC failure from masking the intended deeper check. The message-length
negative intentionally also invalidates inner CRC: exact length must be rejected
first, matching the API's validation order.

Additionally the test flips each of 2048 bits of each of the three intact
packets (6144 trials), feeds its valid prefix first, and requires SH_PACKET_CRC.
This includes payload, header, padding and CRC bytes. It checks all 0..255 input
sizes, orphan continuation, clearing of outputs and the full receive state on
error, and recovery with a new valid sequence. These are input mutation negative
controls, not a claim of source-mutation testing or exhaustive protocol coverage.

## Reproduce host execution

From this directory (only the two listed C translation units are linked):

~~~sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  spihid_protocol.c test_golden.c -o /tmp/j313-golden-test
/tmp/j313-golden-test
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  spihid_protocol.c test_golden.c -o /tmp/j313-golden-sanitized
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/j313-golden-sanitized
~~~

Actual run: Apple clang 21.0.0 (clang-2100.1.1.101), target
arm64-apple-darwin25.5.0. Both builds and both executions succeeded; combined
shell exit code 0, stderr empty, no ASan/UBSan report. Both printed:

~~~text
fixture audit: independent CRC and frozen fragmentation PASS
sh_request: two complete frozen 256-byte comparisons PASS
sh_feed: three fixed fragments, 5-byte tail, fields/payload/length PASS
mutation negatives: 6144 bit flips, repaired-CRC inner/length/offset/bounds, truncation/recovery PASS
ALL GOLDEN TESTS PASSED
~~~

| Artifact | SHA-256 at test time |
|---|---|
| [golden_vectors.h](<golden_vectors.h>) | e3c0859328b49900fc7ca4b21378da3430213f92ab87b158621752887ac3df8f |
| [test_golden.c](<test_golden.c>) | 52458a1a4f02948cd8f3c37a0cb0749ecf1a5b18dce0516e00c8509ee885b564 |
| [COPYING](<COPYING>) | edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6 |

This work adds only these three files and this document. It does not modify
existing protocol/init implementation, tests or build files. Existing generated
property tests remain useful complementary coverage, but are not the oracle for
these fixtures. GPL compatibility of any future linked firmware remains a
separate review requirement.
