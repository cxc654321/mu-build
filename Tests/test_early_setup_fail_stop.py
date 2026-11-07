"""Check real PrePi assembly control flow and assemble its extracted tail.

Host-side source control-flow checks and cross-assembly only: no hardware
execution, no full firmware build, and no ADT copy-bounds/saving validation.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Silicon/Apple/AppleSiliconPkg/PrePi/AArch64/ModuleEntryPoint.S"


def early_setup_tail(source):
    """Extract actual instructions, not a separately maintained assembly fixture."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)
    calls = list(re.finditer(r"^\s*bl\s+EarlySetup\s*$", source, re.M))
    if len(calls) != 1:
        raise AssertionError("expected exactly one EarlySetup call")
    return source[calls[0].start():source.index(".LArm64LinuxMagic:", calls[0].end())]


def trace_tail(tail, result, entry=None):
    """Follow this small instruction subset; unknown instructions fail closed.

    WFE is allowed to wake immediately, so termination must be prevented by
    the branch cycle, not by assuming that WFE itself never returns.
    """
    instructions, labels = [], {}
    for line in tail.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.endswith(":"):
            label = line[:-1]
            if label in labels:
                raise AssertionError("duplicate label")
            labels[label] = len(instructions)
        else:
            instructions.append(tuple(filter(None, re.split(r"[\s,]+", line))))
    if instructions[0] != ("bl", "EarlySetup"):
        raise AssertionError("tail must start at EarlySetup")
    pc = labels[entry] if entry else 1  # inject the C return value
    mask = 0
    seen, path = {}, []
    while True:
        state = (pc, mask)
        if state in seen:
            return "cycle", mask, path, path[seen[state]:]
        if not 0 <= pc < len(instructions):
            raise AssertionError("escaped the extracted tail")
        seen[state] = len(path)
        inst = instructions[pc]
        path.append(inst)
        op, *args = inst
        if op == "ret":
            return "return", mask, path, args
        if op == "cbz" and args[0] == "x0":
            pc = labels[args[1]] if result == 0 else pc + 1
        elif op == "b":
            pc = labels[args[0]]
        elif op == "msr" and args[0].lower() == "daifset":
            mask |= int(args[1].removeprefix("#"), 0)
            pc += 1
        elif inst == ("wfe",):
            if mask != 15:
                raise AssertionError("WFE reached without all DAIF masks set")
            pc += 1
        else:
            raise AssertionError(f"unexpected instruction on return/failure path: {inst}")


class EarlySetupFailStopTests(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text()
        self.tail = early_setup_tail(self.source)

    def assert_fail_stop(self, tail):
        outcome, mask, path, cycle = trace_tail(tail, 0)
        self.assertEqual(outcome, "cycle", "FALSE must never return to boot")
        self.assertEqual(mask, 15)
        self.assertIn(("wfe",), cycle)
        self.assertTrue(all(inst[0] in ("wfe", "b") for inst in cycle))
        self.assertFalse(any(inst[0] in ("ret", "bl", "blr") for inst in path))

    def test_false_is_masked_nonreturning_even_when_wfe_wakes(self):
        self.assert_fail_stop(self.tail)

    def test_success_keeps_ret_x28_without_entering_fail_stop(self):
        for result in (1, 2, 0xffffffffffffffff):
            with self.subTest(result=result):
                outcome, mask, path, target = trace_tail(self.tail, result)
                self.assertEqual((outcome, mask, target), ("return", 0, ["x28"]))
                self.assertEqual([inst[0] for inst in path], ["cbz", "ret"])

    def test_header_magic_mismatch_still_uses_original_return(self):
        self.assertRegex(self.source, r"(?m)^\s*bne\s+\.Lout\s*$")
        outcome, mask, path, target = trace_tail(self.tail, 0, entry=".Lout")
        self.assertEqual((outcome, mask, target), ("return", 0, ["x28"]))
        self.assertEqual(path, [("ret", "x28")])

    def test_regression_check_rejects_original_false_return(self):
        original_bug, count = re.subn(r"(cbz\s+x0,\s*)[^\s]+", r"\g<1>.Lout", self.tail)
        self.assertEqual(count, 1)
        with self.assertRaises(AssertionError):
            self.assert_fail_stop(original_bug)

    def test_real_tail_assembles_with_clang_aarch64(self):
        clang = shutil.which("clang")
        if clang is None:
            self.skipTest("clang unavailable; source control-flow checks still run")
        with tempfile.TemporaryDirectory(prefix="early-setup-fail-stop-") as tmp:
            tmp = Path(tmp)
            obj = tmp / "tail.o"
            command = [clang, "--target=aarch64-none-elf", "-x", "assembler",
                       "-c", "-o", str(obj), "-"]
            probe = subprocess.run(command, input=".text\nret\n", text=True, capture_output=True)
            if probe.returncode:
                self.skipTest("clang AArch64 target unavailable: " + probe.stderr.strip())
            # EarlySetup remains an unresolved external symbol in this object.
            compiled = subprocess.run(command, input=".text\n" + self.tail,
                                      text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertGreater(obj.stat().st_size, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
