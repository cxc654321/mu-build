# J313KeyQueue — experimental host-only key translation

This is a standalone C11 **pure logic core**, not a UEFI driver. It converts
successful HID decoder transition batches to a bounded FIFO of UEFI-style
scan codes, UTF-16 code units and modifier/toggle snapshots. It registers no
input protocol, creates no fake input device, connects to no hardware and is
not included in any firmware build.

## Contracts and sources

Read against the existing [HID interface](<../J313HidKeyboard/hid_keyboard.h>)
and [decoder](<../J313HidKeyboard/hid_keyboard.c>), and this tree's
[SimpleTextIn definitions](<../../MU_BASECORE/MdePkg/Include/Protocol/SimpleTextIn.h>)
and [SimpleTextInEx definitions](<../../MU_BASECORE/MdePkg/Include/Protocol/SimpleTextInEx.h>).
The core includes only the HID interface and standard C headers; numeric output
values match those UEFI definitions. Its structs are **not** ABI-compatible
EFI_KEY_DATA objects and must be copied field-by-field by a future adapter.

API: initialize a caller-owned kq_queue with kq_init(), pass each HK_OK result
exactly once to kq_feed(), then drain with kq_pop(). Do not feed failed decodes:
those clear hk_result and do not represent a new modifier snapshot. An empty
successful result is meaningful and refreshes current modifiers. The decoder
already suppresses held-key repeats. This core does not independently deduplicate
successive batches or generate typematic repeats. Do not replay an overflowed
batch; some keys may already have been accepted and locks have been updated.

Only pressed=1 translated key events enter the FIFO; releases, modifier-only
events, lock keys and unsupported usages do not. Invalid count, out-of-range
usage, non-boolean pressed or duplicate usage rejects the whole batch before
state mutation. This is validation of the batch, not of arbitrary corrupted
queue memory. Initialize before use, do not mutate the public state, and
serialize all calls externally (no locks, atomics, interrupts or TPL handling).

## Translation policy

- US A–Z, number row, space and US punctuation. Letters use Shift XOR Caps;
  digits/punctuation use Shift alone. Both left and right Shift work.
- Enter = UTF-16 CR (13), Tab = 9, Backspace = 8, each with scan 0.
- Esc = scan 0x17, arrows/Home/End/PageUp/PageDown/Delete/Insert use UEFI scans;
  F1–F12 use 0x0B–0x16. These have UnicodeChar=0 (no terminal escape strings).
- HID modifier byte is the authoritative **post-report aggregate snapshot**.
  All eight Ctrl/Shift/Alt/GUI bits map to UEFI left/right modifier bits, with
  SHIFT_STATE_VALID set. Individual modifier event order is irrelevant.
- Ctrl/Alt/GUI preserve the normally translated US character and are exposed
  as state, not synthesized C0 characters, ESC prefixes, shortcuts, dead keys,
  AltGr or command actions. Thus Ctrl+A is 'a' plus left/right Ctrl state;
  Shift+Ctrl+A is 'A' plus Shift/Ctrl. A consumer dropping state loses this
  distinction; legacy SimpleTextInput control-character policy belongs in a
  future adapter. Physical Shift bits are retained even for uppercase output;
  any SimpleTextInputEx protocol-specific normalization belongs there too.
- Caps/Num/Scroll Lock toggle on press only, start off and produce no key.
  TOGGLE_STATE_VALID is set; KEY_STATE_EXPOSED is not. Num/Scroll are software
  state only (no keypad mapping, scrolling or LED output).
- Each hk_result is treated as simultaneous: update all locks first, then
  translate keys in supplied event order. The existing decoder orders events
  by usage, not physical time, so Caps+A in one report uses the new Caps state
  even though A sorts first. A queued key retains its original state snapshot.

## Bounded behavior

Capacity is 32 keys, with no allocation. FIFO full means **drop newest**,
keep earlier entries, return KQ_OVERFLOW for that feed and increment public
'dropped' once per rejected translated key (saturates at UINT64_MAX). Lock and
current modifier state still advance on overflow. Unsupported keys do not
count as dropped. 'count' exposes occupancy. kq_pop returns KQ_EMPTY without
changing its output when empty; NULL arguments return KQ_INVALID (kq_init(NULL)
is a no-op). kq_init resets queue, locks and counter; coordinate reset with the
upstream decoder/session so held keys are not unexpectedly lost.

## Host build and actual validation

From this directory:

~~~sh
make test
make sanitize
~~~

Both were actually compiled and executed on the macOS host, returning exit 0
and printing:

~~~text
J313KeyQueue: mapping, modifiers, batches, bounds, FIFO and HID integration PASS
~~~

Build uses C11 and -Wall -Wextra -Wpedantic -Werror. sanitize uses both
AddressSanitizer and UndefinedBehaviorSanitizer with halt_on_error=1; no sanitizer
diagnostics were produced. The test links the existing decoder without changing
it and uses a synthetic boot descriptor/report, **not a J313 hardware capture**.
Coverage includes every supported character/navigation/function mapping,
Shift/Caps combinations, all 256 modifier masks, press/release and lock handling,
invalid atomic rejection, empty/null behavior, repeated FIFO fill/drain,
wraparound, partial-batch overflow, saturating loss count, reset, and actual
hk_parse -> hk_decode -> kq_feed integration with unchanged held reports.
Build artifacts stay under ignored build/; make clean removes the two binaries.

## Deliberate limits / future real adapter

No non-US layouts, non-US # key, keypad, media keys, IME, compose, Unicode beyond
ASCII, autorepeat, LED synchronization, multi-device arbitration or persistence.
No firmware compile/boot or hardware testing is claimed. Future real transport
and driver work must supply ownership/lifetime, valid-report gating, concurrency,
reset/disconnect semantics, EFI_STATUS conversion, EFI_KEY_DATA field mapping,
ReadKeyStroke(Ex), WaitForKey signaling, SetState/LED policy, notification behavior,
and appropriate protocol installation only for real supported hardware.
Passing these host tests is **not completion of SimpleTextInput(Ex)**.
