#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""T6040/T6041 split-BAR ANS regression tests."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / "Tests" / "test_t6040_ans_contract.c"
ANS = (
    REPO
    / "Silicon"
    / "Apple"
    / "AppleSiliconPkg"
    / "Drivers"
    / "AppleNANDStorageDxe"
)
CORE = ANS / "Shared" / "AppleNvmeCore.c"
CONTROLLER = ANS / "Shared" / "AppleNvmeControllerCore.c"
RUNTIME = ANS / "AppleNANDStorageDxe.c"
ACPI = (
    REPO
    / "Silicon"
    / "Apple"
    / "AppleSiliconPkg"
    / "Drivers"
    / "AcpiPlatformDxe"
    / "AcpiPlatform.c"
)


def _host_cc() -> str:
    for candidate in ("cc", "clang", "gcc"):
        path = shutil.which(candidate)
        if path:
            return path
    raise unittest.SkipTest("no host C compiler found")


class TestT6040AnsContract(unittest.TestCase):
    def test_host_core_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test_t6040_ans_contract"
            build = subprocess.run(
                [
                    _host_cc(),
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-o",
                    str(binary),
                    str(SOURCE),
                    str(CORE),
                ],
                cwd=REPO,
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("all assertions passed", run.stdout)
            self.assertNotIn("FAIL:", run.stdout)

    def test_runtime_selection_is_capability_driven(self) -> None:
        controller = CONTROLLER.read_text(encoding="utf-8")
        runtime = RUNTIME.read_text(encoding="utf-8")
        self.assertNotIn("SILICON_PLATFORM == 8142", controller)
        self.assertIn("hw->secure_io_queue_registers", controller)
        self.assertIn("ntasi_ans_hw_t604x", runtime)
        self.assertIn("SILICON_PLATFORM == 6040", runtime)
        self.assertIn("SILICON_PLATFORM == 6041", runtime)
        self.assertIn("Device->NvmeHw->secure_io_queue_registers", runtime)

    def test_acpi_appends_reg9_as_range_three(self) -> None:
        acpi = ACPI.read_text(encoding="utf-8")
        sart = acpi.index("AppleAnsAddMemoryResource (CrsNode, SartBase, SartSize)")
        secure = acpi.index("AppleAnsAddMemoryResource (\n               CrsNode,\n               NvmeStandardBase", sart)
        interrupt = acpi.index("AmlCodeGenRdInterrupt", secure)
        self.assertLess(sart, secure)
        self.assertLess(secure, interrupt)
        self.assertIn("dt_node_reg (AnsNode, 9", acpi)
        self.assertIn("APPLE_ANS_NVME_SECURE_MIN_SIZE", acpi)

    def test_t6040_pmgr_names_are_resolved_exactly(self) -> None:
        runtime = RUNTIME.read_text(encoding="utf-8")
        acpi = ACPI.read_text(encoding="utf-8")
        for source in (runtime, acpi):
            self.assertIn('"APCIE_ST0"', source)
            self.assertIn('"APCIE_SYS_ST0"', source)
            self.assertIn('"APCIE_SYS_ST1"', source)
            self.assertNotIn("0x502280108", source)
            self.assertNotIn("0x502280380", source)
            self.assertNotIn("0x502280388", source)


if __name__ == "__main__":
    unittest.main()
