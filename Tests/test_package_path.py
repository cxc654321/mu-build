#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""ArmPkg, ArmPlatformPkg and DynamicTablesPkg must resolve to Silicon/ARM/TIANO.

All three exist in both MU_BASECORE and Silicon/ARM/TIANO since the M5-Dev
submodule bump. They did not before: the MU_BASECORE main pins has no ArmPkg,
so the whole tree was written against the TIANO copies.

EDK2 resolves a package name against PACKAGES_PATH in order, so whichever entry
comes first wins. Getting it wrong is not a missing file -- it is a DEC that
parses fine and lacks one GUID, and the failure is

    ArmMmuPeiLib.inf(51): error 4000: Value of Guid
    [gArmMmuReplaceLiveTranslationEntryFuncGuid] is not found under [Guids]

which says nothing about package paths.
"""

from __future__ import annotations

import ast
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "Platform"))

SHADOWED = ("ArmPkg", "ArmPlatformPkg", "DynamicTablesPkg")


class PackagePathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Read the literal rather than importing: PlatformBuildCommon pulls in
        # edk2toolext, which lives in the build venv, not in the test env.
        source = (REPO / "Platform/PlatformBuildCommon.py").read_text(encoding="utf-8")
        body = source[source.index("PACKAGES_PATH = ("):]
        body = body[: body.index(")") + 1]
        cls.order = list(ast.literal_eval(body.split("=", 1)[1].strip()))

    def test_tiano_precedes_basecore(self):
        self.assertLess(
            self.order.index("Silicon/ARM/TIANO"), self.order.index("MU_BASECORE"),
            "Silicon/ARM/TIANO must win the packages that exist in both")

    def test_the_shadowed_packages_resolve_to_tiano(self):
        for package in SHADOWED:
            with self.subTest(package=package):
                first = next(
                    (root for root in self.order if (REPO / root / package).is_dir()), None)
                if first is None:
                    self.skipTest(f"{package} is not checked out")
                self.assertEqual(first, "Silicon/ARM/TIANO")

    def test_armpkg_dec_declares_the_guid_the_dsc_needs(self):
        dec = REPO / "Silicon/ARM/TIANO/ArmPkg/ArmPkg.dec"
        if not dec.is_file():
            self.skipTest("Silicon/ARM/TIANO is not checked out")
        self.assertIn("gArmMmuReplaceLiveTranslationEntryFuncGuid",
                      dec.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
