"""Run actual handoff C against fake services; no submodules required."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/J313SreHandoffDxe/J313SreHandoffDxe.c"


class HandoffTests(unittest.TestCase):
    def test_c_fault_injection(self):
        with tempfile.TemporaryDirectory(prefix="j313-sre-") as tmp:
            tmp = Path(tmp)
            for header in re.findall(r"#include <([^>]+)>", DRIVER.read_text()):
                dest = tmp / header
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text("/* Types supplied by host fixture. */\n")
            binary = tmp / "handoff"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-Wno-unused-parameter", "-I", str(tmp),
                            str(ROOT / "Tests/j313_sre_handoff_host.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_j313_only_opt_in(self):
        opted_in = [p for p in (ROOT / "Platform").rglob("*.dsc")
                    if "DEFINE J313_SRE_HANDOFF" in p.read_text()]
        self.assertEqual([p.parent.name for p in opted_in], ["MacBookAirMid2020Pkg"])
        text = opted_in[0].read_text()
        self.assertIn("!ifndef J313_SRE_HANDOFF", text)
        self.assertIn("!if $(J313_SRE_HANDOFF) == TRUE", text)
        fdf = opted_in[0].with_suffix(".fdf").read_text()
        self.assertIn("!if $(J313_SRE_HANDOFF) == TRUE\n  INF AppleSiliconPkg/Drivers/J313SreHandoffDxe/", fdf)

    def test_apriori_order_and_disabled_equivalence(self):
        rel = "Silicon/Apple/AppleSiliconPkg/Include/DXEApriori.fdf.inc"
        text = (ROOT / rel).read_text()
        original = subprocess.check_output(["git", "show", "e5e03e7:" + rel], cwd=ROOT, text=True)
        block = re.search(r"!ifdef J313_SRE_HANDOFF\n.*?!endif\n!endif\n", text, re.S)
        self.assertIsNotNone(block)
        self.assertEqual(text.replace(block[0], ""), original)
        names = ["SimpleFbDxe.inf", "RenderingEngineDxe.inf", "J313SreHandoffDxe.inf",
                 "ConPlatformDxe.inf", "ConSplitterDxe.inf", "GraphicsConsoleDxe.inf"]
        positions = [text.index(name) for name in names]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("!if $(J313_SRE_HANDOFF) == TRUE", block[0])
        for name in ["FrontpageDsc.inc", "FrontpageFdf.inc"]:
            rel = "Silicon/Apple/AppleSiliconPkg/" + name
            self.assertEqual((ROOT / rel).read_bytes(), subprocess.check_output(
                ["git", "show", "e5e03e7:" + rel], cwd=ROOT))


if __name__ == "__main__":
    unittest.main(verbosity=2)
