# J313 firmware core port

Experimental source directories are unchanged. Production copies retain original SPDX
headers and attribution. spihid_protocol/sh_init and PIO are GPL-2.0-only adaptations;
COPYING contains GPLv2. Their pinned upstream attribution is preserved in
J313SpiHidProtocol-PROVENANCE.md, INIT.md and J313SpiPio-PROVENANCE.md (relative
experimental documentation links therein refer to the original Experimental tree).
The independently implemented hid_keyboard/key_queue remain MIT; LICENSE retains
J313HidKeyboard contributors' copyright and full permission notice. Integration
j313_session and driver code are GPL-2.0-only; this is not a clean-room relicensing.
Firmware redistribution still requires license compatibility review.

Only changes to copied algorithms are local include paths and replacing standard
headers with J313CoreCompat.h. Firmware uses EDK2 Base.h integer types/UINTN and
BaseMemoryLib CopyMem/SetMem/CompareMem. J313_HOST_TEST enables standard C headers
only for host tests. No host allocator or libc dependency is required by the core.

j313_session is production logic: bounded correlated sh_init requests, sh_feed CRC
and fragmentation, live interface-1 descriptor hk_parse, hk_decode. Caller owns
transport/clock/serialization and submits successful hk_result once to adapter's
key queue. Context includes a large immutable parsed descriptor and MUST be pool
allocated, never placed on the firmware stack. No observed fixture is embedded.
Malformed handshake fails closed without restart; malformed runtime packets stop
after eight errors, valid key decode resets the consecutive-error count; fragmented
message timeout is 100ms. Handshake phase limit is 1s with driver total 6s deadline
and independent 1200 timer wake budget. Timer work is one RX and at most one TX,
not an unbounded drain loop. Publish and console binding happen at APPLICATION.

Tests: python3 -m unittest Tests.test_spihid_safe_entry -v (repository root).
This runs actual session code with synthetic wire packets and actual entry code
with mocked UEFI/transport/session, plus all production sources compiled against
real EDK2 headers for freestanding AArch64. Host cases are ASan/UBSan enabled.
No test runs real MMIO. Passing does not establish hardware interoperability.
