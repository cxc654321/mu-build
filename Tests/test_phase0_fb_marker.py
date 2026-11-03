"""Host static guards only: a framebuffer build marker is not SPI entry proof."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Silicon/Apple/AppleSiliconPkg/Library/AppleFbDebugLib/AppleFbDebugLib.c"


class Phase0FbMarkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()
        cls.probe = cls.source.split("FbProbe (\n", 1)[1].split("\nSTATIC\n", 1)[0]

    def test_marker_follows_banner_unconditionally(self):
        code = re.sub(r"//[^\n]*", "", self.probe)
        self.assertRegex(code,
            r'FbPuts \("#### APPLE FB DEBUG CONSOLE ####\\n"\);\s*'
            r'FbPuts \("J313-P0-20261104-r1\\n"\);')
        self.assertEqual(self.source.count("J313-P0-20261104-r1"), 1)

    def test_probe_still_clears_then_prints_for_each_library_instance(self):
        for statement in ("if (mFb.Probed)", "mFb.Probed = TRUE;",
                          "mFb.Ready    = TRUE;",
                          "SetMem ((VOID *)mFb.Base, mFb.StridePx * 4 * CellH * mFb.Rows, 0);",
                          "FbFlush (0, mFb.StridePx * 4 * CellH * mFb.Rows);"):
            self.assertIn(statement, self.probe)
        self.assertLess(self.probe.index("SetMem ("), self.probe.index("J313-P0-20261104-r1"))
        self.assertIn("STATIC struct {", self.source)

    def test_no_photography_pacing_or_unused_line_buffer(self):
        self.assertNotRegex(self.source,
            r"\b(?:FbSleepMs|LineIsInteresting|mLine|mLineLen|Stall|"
            r"MicroSecondDelay|NanoSecondDelay)\b")
        self.assertNotRegex(self.source, r"1200|cntfrq_el0|cntvct_el0")
        self.assertNotIn("long enough to photograph", self.source)

    def test_existing_mapping_and_assert_stop_preserved(self):
        for statement in (
            "BootArgs = (APPLE_BOOT_ARGS *)(UINTN)FixedPcdGet64 (PcdBootArgsPointer);",
            "mFb.Base     = (volatile UINT32 *)(UINTN)BootArgs->VideoBase;",
            "mFb.StridePx = (UINTN)(BootArgs->VideoStride / 4);",
            "mFb.Mirror   = FB_MIRROR_X;",
            "CpuDeadLoop ();",
        ):
            self.assertIn(statement, self.source)

    def test_marker_is_not_spi_entry_evidence(self):
        self.assertIn("this is not SPI entry evidence", self.probe)
        # Existing all-uppercase rendering is deliberately unchanged.
        self.assertIn("Ch = (CHAR8)(Ch - 'a' + 'A');", self.source)


if __name__ == "__main__":
    unittest.main()
