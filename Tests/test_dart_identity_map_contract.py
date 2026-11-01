# Copyright (c) 2026 Aurora Silicon

"""Host-side contract tests for the DART identity-map fallback.

T8142's usb DART cannot bypass. Measured on J813 over the m1n1 proxy by
writing 0xFFFFFFFF to a TCR and reading back the writable bits:

    usb0 reg[0] TCR writable mask = 0x00000F89
    usb0 reg[1] TCR writable mask = 0x00000F89

TRANSLATE_ENABLE (bit 0), FOUR_LEVEL (bit 3), REMAP_EN (bit 7) and REMAP
(bits 11:8) all take writes. BYPASS_DART (bit 1) and BYPASS_DAPF (bit 2) are
hardwired to zero -- with translation already disabled, with every stream
disabled, after writing UNPROTECT, and written singly. PARAMS2 nonetheless
sets BYPASS_SUPPORT, so the capability bit is not evidence of anything.

The fallback is a four-level identity map, DVA == PA, which keeps the
controller transparent to an OS that has never heard of a DART. Three levels
would reach only 64GB of DVA and DRAM on this part begins at 0x100_0000_0000,
so the fourth level is load-bearing rather than an optimisation.

None of that is visible at build time. A DART that is left translating
nothing looks exactly like a configured one from every register, and the
first evidence is a device that never transfers, so the properties that keep
this failing closed are pinned here instead.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
DART_DRIVER = ROOT / (
    "Silicon/Apple/AppleSiliconPkg/Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.c"
)
DART_HEADER = ROOT / "Silicon/Apple/AppleSiliconPkg/Include/Drivers/AppleDartIoMmuDxe.h"
# AsahiLinux is the authority for the register and PTE layout. Parsed rather
# than transcribed so a rebase that changes either cannot drift silently past
# this tree.
LINUX_DART = ROOT.parent / "linux" / "drivers" / "iommu" / "apple-dart.c"
LINUX_PGTABLE = ROOT.parent / "linux" / "drivers" / "iommu" / "io-pgtable-dart.c"


def function_body(source: str, name: str) -> str:
    """Return a C function body, balancing braces instead of relying on regex."""
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"function {name} not found")

    start = source.index("{", match.start())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : index]
    raise AssertionError(f"unterminated function {name}")


class DartIdentityMapContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.driver = DART_DRIVER.read_text(encoding="utf-8")
        cls.header = DART_HEADER.read_text(encoding="utf-8")

    def test_bypass_is_probed_not_believed(self) -> None:
        # PARAMS2 claims bypass on hardware that refuses it. The register has
        # to be asked, and asked non-destructively -- this runs against a DART
        # that iBoot may already have configured.
        probe = function_body(self.driver, "AppleDartProbeBypass")
        self.assertIn("Original = MmioRead32(TcrAddress)", probe)
        self.assertIn("MmioWrite32(TcrAddress, Dart->TcrBypass)", probe)
        self.assertIn("MmioWrite32(TcrAddress, Original)", probe)
        # The restore must happen before the verdict is computed, or a probe
        # that returns early leaves the DART holding the probe value.
        self.assertLess(
            probe.index("MmioWrite32(TcrAddress, Original)"),
            probe.index("return"),
        )

        initialize = function_body(self.driver, "AppleDartIoMmuDxeInitialize")
        self.assertIn("AppleDartProbeBypass(&DartInfo[DartIndex])", initialize)

    def test_identity_map_refuses_to_be_empty(self) -> None:
        # Enabling translation against a map that covers nothing blocks every
        # transfer while reading back as a healthy DART. It must fail closed.
        build = function_body(self.driver, "AppleDartBuildIdentityMap")
        self.assertIn("DramBase == 0 || DramSize == 0", build)
        guard = build.index("DramBase == 0 || DramSize == 0")
        self.assertLess(guard, build.index("AppleDartAllocTable"))

    def test_tables_are_reserved_memory(self) -> None:
        # The OS must not be able to reclaim a live page table. If it does,
        # the DART keeps walking the page while a driver writes its own data
        # into it, and device DMA lands wherever the recycled contents decode.
        pool = function_body(self.driver, "AppleDartReserveTablePool")
        self.assertIn("EfiReservedMemoryType", pool)
        self.assertIn("ALIGN_VALUE", pool)

        alloc = function_body(self.driver, "AppleDartAllocTable")
        self.assertIn("ZeroMem", alloc)

    def test_no_page_is_ever_freed(self) -> None:
        # AllocateAlignedReservedPages over-allocates and returns the slack
        # with FreePages. Under this platform's memory protection policy,
        # freeing part of an EfiReservedMemoryType allocation returns
        # EFI_INVALID_PARAMETER, which the library turns into ASSERT_EFI_ERROR
        # -- a DEBUG-build deadloop in the middle of DXE. Measured on J813 as
        # "ASSERT [AppleDartIoMmuDxe] MemoryAllocationLib.c(222)". Aligning by
        # hand and wasting the head is the whole fix, so nothing on this path
        # may free anything.
        for name in ("AppleDartReserveTablePool", "AppleDartAllocTable",
                     "AppleDartBuildIdentityMap"):
            with self.subTest(function=name):
                body = function_body(self.driver, name)
                self.assertNotIn("FreePages", body)
                self.assertNotIn("AllocateAligned", body)

    def test_the_table_pool_is_sized_not_guessed(self) -> None:
        # An undersized pool would truncate the map: transparent for low
        # addresses and blocking above the cutoff, which is exactly the
        # partial-transparency failure the width checks exist to prevent.
        pool = function_body(self.driver, "AppleDartReserveTablePool")
        self.assertIn("DramSize", pool)
        self.assertIn("LeafSpan", pool)
        self.assertIn("MidSpan", pool)
        # Running out must be detectable, not silent.
        alloc = function_body(self.driver, "AppleDartAllocTable")
        self.assertIn("return NULL", alloc)
        build = function_body(self.driver, "AppleDartBuildIdentityMap")
        self.assertEqual(build.count("out of memory"), 3)

    def test_translation_is_only_enabled_after_the_tables_exist(self) -> None:
        initialize = function_body(self.driver, "AppleDartIoMmuDxeInitialize")
        self.assertIn("IdentityMapRoot = AppleDartBuildIdentityMap(DramBase, DramSize)", initialize)
        # Root first, then TTBRs, then streams, then TCR. A TCR written before
        # its TTBR is a window in which the DART translates through whatever
        # the previous owner left behind.
        order = (
            "IdentityMapRoot = AppleDartBuildIdentityMap(DramBase, DramSize)",
            "DART_TTBR(DartInfo[DartIndex], sid, 0)",
            "DART_SID_ENABLE(DartInfo[DartIndex], i)",
            "DART_T8110_TCR_TRANSLATE_ENABLE | DART_T8110_TCR_FOUR_LEVEL",
        )
        positions = tuple(initialize.index(token) for token in order)
        self.assertEqual(positions, tuple(sorted(positions)))

        # And the DART has to be asked whether it accepted the mode, because
        # this is exactly the register that silently drops writes here.
        self.assertIn("refused the four-level TCR", initialize)

    def test_widths_are_checked_against_dram(self) -> None:
        # A DART whose VA or PA width cannot name the top of DRAM would give a
        # partial identity map: transparent for low addresses and silently
        # wrong above the cutoff, which is worse than refusing.
        initialize = function_body(self.driver, "AppleDartIoMmuDxeInitialize")
        self.assertIn("DART_T8110_PARAMS3_VA_WIDTH_SHIFT", initialize)
        self.assertIn("DART_T8110_PARAMS3_PA_WIDTH_SHIFT", initialize)
        self.assertIn("cannot address DRAM top", initialize)
        # Three levels reach 64GB; J813's DRAM starts above that.
        self.assertIn("VaWidth <= 36", initialize)

    def test_both_apertures_are_programmed(self) -> None:
        # Each usb DART node carries two independent DART instances and m1n1
        # holds DWC3 in reset expecting both to be reprogrammed. A T8142-only
        # branch used to skip every odd index on the theory that the companion
        # aperture faulted; both were measured to answer identically.
        self.assertNotIn("SILICON_PLATFORM == 8142", self.driver)
        self.assertNotIn("DartIndex & 1U", self.driver)

    def test_tlb_flush_targets_the_command_register(self) -> None:
        # This wrote to BaseAddress + BIT(8) -- the ERROR register -- and then
        # polled the real TLB_CMD, which is never busy because nothing was
        # ever commanded. It looked like a flush and was a no-op.
        flush = function_body(self.driver, "AppleDartT8110TlbFlush")
        self.assertIn("BaseAddress + DART_T8110_TLB_CMD,", flush)
        self.assertIn("DART_T8110_TLB_CMD_OP_FLUSH_ALL", flush)
        self.assertNotIn("BaseAddress + DART_T8110_TLB_CMD_FLUSH_ALL", flush)

    def test_register_and_pte_layout_matches_asahi(self) -> None:
        # The values below were read off J813 hardware, but their names come
        # from AsahiLinux. If a rebase moves either, this tree must move too.
        if not LINUX_DART.exists() or not LINUX_PGTABLE.exists():
            self.skipTest("AsahiLinux tree not present")

        linux_dart = LINUX_DART.read_text(encoding="utf-8")
        linux_pgtable = LINUX_PGTABLE.read_text(encoding="utf-8")

        expected = {
            "DART_T8110_TCR_FOUR_LEVEL": "BIT(3)",
            "DART_T8110_TCR_BYPASS_DAPF": "BIT(2)",
            "DART_T8110_TCR_BYPASS_DART": "BIT(1)",
            "DART_T8110_TCR_TRANSLATE_ENABLE": "BIT(0)",
        }
        for name, bit in expected.items():
            with self.subTest(register=name):
                self.assertRegex(linux_dart, rf"#define\s+{name}\s+{re.escape(bit)}")
                self.assertRegex(self.header, rf"#define\s+{name}\s+{re.escape(bit)}")

        # FLUSH_ALL is an operation value of zero, not a bit.
        self.assertRegex(linux_dart, r"#define\s+DART_T8110_TLB_CMD_OP_FLUSH_ALL\s+0")
        self.assertRegex(self.header, r"#define\s+DART_T8110_TLB_CMD_OP_FLUSH_ALL\s+0")

        # PTE address field: paddr >> 4 held in bits 37:10, i.e. a 16KB
        # granule and a 42-bit output address -- which is exactly the
        # PA_WIDTH J813 reports.
        self.assertRegex(
            linux_pgtable, r"#define\s+APPLE_DART2_PADDR_MASK\s+GENMASK_ULL\(37,\s*10\)"
        )
        self.assertRegex(linux_pgtable, r"#define\s+APPLE_DART2_PADDR_SHIFT\s+\(4\)")
        self.assertIn("#define APPLE_DART2_PTE_ADDR_MASK\t0x0000003FFFFFFC00ULL", self.header)
        self.assertIn("#define APPLE_DART2_PTE_ADDR_SHIFT\t4", self.header)
        self.assertEqual(0x0000003FFFFFFC00, ((1 << 38) - 1) ^ ((1 << 10) - 1))

    def test_leaf_entries_open_the_whole_page(self) -> None:
        # Subpage start 0 / end 0xfff is what makes the entire 16KB page
        # accessible; a zeroed subpage field would permit a single subpage and
        # fault everything else, which presents as sporadic DMA failure rather
        # than as a broken map.
        build = function_body(self.driver, "AppleDartBuildIdentityMap")
        self.assertIn("APPLE_DART_PTE_SUBPAGE_ALL", build)
        self.assertRegex(
            self.header, r"#define\s+APPLE_DART_PTE_SUBPAGE_ALL\s+\(0xfffULL << 40\)"
        )
        # Table descriptors carry no subpage field, so they must not get it.
        encode = function_body(self.driver, "AppleDartEncodePte")
        self.assertNotIn("APPLE_DART_PTE_SUBPAGE_ALL", encode)


if __name__ == "__main__":
    unittest.main()
