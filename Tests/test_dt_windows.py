#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""The generated MMIO windows match the device tree, and the tables match them.

Tools/dtwindows.py reads Asahi's vendored device trees and emits
Include/Platform/Ntasi<Machine>Windows.h. Two things can go wrong and both are
silent, so both are checked here:

  1. Someone edits the vendored .dtsi (or bumps it) and forgets to regenerate.
  2. Someone edits the generated header by hand.

Either shows up as `dtwindows.py check` reporting a stale file.

The third test is the one that mattered when this was written: the nine windows
in mNtasiMcaWindows must stay in _CRS order, because AppleMcaAudio indexes them.
Seven now come from the tree and two are sub-page slices no node describes, so
the order is a mix of macro references and literals and is easy to get wrong.
"""

from __future__ import annotations

import importlib.util
import re
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "Tools/dtwindows.py"
GENERATED = REPO / "Silicon/Apple/AppleSiliconPkg/Include/Platform/NtasiJ414sWindows.h"
TABLE = REPO / "Silicon/Apple/AppleSiliconPkg/Include/Platform/NtasiMediaJ414s.h"

# _CRS order, as AppleMcaAudio indexes it. Recorded when the windows moved from
# hand-transcribed literals to generated macros; every one was byte-identical.
EXPECTED_MCA = [
    (0x39B600000, 0x10000),   # 0 MCA cluster registers
    (0x39B500000, 0x20000),   # 1 MCA switch / DMA glue
    (0x39B400000, 0x34000),   # 2 ADMAC
    (0x28E03C000, 0x14000),   # 3 NCO
    (0x290280000, 0x01000),   # 4 pmgr_east PS page      (authored, sub-page)
    (0x39B044000, 0x04000),   # 5 i2c1
    (0x39B04C000, 0x04000),   # 6 i2c3
    (0x39B028000, 0x04000),   # 7 pinctrl_ap
    (0x28E03807C, 0x00018),   # 8 mca-switch clock mux   (authored, sub-page)
]


def load_tool():
    spec = importlib.util.spec_from_file_location("dtwindows", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def macros() -> dict[str, tuple[int, int]]:
    text = GENERATED.read_text(encoding="utf-8")
    return {
        name: (int(base, 16), int(size, 16))
        for name, base, size in re.findall(
            r'#define\s+(NTASI_J414S_W_\w+)\s+\{\s*0x([0-9A-Fa-f]+)ULL,'
            r'\s*0x([0-9A-Fa-f]+)ULL\s*\}', text)
    }


def table_windows() -> list[tuple[int, int]]:
    text = TABLE.read_text(encoding="utf-8")
    body = text[text.index("mNtasiMcaWindows[] = {"):]
    body = body[: body.index("};")]
    known, out = macros(), []
    for line in body.splitlines():
        literal = re.search(r'\{\s*0x([0-9A-Fa-f]+)ULL,\s*0x([0-9A-Fa-f]+)ULL\s*\}', line)
        if literal:
            out.append((int(literal.group(1), 16), int(literal.group(2), 16)))
            continue
        reference = re.search(r'(NTASI_J414S_W_\w+)\s*,', line)
        if reference:
            out.append(known[reference.group(1)])
    return out


class DeviceTreeWindowTests(unittest.TestCase):
    def test_generated_headers_are_not_stale(self):
        done = subprocess.run([sys.executable, str(TOOL), "check"],
                              capture_output=True, text=True, cwd=REPO)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_every_generated_window_comes_from_a_device_tree_node(self):
        resolved = {(base, size) for _, _, _, base, size in load_tool().resolve("j414s")["Mca"]}
        for name, window in macros().items():
            with self.subTest(macro=name):
                self.assertIn(window, resolved)

    def test_the_crs_table_keeps_its_order(self):
        self.assertEqual(table_windows(), EXPECTED_MCA)

    def test_the_two_authored_windows_are_not_device_tree_nodes(self):
        """If a node ever describes them, they should stop being literals."""
        resolved = {base for _, _, _, base, _ in load_tool().resolve("j414s")["Mca"]}
        for base in (0x290280000, 0x28E03807C):
            with self.subTest(window=hex(base)):
                self.assertNotIn(base, resolved)


if __name__ == "__main__":
    unittest.main()
