"""Static guard for J313MemMapDxe camera pauses only; SPI-HID is out of scope."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/J313MemMapDxe/J313MemMapDxe.c"


class NoCameraHoldsTests(unittest.TestCase):
    def test_no_camera_pause_or_hold_message(self):
        source = DRIVER.read_text()
        self.assertNotRegex(source, r"(?i)camera|\bholding\b|\bhold\b")
        # This diagnostic driver needs no delays; catch alternate delay APIs too.
        self.assertNotRegex(source, r"\b(?:Stall|MicroSecondDelay|NanoSecondDelay)\s*\(")

    def test_watchdog_and_ready_to_boot_reporting_preserved(self):
        source = DRIVER.read_text()
        self.assertEqual(len(re.findall(
            r"gBS->SetWatchdogTimer\s*\(0,\s*0,\s*0,\s*NULL\)", source)), 2)
        for marker in ("OnReadyToBoot", "&gEfiEventReadyToBootGuid",
                       "ReportMemoryMap ();", "ReportMemoryHobs ();",
                       "LOADED: dumping", "UI CHAIN:"):
            self.assertIn(marker, source)


if __name__ == "__main__":
    unittest.main()
