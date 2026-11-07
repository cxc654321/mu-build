<!-- SPDX-License-Identifier: GPL-2.0-only -->
# J313 input pipeline: host-only integration replay

This is an offline integration regression test, **not a working J313 keyboard
or a firmware driver**. Only this integration-test directory is changed. No UEFI, MMIO,
SPI I/O, HID device opening, deployment, firmware linking, or push is involved.

## Data provenance and selector policy

All SPI packets, fragmentation patterns, message headers and HID input reports
in [test_pipeline.c](<test_pipeline.c>) are **synthetic**, not hardware captures.
The actual 182-byte descriptor is included directly from the existing
[observed descriptor](<../J313HidKeyboard/observed_descriptor.h>), originally
obtained by a read-only macOS IOService registry query of the internal SPI
keyboard. See [acquisition evidence](<../J313HidKeyboard/fixtures/SOURCE.md>)
and [metadata](<../J313HidKeyboard/fixtures/source.json>).
Descriptor SHA-256:

5ad48fbaddbae4d5806c4dbc27c842e535e2954cd140e208494cf4f17fbc47c7

The upper-layer input selector is an **explicit policy assumption**, adopted
from the public [Asahi SPI HID core at fixed commit
549e8b442b485c03e3e766635e2203c75362e312](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c).
The full source was retrieved and checked (HTTP 200, not truncated):

- SPIHID_DEVICE_ID_KBD is 1, TP is 2, READ_PACKET is 0x20.
- spihid_process_message dispatches 0x20 to spihid_process_input_report;
  0x40 goes to response handling instead.
- spihid_process_input_report rejects unknown0 != 0x10; for an available,
  ready keyboard interface it sends the **unchanged payload and length** to
  hid_input_report. This test replaces interface readiness with successful
  parsing of the observed descriptor and initialized host state.
- That keyboard branch does not require unknown1 == 1, unknown2 == 0,
  or rsplen == 0. We do not invent those conditions or equate unknown1 to
  the HID report ID. The synthetic headers deliberately use unknown1=0xa7,
  unknown2=0x6d, id=0xe3, rsplen=0x1234 to expose accidental dependencies.
  This is a routing-policy test, **not evidence those values occur on hardware**.
- Unknown header fields keep their existing names; no complete semantics are
  claimed. The report ID comes from payload[0], not the message header.

## Interface contract

The adapter is a static test-local function, not a new firmware library/API:

1. Parse the immutable observed descriptor with hk_parse, initialize hk_state
   and sh_rx once, and initialize the real kq_queue with kq_init. The large
   descriptor has static storage.
2. feed accepts one complete 256-byte packet per call, using existing sh_feed
   for outer CRC, continuous reassembly, exact message length and inner CRC.
3. Decode only SH_MESSAGE + flags=0x20 + device=1 + unknown0=0x10 + nonempty
   payload. Partial/error/device2/response/non-input messages do not call
   hk_decode. CRC integrity is not cryptographic authenticity.
4. Pass payload and exact length, including the HID report ID, synchronously to
   hk_decode. The borrowed payload expires at the next feed/reset and is not
   retained. All input/context/output buffers must be valid, nonoverlapping;
   one stream and externally serialized calls only.
5. Report 1 is **10 bytes total**: ID, modifier, constant byte, six array key
   selectors, and one consumer/vendor byte. It is not an 8-byte boot report.
   ID 0x52 has 2 bytes, vendor ID 0x3f has 65 bytes; feature-only ID 9 is not
   keyboard Input. Lengths and keyboard fields come from the descriptor.
6. outcome.transport is always meaningful. outcome.hid is meaningful only
   when outcome.decoded is true. Emit events only for a decoded HK_OK result;
   zero modifiers on an error/ignored result do not mean a release. Decoder
   failures leave the previous complete keyboard state unchanged. Each HK_OK
   result is fed exactly once into the existing kq_feed; outcome.queue_fed
   indicates whether outcome.queued is meaningful (KQ_OK or KQ_OVERFLOW).
   Transport/routing/HID failures never feed or mutate the queue.
7. Consumer/vendor input contributes no keyboard events and must not erase
   keys held by report 1. An actual valid all-up report 1 does release keys.
   A full keyboard report with unchanged keys and changed ignored trailing
   consumer/vendor bits must not release keys either.

## Build and executed checks

Run in this directory:

~~~sh
make test
make sanitize
# or both:
make all
~~~

[Makefile](<Makefile>) links the existing protocol, keyboard and
[key queue](<../J313KeyQueue/key_queue.c>) C sources only
into local host executables. Assertions are mandatory (NDEBUG is rejected).
Temporary executables live here and are removed on command exit; -g0 avoids
macOS dSYM side files. No sibling Makefile or library is modified.

Actually executed with Apple clang 21.0.0 (clang-2100.1.1.101),
arm64-apple-darwin25.5.0: normal -O2 and -O1 AddressSanitizer +
UndefinedBehaviorSanitizer, both exit 0; no sanitizer diagnostics. Both print:

~~~text
PASS filters, CRC/length/HID errors, consumer/vendor isolation, release
PASS every two-fragment split, bytewise reassembly, corruption recovery
PASS queued characters/modifiers, four arrow scans, FIFO overflow and recovery
ALL PIPELINE TESTS PASSED (132 synchronous HID decodes)
~~~

Coverage includes A + left Shift press/release, repeated unchanged report,
packet CRC failure, valid outer CRC with invalid inner CRC, invalid declared
message length, short packet, device 2, 0x40 response, other unknown0 selector,
empty message, report 1 lengths 1..9/11, all three HID error selectors 1/2/3,
unknown ID, feature-only ID, consumer/vendor isolation while keys remain held,
and changed ignored bits in a full report 1. State snapshots are compared in
full, and no-event results are asserted. Every two-piece split of the 20-byte
message (including inside the header, report and CRC), twenty one-byte
fragments, interrupted reassembly, orphan continuation rejection and recovery
on a fresh valid release are replayed through the actual linked C libraries.
The CRC fixture generator uses the existing CRC primitive: this is integration
coverage, not an independent CRC oracle or new wire-format discovery.

The real queue is exercised solely via sh_feed -> hk_decode -> kq_feed, never
with hand-built hk_result batches. Every quiet path compares a byte-for-byte
queue snapshot, including buffered keys, counters and modifier state: outer/inner
CRC errors, filters, HID failures, repeated reports and consumer/vendor reports
cannot alter it. Shift+A remains buffered throughout those checks; after release,
kq_pop returns Unicode A, scan 0 and press-time left Shift (valid bit + 2),
while current queue modifiers are cleared. Fragmented messages yield exactly
twenty lowercase a keys. All four arrows return Unicode 0 and scan codes
Right=3, Left=4, Down=2, Up=1, with valid unshifted modifiers/toggle state.

Overflow is observed through the adapter return status and dropped counter:
32 alternating a/b presses fill the FIFO; a new Shift+C yields KQ_OVERFLOW,
count remains 32 and dropped becomes 1. Repeated held input and release do not
add keys or increment drops, even while full. Draining verifies all original
a/b keys in order (drop-newest), and a fresh Shift+C after draining succeeds
without erasing the loss counter. Empty pops preserve their output argument.

## Licensing boundary

This integration C test, Makefile and README are **GPL-2.0-only**. The host
combined executable contains the GPL protocol implementation; this directory
is not advertised as MIT/clean-room. GPL text is available in the existing
[protocol COPYING](<../J313SpiHidProtocol/COPYING>).
Protocol provenance/attribution is preserved in its unmodified sources:
Copyright (C) The Asahi Linux Contributors; based upstream on applespi.c,
Copyright (c) 2015-2018 Federico Lorenzi and Copyright (c) 2017-2018 Ronald
Tschalär. See [protocol provenance](<../J313SpiHidProtocol/README.md>).

The unmodified keyboard sublibrary remains MIT, Copyright (c) 2026
J313HidKeyboard contributors. Its complete copyright, permission and warranty
notice remains in [MIT LICENSE](<../J313HidKeyboard/LICENSE>), and must accompany
redistribution of its copies/substantial portions, including in the combined
host distribution. It is not relicensed by this README. The descriptor is
observed device-description data, not an imported implementation.

The unmodified key queue sources retain their MIT SPDX notices; this GPL host
test does not relicense that independent module.

Only host linking occurs. No firmware module/build graph or firmware license
is changed. Any future firmware integration/distribution requires separate
license-compatibility review and authorization; this test gives no clearance.

## Remaining hardware / UEFI gaps

No real SPI/HID input capture or end-to-end device validation exists here.
The descriptor is preloaded from the observed fixture: it is not fetched by
sh_init during this test. Boot notifications, TX status, power/reset, GPIO/IRQ,
SPI timing/PIO/DMA, initialization correlation, transport timeout/cancellation
and recovery are not integrated. A future transport must call sh_reset on
bus failure/timeout/cancel; policy for stuck held keys/disconnect remains a
separate decision, not an invented release on malformed data.

The existing host key queue supplies limited US-layout Unicode/scan translation
and a bounded FIFO here, not a UEFI protocol or hardware driver. No UEFI
SimpleTextIn/Ex installation, repeat, hardware lock/LED output, concurrency
lifecycle or touchpad support is supplied. No secure-boot, deployment or hardware functionality claim is
made. The safe result is an offline, reproducible integration test only.
