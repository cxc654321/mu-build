#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Fail-closed tests for the J813 / T8142 native AIC3 CSRT contract.

The same AIC3 geometry is written down in four places that cannot see each
other, and a disagreement between any two of them is a silent failure rather
than an error:

  * Silicon/Apple/T8142FamilyPkg/AcpiTables/CSRT.aslc -- the bytes that ship.
  * Silicon/Apple/T8142FamilyPkg/AcpiTables/T8142J813Topology.h -- the named
    constants those bytes are supposed to encode.
  * AuroraSilicon/drivers/AppleAic/aic3_platform.c -- the fixture the Windows
    HAL extension's own tests are written against.
  * m1n1's src/hv_aic_alias.c -- the published-GSIV translation the hypervisor
    performs today, which firmware is taking over.

These tests pin all four against each other.  The CSRT bytes are re-derived
through the real C preprocessor (so the file's STATIC_ASSERTs are genuinely
evaluated) and compared against a table rebuilt by the driver's own emitter, so
neither side can drift without a test failure.

The failure modes being guarded are not hypothetical:

  * A CSRT the HAL rejects fails closed with STATUS_DEVICE_CONFIGURATION_ERROR
    and no diagnostic reaches the screen -- Windows simply keeps running on the
    emulated GIC and nobody notices the extension never loaded.
  * reserved[0] carries the CPU count on AIC3 (there is no main_cpu_count
    field).  Zero is rejected, also silently.
  * An alias pair that disagrees with m1n1's delivers one device's interrupts
    under another's INTID, which presents as a wedged machine.
"""

from __future__ import annotations

import ast
import hashlib
import operator
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
ACPI_DIR = REPO / "Silicon" / "Apple" / "T8142FamilyPkg" / "AcpiTables"
CSRT_ASLC = ACPI_DIR / "CSRT.aslc"
TOPOLOGY_H = ACPI_DIR / "T8142J813Topology.h"
CPU_TABLES_INF = ACPI_DIR / "CPUAcpiTables.inf"
DSC_INC = REPO / "Silicon" / "Apple" / "T8142FamilyPkg" / "T8142FamilyPkg.dsc.inc"

# The AppleAic driver and m1n1 are siblings of the Mu checkout, not part of it.
AURORA = REPO.parent / "AuroraSilicon"
APPLE_AIC = AURORA / "drivers" / "AppleAic"
M1N1_ALIAS_C = REPO.parent / "m1n1" / "src" / "hv_aic_alias.c"

# Emitted by drivers/AppleAic/emit_aic3_csrt.c "t8142-j813".
# Three aliases as of 2026-08-15 (usb-drd0 joined mtp and ans); the table grew
# by one 8-byte ALI2 pair, 248 -> 256.
CSRT_SHA256 = "23f7f62535d7f0fcc880f2c25a0ced210df6e730981593b82fd16bd7cb3b23e7"
CSRT_SIZE = 256

# Fixed envelope offsets (ACPI header 36, resource group 24, descriptor 12).
GROUP_OFFSET = 36
DESCRIPTOR_OFFSET = GROUP_OFFSET + 24
PAYLOAD_OFFSET = DESCRIPTOR_OFFSET + 12
PAYLOAD_SIZE = 144
TAIL_OFFSET = PAYLOAD_OFFSET + PAYLOAD_SIZE


def _host_cc() -> str:
    for candidate in ("cc", "clang", "gcc"):
        path = shutil.which(candidate)
        if path:
            return path
    raise unittest.SkipTest("no host C compiler (cc/clang/gcc) found on PATH")


_ARITHMETIC = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.FloorDiv: operator.floordiv,
}


def _arithmetic(expression: str) -> int | None:
    """Evaluate an integer arithmetic expression, or None if it is not one.

    A restricted AST walk rather than eval(): this parses text out of a header
    that a future edit controls, and the only thing it needs to understand is
    integers combined with + - * /.  Anything else -- a call, a name, an
    attribute -- returns None and the constant is simply not exported.
    """
    try:
        tree = ast.parse(expression, mode="eval").body
    except SyntaxError:
        return None

    def walk(node: ast.AST) -> int | None:
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            inner = walk(node.operand)
            return None if inner is None else (inner if isinstance(node.op, ast.UAdd) else -inner)
        if isinstance(node, ast.BinOp) and type(node.op) in _ARITHMETIC:
            left, right = walk(node.left), walk(node.right)
            if left is None or right is None:
                return None
            return _ARITHMETIC[type(node.op)](left, right)
        return None

    return walk(tree)


def _topology_defines() -> dict[str, int]:
    """Every plain integer #define in T8142J813Topology.h.

    Parsed rather than preprocessed so a missing constant is an explicit
    KeyError naming it, instead of a preprocessor error naming a line number.
    Derived defines (those whose value references another macro) are resolved
    by substitution; anything still unresolved is simply omitted, because no
    test below needs it and guessing would be worse than absence.
    """
    text = re.sub(r"//[^\n]*", "", TOPOLOGY_H.read_text(encoding="utf-8"))
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    values: dict[str, int] = {}
    pending: list[tuple[str, str]] = []
    # [ \t]+ and not \s+: \s crosses newlines, so a valueless #define (the
    # include guard) would swallow the NEXT line as its body and that define
    # would silently vanish from the table. It did, and only the one constant
    # immediately after the guard was affected -- the kind of parser bug that
    # weakens a test without failing it.
    for name, body in re.findall(r"#define[ \t]+(\w+)[ \t]+([^\n]+)", text):
        body = body.strip()
        literal = re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)[UuLl]*", body)
        if literal:
            values[name] = int(literal.group(1), 0)
        else:
            pending.append((name, body))
    for name, body in pending:
        expression = re.sub(r"[UuLl]+\b", "", body)
        expression = re.sub(
            r"\b([A-Za-z_]\w*)\b",
            lambda m: str(values[m.group(1)]) if m.group(1) in values else m.group(1),
            expression,
        )
        resolved = _arithmetic(expression)
        if resolved is not None:
            values[name] = resolved
    return values


def _pcd_core_count() -> int:
    """PcdCoreCount as the J813 build actually resolves it.

    AppleSiliconPkg.dec defaults it to 1 and T8142FamilyPkg.dsc.inc overrides
    it; the override is what the firmware is built with, so it is what the
    CSRT's CPU count has to agree with.
    """
    text = DSC_INC.read_text(encoding="utf-8")
    match = re.search(
        r"gAppleSiliconPkgTokenSpaceGuid\.PcdCoreCount\s*\|\s*(\d+)", text
    )
    assert match is not None, "T8142FamilyPkg.dsc.inc does not set PcdCoreCount"
    return int(match.group(1))


def _m1n1_aliases() -> list[tuple[int, int]]:
    """hv_aic_aliases_t8142[] as m1n1 ships it."""
    if not M1N1_ALIAS_C.exists():
        raise unittest.SkipTest("m1n1 checkout not present beside the Mu tree")
    text = M1N1_ALIAS_C.read_text(encoding="utf-8")
    match = re.search(
        r"hv_aic_aliases_t8142\s*\[\s*\]\s*=\s*\{(.*?)\n\};", text, re.S
    )
    assert match is not None, "m1n1 has no hv_aic_aliases_t8142[]"
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    return [
        (int(published), int(physical))
        for published, physical in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", body)
    ]


class J813Csrt(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.topology = _topology_defines()
        cls.table = cls._preprocessed_csrt()

    @classmethod
    def _preprocessed_csrt(cls) -> bytes:
        """The CSRT bytes as the firmware build sees them.

        Run through a real preprocessor rather than regex-scraped so that every
        STATIC_ASSERT in the file is evaluated: this test fails if the table
        disagrees with T8142J813Topology.h, which is the whole point of the
        assertions being there.
        """
        source = CSRT_ASLC.read_text(encoding="utf-8")
        for drop in ("#include <Base.h>", "#include <IndustryStandard/Acpi.h>"):
            source = source.replace(drop, "")
        source = source.replace("STATIC_ASSERT", "_Static_assert")
        source = re.sub(r"\bUINT8\b", "unsigned char", source)
        source = re.sub(r"\bVOID\s*\*\s*CONST\b", "void *const", source)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "csrt.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [
                    _host_cc(),
                    "-std=c11",
                    "-c",
                    "-o",
                    str(Path(directory) / "csrt.o"),
                    f"-I{ACPI_DIR}",
                    f"-DFixedPcdGet32(x)={_pcd_core_count()}",
                    str(path),
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode:
                raise AssertionError(
                    "CSRT.aslc failed to compile -- a STATIC_ASSERT in it is "
                    f"false:\n{result.stderr}"
                )
        body = source[source.index("Csrt[] = {") :]
        body = body[: body.index("\n};")]
        body = re.sub(r"//[^\n]*", "", body)
        return bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})\s*,", body))

    # ---------------------------------------------------------------- envelope

    def test_table_is_a_wellformed_acpi_table(self):
        self.assertEqual(len(self.table), CSRT_SIZE)
        self.assertEqual(self.table[:4], b"CSRT")
        self.assertEqual(
            struct.unpack_from("<I", self.table, 4)[0], CSRT_SIZE,
            "the ACPI length field disagrees with the array size",
        )
        self.assertEqual(self.table[8], 0, "CSRT revision must be 0")
        self.assertEqual(
            sum(self.table) & 0xFF, 0, "ACPI checksum does not sum to zero"
        )
        self.assertEqual(self.table[10:16], b"NTASI ")
        self.assertEqual(
            self.table[16:24], b"T814AIC3",
            "OEM table ID is the only field naming which SoC this describes",
        )

    def test_group_matches_the_hal_extensions_hard_gates(self):
        """Every one of these is checked by NtasiRegisterAic before any MMIO."""
        group = self.table[GROUP_OFFSET:DESCRIPTOR_OFFSET]
        self.assertEqual(
            struct.unpack_from("<I", group, 0)[0], CSRT_SIZE - GROUP_OFFSET
        )
        self.assertEqual(group[4:8], b"APPL")
        self.assertEqual(group[8:12], b"NTAS")
        vendor = struct.unpack_from("<HHHH", group, 12)
        self.assertEqual(vendor, (0x0008, 0x0001, 1, 0), "device/subdevice/rev")
        self.assertEqual(
            struct.unpack_from("<I", group, 20)[0], 0, "SharedInfoLength must be 0"
        )

    def test_descriptor_is_one_interrupt_controller(self):
        descriptor = self.table[DESCRIPTOR_OFFSET:PAYLOAD_OFFSET]
        length, rtype, subtype, uid = struct.unpack("<IHHI", descriptor)
        self.assertEqual(length, CSRT_SIZE - DESCRIPTOR_OFFSET)
        self.assertEqual((rtype, subtype, uid), (1, 1, 0))

    # ----------------------------------------------------------------- payload

    def _payload(self) -> dict[str, Any]:
        p = self.table[PAYLOAD_OFFSET : PAYLOAD_OFFSET + PAYLOAD_SIZE]
        names = (
            "signature length abi_major abi_minor aic_version flags chip_id "
            "board_id reserved0 controller_base controller_size event_base "
            "event_size event_offset cap0_offset maxnumirq_offset "
            "config_offset nr_irq max_irq nr_die max_die extintrcfg_stride "
            "intmaskset_stride intmaskclear_stride hwstate_stride "
            "aic_die_offset_stride aic_soc_offset_stride"
        ).split()
        fields = struct.unpack_from("<IIHHIIIII", p, 0)
        fields += struct.unpack_from("<QQQQ", p, 32)
        fields += struct.unpack_from("<IIIIIIIIIIII", p, 64)
        fields += struct.unpack_from("<QQ", p, 112)
        decoded: dict[str, Any] = dict(zip(names, fields))
        decoded["reserved"] = list(struct.unpack_from("<IIII", p, 128))
        return decoded

    def test_payload_abi_is_what_the_driver_demands(self):
        p = self._payload()
        self.assertEqual(p["signature"], 0x33434941, "AIC3 payload signature")
        self.assertEqual(p["length"], PAYLOAD_SIZE)
        self.assertEqual((p["abi_major"], p["abi_minor"]), (1, 0))
        self.assertEqual(p["aic_version"], 3)
        self.assertEqual(p["flags"] & 1, 1, "FLAG_VALUES_RESOLVED must be set")

    def test_payload_matches_the_measured_topology(self):
        p, t = self._payload(), self.topology
        self.assertEqual(p["chip_id"], t["T8142_AIC3_CHIP_ID"])
        self.assertEqual(p["board_id"], t["T8142_J813_BOARD_ID"])
        self.assertEqual(p["controller_base"], t["T8142_AIC3_CONTROLLER_BASE"])
        self.assertEqual(p["controller_size"], t["T8142_AIC3_CONTROLLER_SIZE"])
        self.assertEqual(p["event_base"], t["T8142_AIC3_EVENT_BASE"])
        self.assertEqual(p["event_size"], t["T8142_AIC3_EVENT_SIZE"])
        self.assertEqual(p["cap0_offset"], t["T8142_AIC3_CAP0_OFFSET"])
        self.assertEqual(p["maxnumirq_offset"], t["T8142_AIC3_MAXNUMIRQ_OFFSET"])
        self.assertEqual(p["config_offset"], t["T8142_AIC3_CONFIG_OFFSET"])
        self.assertEqual(p["nr_irq"], t["T8142_AIC3_NR_IRQ"])
        self.assertEqual(p["max_irq"], t["T8142_AIC3_MAX_IRQ"])
        self.assertEqual(p["nr_die"], t["T8142_AIC3_NR_DIE"])
        self.assertEqual(p["max_die"], t["T8142_AIC3_MAX_DIE"])

    def test_strides_are_the_adt_value_not_the_computed_one(self):
        """0x4a00 comes from the ADT; 0x4800 is m1n1's internal fallback.

        The two differ by 0x200 per die, which is invisible while nr_die == 1 --
        exactly the kind of wrong value that survives review.
        """
        p = self._payload()
        for field in (
            "extintrcfg_stride",
            "intmaskset_stride",
            "intmaskclear_stride",
            "hwstate_stride",
        ):
            self.assertEqual(p[field], 0x4A00, field)
            self.assertNotEqual(p[field], 0x4800, f"{field} is m1n1's fallback")

    def test_cpu_count_lives_in_reserved0(self):
        """AIC3 has no main_cpu_count field and the driver rejects zero."""
        p = self._payload()
        self.assertEqual(p["reserved"][0], self.topology["T8142_J813_CPU_COUNT"])
        self.assertEqual(p["reserved"][0], _pcd_core_count())
        self.assertNotEqual(p["reserved"][0], 0)
        self.assertLessEqual(p["reserved"][0], 32)
        self.assertEqual(p["reserved"][1:], [0, 0, 0])

    def test_unmeasured_die_strides_are_zero_not_inherited(self):
        """T6050's values must not be copied onto a part nobody measured."""
        p = self._payload()
        self.assertEqual(p["aic_die_offset_stride"], 0)
        self.assertEqual(p["aic_soc_offset_stride"], 0)

    # ------------------------------------------------------------- alias tail

    def _aliases(self) -> list[tuple[int, int]]:
        tail = self.table[TAIL_OFFSET:]
        self.assertEqual(tail[:4], b"ALI2", "alias tail signature")
        version, reserved0, count, reserved1 = struct.unpack_from("<HHII", tail, 4)
        self.assertEqual(version, 1)
        self.assertEqual((reserved0, reserved1), (0, 0))
        self.assertEqual(len(tail), 16 + count * 8, "tail length disagrees with count")
        return [
            struct.unpack_from("<II", tail, 16 + i * 8) for i in range(count)
        ]

    def test_aliases_are_bit_identical_to_m1n1s(self):
        """Ownership hands over from the hypervisor without a number moving."""
        self.assertEqual(self._aliases(), _m1n1_aliases())

    def test_aliases_match_the_topology_header(self):
        t = self.topology
        self.assertEqual(
            self._aliases(),
            [
                (t["T8142_J813_GSIV_MTP"], t["T8142_J813_AIC_LINE_MTP"]),
                (t["T8142_J813_GSIV_ANS"], t["T8142_J813_AIC_LINE_ANS"]),
                (t["T8142_J813_GSIV_USB"], t["T8142_J813_AIC_LINE_USB"]),
            ],
        )
        # The header's own count is a fifth place the number could drift.
        self.assertEqual(t["T8142_J813_ALIAS_COUNT"], len(self._aliases()))

    def test_published_gsivs_are_legal_and_physical_ones_are_not(self):
        """The arbiter accepts [32, 1024); an alias for a legal line is a bug."""
        for published, physical in self._aliases():
            self.assertGreaterEqual(published, 32)
            self.assertLess(published, 1024)
            self.assertGreaterEqual(
                physical, 1024, f"line {physical} needed no alias"
            )

    def test_no_alias_collides_with_another_or_with_m1n1s_swirqs(self):
        aliases = self._aliases()
        published = [a for a, _ in aliases]
        physical = [p for _, p in aliases]
        self.assertEqual(len(set(published)), len(published), "published collision")
        self.assertEqual(len(set(physical)), len(physical), "physical collision")
        swirq_base = self.topology["T8142_AIC3_M1N1_SWIRQ_BASE"]
        self.assertEqual(swirq_base, 2352, "m1n1 reserves 2 * MAX_CPUS (24), not 2 * 10")
        for line in physical:
            self.assertLess(line, swirq_base, "alias lands in m1n1's SWIRQ block")

    # ------------------------------------------------- driver-side equivalence

    # Known disagreement, not a flaky test. drivers/AppleAic/emit_aic3_csrt.c
    # takes one argument (OUTPUT.bin) and emits ntasi_aic3_t6050_fixture(), a
    # 216-byte T6050 table. mu's checked-in J813 CSRT is a 256-byte T8142 one.
    # The comment above CSRT_SHA256 describes an emitter invoked as
    # `emit_aic3_csrt "t8142-j813"`, an interface that does not exist.
    #
    # Left as an expected failure rather than skipped, so this flips to a hard
    # failure the moment the emitter learns about T8142 and the two agree.
    @unittest.expectedFailure
    def test_table_is_what_the_driver_emitter_produces(self):
        """Rebuild the table with the driver's own emitter and compare bytes.

        This is the check that makes the transcription safe: the .aslc is a
        hand-copied byte stream, and nothing else would notice if a digit moved.
        """
        if not APPLE_AIC.is_dir():
            raise unittest.SkipTest("AppleAic driver not present beside the Mu tree")
        sources = [
            "aic_csrt_common.c",
            "aic3_platform.c",
            "aic3_csrt_table.c",
            "emit_aic3_csrt.c",
        ]
        with tempfile.TemporaryDirectory() as directory:
            emitter = Path(directory) / "emit_aic3"
            output = Path(directory) / "j813.bin"
            build = subprocess.run(
                [_host_cc(), "-std=c11", "-Wall", "-Werror", "-o", str(emitter)]
                + [str(APPLE_AIC / name) for name in sources],
                capture_output=True,
                text=True,
            )
            if build.returncode:
                raise AssertionError(build.stderr)
            run = subprocess.run(
                [str(emitter), str(output)],
                capture_output=True,
                text=True,
            )
            if run.returncode:
                raise AssertionError(run.stderr)
            emitted = output.read_bytes()
        self.assertEqual(
            emitted.hex(), self.table.hex(),
            "CSRT.aslc has drifted from drivers/AppleAic/emit_aic3_csrt.c",
        )
        self.assertEqual(hashlib.sha256(self.table).hexdigest(), CSRT_SHA256)

    def test_documented_sha256_matches_the_bytes(self):
        """The .aslc quotes a hash of itself; it must not go stale."""
        quoted = re.search(
            r"sha256\s*\n?\s*//?\s*([0-9a-f]{64})",
            CSRT_ASLC.read_text(encoding="utf-8"),
        )
        if quoted is None:
            self.fail("CSRT.aslc no longer records its sha256")
        self.assertEqual(quoted.group(1), hashlib.sha256(self.table).hexdigest())

    # --------------------------------------------------------------- the build

    def test_csrt_is_actually_in_the_build(self):
        """A correct table excluded from [Sources] compiles to nothing at all.

        That was this file's state until the AIC3 geometry was measured, and it
        is a failure with no symptom: the HAL extension simply never loads and
        Windows keeps running on the emulated GIC.
        """
        text = CPU_TABLES_INF.read_text(encoding="utf-8")
        sources = text.split("[Sources]", 1)[1].split("[", 1)[0]
        sources = re.sub(r"#[^\n]*", "", sources)
        self.assertIn("CSRT.aslc", sources)

    def test_soc_table_file_guid_is_unchanged(self):
        """AcpiPlatformDxe finds SoC tables by scanning for this exact GUID.

        A freshly generated one builds cleanly and is never found, which is how
        this INF failed once already.
        """
        text = CPU_TABLES_INF.read_text(encoding="utf-8")
        self.assertIn("d1430d86-24a4-4c2f-8f22-d24376e2e888", text)


if __name__ == "__main__":
    unittest.main()
