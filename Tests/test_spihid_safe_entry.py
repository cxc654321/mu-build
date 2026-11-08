"""Compile and execute production SPI entry/state machine with no hardware access."""
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe"
CORE = DRIVER / "Core"

class SafeEntryTests(unittest.TestCase):
    def checked(self, args):
        result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_real_entry_mock_lifecycle(self):
        cc = shutil.which("clang")
        self.assertIsNotNone(cc)
        arch = {"arm64": "AArch64", "aarch64": "AArch64", "x86_64": "X64", "AMD64": "X64"}[platform.machine()]
        inc = ROOT / "MU_BASECORE/MdePkg/Include"
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "entry"
            self.checked([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
                          "-fsanitize=address,undefined", "-I", inc, "-I", inc / arch,
                          "-I", ROOT / "MU_BASECORE/MdeModulePkg/Include",
                          "-I", ROOT / "Silicon/Apple/AppleSiliconPkg/Include",
                          CORE / "test_entry.c", "-o", output])
            result = self.checked([output])
            self.assertIn("retained cleanup PASS", result)
            self.assertIn("photo delay: each entry path, retained cleanup, live polling, high-TPL skip PASS", result)

    def test_production_aarch64_freestanding_compile(self):
        cc = shutil.which("clang")
        self.assertIsNotNone(cc)
        inc = ROOT / "MU_BASECORE/MdePkg/Include"
        sources = [DRIVER / name for name in ("AppleSpiHidDxe.c", "J313SpiTransport.c",
                   "J313Keyboard.c", "J313BoardResources.c")]
        sources += [p for p in CORE.glob("*.c") if not p.name.startswith("test_")]
        self.checked([cc, "--target=aarch64-none-elf", "-ffreestanding", "-std=c11",
                      "-Wall", "-Wextra", "-Werror", "-fshort-wchar", "-fsyntax-only",
                      "-I", inc, "-I", inc / "AArch64",
                      "-I", ROOT / "MU_BASECORE/MdeModulePkg/Include",
                      "-I", ROOT / "Silicon/Apple/AppleSiliconPkg/Include", *sources])

    def test_production_session_synthetic_packets(self):
        cc = shutil.which("clang")
        self.assertIsNotNone(cc)
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "session"
            sources = [CORE / name for name in ("spihid_protocol.c", "sh_init.c", "hid_keyboard.c",
                        "key_queue.c", "j313_session.c", "test_session.c")]
            self.checked([cc, "-DJ313_HOST_TEST", "-std=c11", "-Wall", "-Wextra", "-Werror",
                          "-fsanitize=address,undefined", *sources, "-o", output])
            self.assertIn("bounds, stop PASS", self.checked([output]))

if __name__ == "__main__":
    unittest.main()
