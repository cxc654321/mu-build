"""Compile the production transport and production PIO; never access host MMIO."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe"


class J313SpiTransportTests(unittest.TestCase):
    def test_production_transport_sanitized(self):
        with tempfile.TemporaryDirectory(prefix="j313-spi-transport-") as tmp:
            binary = Path(tmp) / "test_transport"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                "-DJ313_HOST_TEST", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                str(DRIVER / "Tests/test_transport.c"),
                str(DRIVER / "J313SpiTransport.c"),
                str(DRIVER / "Core/j313_spi_pio.c"), "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT, timeout=90)
            result = subprocess.run([str(binary)], check=True, cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertIn("production transport mock tests: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
