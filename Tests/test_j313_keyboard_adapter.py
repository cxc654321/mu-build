#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 J313HidKeyboard contributors
"""Compile actual DXE keyboard adapter with real UEFI headers and mocked services."""
import os
import pathlib
import platform
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class KeyboardAdapterTest(unittest.TestCase):
    def test_production_adapter(self):
        arch = "AArch64" if platform.machine().lower() in ("arm64", "aarch64") else "X64"
        with tempfile.TemporaryDirectory(prefix="j313-keyboard-") as tmp:
            exe = pathlib.Path(tmp) / "test"
            command = [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-function", "-DJ313_HOST_TEST", "-fshort-wchar",
                       "-I", str(ROOT / "Silicon/Apple/AppleSiliconPkg/Include"),
                       "-I", str(ROOT / "MU_BASECORE/MdePkg/Include"),
                       "-I", str(ROOT / "MU_BASECORE/MdePkg/Include" / arch),
                       "-fsanitize=address,undefined", "-g",
                       str(ROOT / "Tests/test_j313_keyboard_adapter.c"),
                       str(ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe/Core/key_queue.c"),
                       "-o", str(exe)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
            self.assertIn("production J313Keyboard adapter: PASS", result.stdout)

if __name__ == "__main__":
    unittest.main()
