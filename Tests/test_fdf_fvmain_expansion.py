#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Every platform's FvMain module list is one of a small number of known shapes.

Silicon/Apple/AppleSiliconPkg/AppleSiliconFvMain.fdf.inc holds the INF lines
every .fdf has in common, plus the ones that differ, each behind a DEFINE. This
expands the include with each machine's DEFINEs and checks the resulting list --
contents *and* order.

WHY IT PINS SHAPES RATHER THAN MACHINES

It used to record one expected list per platform. With forty-three platforms
sharing one include that meant forty-three copies of a handful of distinct
lists, and adding a Mac meant appending a fifty-line array to a JSON file that
already contained the same array several times over. That is the same
duplication the board packages exist to remove, in test data.

So the expectation is the set of distinct module lists, each with the machines
that produce it. A new machine that matches an existing shape needs no change
here, which is the common case and the one that should be cheap. A machine that
produces a shape nobody has seen fails, and so does one that moves between
shapes -- which is what the test is for.

Order matters, so these are sequences, not sets. The one thing deliberately not
unified is where AppleNANDStorageDxe sits: machines that boot from internal
storage put it after SmbiosDxe, the rest after NonDiscoverablePciDeviceDxe, and
PLATFORM_NAND_SLOT keeps both.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
INCLUDE = REPO / "Silicon/Apple/AppleSiliconPkg/AppleSiliconFvMain.fdf.inc"
EXPECTED = Path(__file__).with_name("fdf_fvmain_expected.json")


#
# Left symbolic when comparing. These name which package supplies a module, not
# whether the module is there, and every machine has its own value for the first
# -- substituting them would make each machine's list unique and the comparison
# vacuous.
#
SYMBOLIC = ("PLATFORM_PKG", "PLATFORM_BOARD_PKG", "PLATFORM_FAMILY_PKG",
            "PLATFORM_SOC_PKG")


def defines_of(text: str) -> dict[str, str]:
    return dict(re.findall(r'^\s*DEFINE\s+(\w+)\s*=\s*(\S+)\s*$', text, re.M))


def expand(text: str, defines: dict[str, str]) -> list[str]:
    """Enough of the EDK2 .fdf preprocessor for !if/!endif and $(VAR)."""
    out: list[str] = []
    keep = [True]
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("!ifdef ") or line.startswith("!ifndef "):
            name = line.split(None, 1)[1].strip().strip("$()")
            have = name in defines
            keep.append(keep[-1] and (have if line.startswith("!ifdef") else not have))
            continue
        if line.startswith("!if "):
            cond = line[4:]
            m = re.match(r'\$\((\w+)\)\s*(==|!=)\s*(\S+)', cond)
            if not m:
                keep.append(keep[-1])
                continue
            name, op, want = m.groups()
            have = defines.get(name, "")
            hit = (have == want) if op == "==" else (have != want)
            keep.append(keep[-1] and hit)
            continue
        if line.startswith("!else"):
            if len(keep) > 1:
                keep[-1] = keep[-2] and not keep[-1]
            continue
        if line.startswith("!endif"):
            if len(keep) > 1:
                keep.pop()
            continue
        if not keep[-1] or not line.startswith("INF "):
            continue
        out.append(re.sub(
            r'\$\((\w+)\)',
            lambda m: m.group(0) if m.group(1) in SYMBOLIC
            else defines.get(m.group(1), m.group(0)),
            line))
    return out


class FdfFvMainExpansionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.include = INCLUDE.read_text(encoding="utf-8")
        cls.expected = json.loads(EXPECTED.read_text(encoding="utf-8"))

    def _fvmain_infs(self, fdf: Path) -> list[str]:
        text = fdf.read_text(encoding="utf-8")
        defines = defines_of(text)
        body = text[text.index("[FV.FvMain]"):]
        body = body[: body.index("[FV.FVMAIN_COMPACT]")]
        expanded = []
        for raw in body.splitlines():
            if raw.strip().startswith("!include") and "AppleSiliconFvMain.fdf.inc" in raw:
                expanded.extend(expand(self.include, defines))
            else:
                expanded.append(raw)
        return expand("\n".join(expanded), defines)

    def _shapes(self):
        """platform -> its expanded module list, for every platform in the tree."""
        return {fdf.name: self._fvmain_infs(fdf)
                for fdf in sorted(REPO.glob("Platform/*Pkg/*.fdf"))}

    def test_every_platform_matches_a_known_shape(self):
        known = {s["name"]: s["modules"] for s in self.expected["shapes"]}
        for name, modules in self._shapes().items():
            with self.subTest(platform=name):
                match = [k for k, v in known.items() if v == modules]
                self.assertTrue(
                    match,
                    f"{name} produces a FvMain module list matching no known shape. "
                    f"If that is intended, add it to {EXPECTED.name} in the same commit.")

    def test_every_platform_is_in_the_shape_it_was_recorded_in(self):
        recorded = {p: s["name"] for s in self.expected["shapes"] for p in s["platforms"]}
        shapes = self._shapes()
        known = {s["name"]: s["modules"] for s in self.expected["shapes"]}
        for name, modules in shapes.items():
            with self.subTest(platform=name):
                self.assertIn(name, recorded, f"{name} is not recorded in any shape")
                want = recorded[name]
                got = next((k for k, v in known.items() if v == modules), None)
                self.assertEqual(
                    got, want,
                    f"{name} moved from shape {want!r} to {got!r}")
        self.assertEqual(
            set(shapes), set(recorded),
            "the set of platforms changed; record the new ones in "
            f"{EXPECTED.name}")

    def test_the_include_is_actually_used(self):
        for fdf in sorted(REPO.glob("Platform/*Pkg/*.fdf")):
            with self.subTest(platform=fdf.name):
                self.assertRegex(
                    fdf.read_text(encoding="utf-8"),
                    r"(?m)^\s*!include\s+AppleSiliconPkg/AppleSiliconFvMain\.fdf\.inc\s*$")


if __name__ == "__main__":
    unittest.main()
