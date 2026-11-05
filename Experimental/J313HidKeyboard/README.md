# Experimental J313 HID keyboard: host-only restricted parser

This is an independent, MIT-licensed C11 implementation based on public USB-IF
specifications, not a port of GPL source. No third-party parser code was copied.
Semantic sources: [HID 1.11](https://www.usb.org/document-library/device-class-definition-hid-111)
(short items, Main/Global/Local items, report format; sections 6.2.2 and 8),
[HID Usage Tables](https://www.usb.org/document-library/hid-usage-tables-15)
(Keyboard/Keypad page 0x07, including error selectors and modifiers).
The specs are references, not files relicensed by this directory.

**The 182-byte [observed descriptor](<observed_descriptor.h>) comes from a read-only
macOS IOService registry query of the internal SPI keyboard, not a synthetic
descriptor. See [acquisition provenance](<fixtures/SOURCE.md>) and
[allowlisted source metadata](<fixtures/source.json>). SHA-256:
`5ad48fbaddbae4d5806c4dbc27c842e535e2954cd140e208494cf4f17fbc47c7`.
All input reports used here are SYNTHETIC: no actual key/input reports were
captured. Tests perform no hardware communication and are not connected to
firmware, UEFI, OS input, transport, or MMIO. No hardware validation or keyboard
functionality is claimed.**

The original restricted parser returned `HK_UNSUPPORTED` for this descriptor.
The expanded parser tracks these independent payload lengths (ID byte excluded):

| Report ID | Input bits | Output bits | Feature bits | Input bytes including ID |
| --- | ---: | ---: | ---: | ---: |
| 0x01 | 72 | 8 | 0 | 10 |
| 0x52 | 8 | 0 | 0 | 2 |
| 0x3f | 512 | 0 | 0 | 65 |
| 0x09 | 0 | 0 | 24 | none (`HK_UNKNOWN_REPORT` on decode) |

## Supported deliberately narrow subset

- Sequential top-level Application collections, no nesting; at least one keyboard
  Input field required overall. Non-keyboard pages are accepted for length accounting.
- Short items only. Global Usage Page (7 for emitted keys), unsigned
  nonnegative Logical Minimum/Maximum, Report Size/Count and Report ID.
- Local Usage lists OR one Usage Minimum/Maximum range; not mixed. Local state
  resets after every Main item. Page changes with pending local usages rejected.
- Input flags exactly 0 (Data/Array/Absolute), 2 (Data/Variable/Absolute), or
  1 (Constant padding); 0x22 is additionally allowed for ignored non-keyboard Input.
  Constants and non-keyboard Input consume offsets but not keys; payload padding
  bits, including final unused byte bits, are ignored rather than assumed zero.
- Array selectors index the explicit ordered usage table relative to Logical
  Minimum; selector values are NOT assumed to equal usage codes. Table length
  must equal the entire logical range. Usage 0 is no key, duplicate keys coalesce.
  Keyboard usage tables cover 00..FF; reserved/non-key E8..FF produce no events.
- Boolean variable fields (logical 0..1), including NKRO; exactly one local usage
  per element, usage 4..FF (only 4..E7 emitted). Size may be 1..16 bits;
  out-of-range keyboard values rejected.
- Keyboard usages E0..E7 contribute both ordinary press/release events and a
  modifier bitmask (bit 0 E0 through bit 7 E7). No layout, Unicode, lock state,
  text mapping, or repeat synthesis.
- No fixed 8-byte assumption: little-endian bit extraction crosses byte boundaries.
  Payload length is derived from all Input fields including constants and ignored
  consumer/vendor fields. Output/Feature lengths are independent and never move
  Input offsets or create keys; this API does not transmit or decode those streams.
- Unnumbered reports OR nonzero numbered reports; never mixed. Multiple IDs may
  be interleaved/revisited in the descriptor. Each ID accumulates its own offset.
  Decoder keeps state per ID and outputs the union, so one report cannot release
  a key still held by another. Unknown IDs rejected; ID byte is outside payload.
- Events are sorted by usage, not time or releases-first. Repeated unchanged
  reports emit no events. At most 228 changes are possible in the supported range.

## Fail-closed limits / unsupported constructs

Maximum descriptor 4096 bytes, 8 report IDs, 64 data fields per ID, 4096 payload
bits per ID per stream, 1..16 bits per element, 256 keyboard data elements per
field, and 256 local usages. Non-keyboard/constant count is bounded at 4096 and
by total payload bits. Keyboard usages are limited to 00..FF; other pages allow
16-bit usages but are only length-accounted, not decoded or semantically validated
as consumer/vendor controls.
Limit violations reject the descriptor; there is no partial acceptance.

Nested collections, keyboard usages above FF, long/reserved items, signed
logical ranges, physical bounds, units/exponents,
Push/Pop, delimiter/designator/string locals, extended 32-bit usages, multiple or
mixed usage ranges, implicit repeated variable usages, relative/null-state/
wrap/buffered flags (and nonlinear except ignored non-keyboard Input 0x22),
and all other unlisted items are **rejected**, not
skipped. Even otherwise legal descriptors outside this subset will not work.
Boot-style fixture means an input-only layout, not full boot-protocol support.

## API and error policy

Use hk_parse once, keep its descriptor immutable, hk_state_init, then hk_decode
for each complete report. Core only uses standard integer/size types and
memset/memcpy; no heap, I/O, OS, UEFI or MMIO. Host tests use libc assertions/I/O.
The descriptor is large (fixed field tables); allocate static or caller-managed
storage, not on a small firmware stack. Public structs are trusted internal
objects, not serialized inputs; do not mutate them or share state across
incompatible descriptors. Caller supplies valid nonoverlapping buffers and
serializes access to a state object.

Parse failures zero the output. Decode requires exact report size: short input
is HK_TRUNCATED, extra bytes HK_INVALID. All decode failures leave state unchanged
and clear result (zero modifier field on failure is NOT a new modifier state).
HK_UNKNOWN_REPORT handles absent IDs; HK_INVALID also covers illegal selectors.
Array usages 1/2/3 (ErrorRollOver/POSTFail/ErrorUndefined), anywhere in an array,
return HK_ROLLOVER and preserve the **entire previous report state**, including
modifiers. They never become ordinary keys or spurious releases. Next valid
report reconciles against the preserved state. Callers must explicitly handle
transport loss/disconnection and reset/release policy; no timeout is guessed.

## Host verification

Run from this directory:

~~~sh
make test
make sanitize
make fuzz-smoke
# Requires an LLVM toolchain with libFuzzer runtime:
make fuzz CC=/path/to/llvm/bin/clang
~~~

Tests cover input-only boot layout, duplicate suppression, press/release,
modifier masks, all three array error codes with transactional preservation,
nonboot ID 5 with 3 padding bits + two 9-bit mapped selectors + modifiers,
ID 9 overlapping state, non-byte-aligned end, full 228-key NKRO, every fixture
prefix, unknown ID, truncated/extra reports, unsupported items, page ambiguity,
field/ID/bit/descriptor upper bounds, and mixed ID mode rejection. The observed
descriptor adds synthetic Shift+key/release, duplicate suppression, every array
error in every slot, all report truncations, overlong reports, E8..FF selectors,
consumer/vendor reports preserving held keyboard state, and feature-only ID
rejection. A mixed-stream synthetic descriptor checks that ignored fields count
toward Input length and Output/Feature do not pollute Input offsets.

The old suite still passed before these updates: its other-page rejection removes
ALL keyboard fields, while its injected Output/Feature items lack local usages.
Those malformed/no-keyboard rejection cases are retained with clarified comments;
they are not evidence that legal non-keyboard fields or Output/Feature are banned.

fuzz-smoke uses fixed seed 0x313: 20,000 random/mutated descriptor/report cases,
plus pristine seeds, including the observed descriptor. Its mutation seed pool
and the shared libFuzzer entry both include that descriptor; reports are also
correctly framed and tested at every shorter payload for each fixture.
The shared LLVMFuzzerTestOneInput entry exercises arbitrary descriptors, a
length-prefixed descriptor/report split, and reports against known fixtures.
Assertions check atomic failures, event bounds/order/range and stable repeats.
This is bounded testing, not a proof of correctness or exhaustive HID coverage.

Local result: strict clang C11 (-Wall -Wextra -Werror -pedantic), host tests,
ASan+UBSan host tests and ASan+UBSan deterministic fuzz smoke passed.
Coverage-guided make fuzz could NOT link with this host Apple clang: missing
libclang_rt.fuzzer_osx.a. Its 100,000-run target is provided but **not claimed run**.
Build artifacts stay under this directory's ignored build/ directory.
