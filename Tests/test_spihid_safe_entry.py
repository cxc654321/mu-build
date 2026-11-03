"""Phase0 only: compile the actual entry, never access or emulate SPI hardware.

Requires clang and checked-out MdePkg headers. Missing tools/headers are failures,
not skipped safety checks. Temporary build products live in the quarantine tree.
"""
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe/AppleSpiHidDxe.c"
INF = DRIVER.with_suffix(".inf")
INCLUDE = ROOT / "MU_BASECORE/MdePkg/Include"
QUARANTINE = ROOT.parent / "artifacts/spihid-quarantine-a26de9a"
MARKER = "J313-P0-20261104-r1 SPI-ENTRY"
DISABLED = "transport disabled pending validated board mapping and protocol"


class SafeEntryTests(unittest.TestCase):
    def run_checked(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def compiler(self):
        compiler = shutil.which("clang")
        self.assertIsNotNone(compiler, "clang is required")
        self.assertTrue((INCLUDE / "Uefi.h").is_file(), "real MdePkg headers required")
        return compiler

    def flags(self, arch):
        return ["-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
                "-I", str(INCLUDE), "-I", str(INCLUDE / arch)]

    def test_static_entry_is_only_fixed_diagnostics(self):
        source = DRIVER.read_text()
        code = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
        self.assertEqual(re.findall(r"#include\s+<([^>]+)>", code),
                         ["Uefi.h", "Library/DebugLib.h"])
        # Exact executable-body allowlist rejects direct register dereferences,
        # alternate hardware APIs, loops, waits, ADT and protocol installation.
        expected = ('(VOID)ImageHandle; (VOID)SystemTable; '
                    'DEBUG ((DEBUG_ERROR, "' + MARKER + r'\n")); '
                    'DEBUG ((DEBUG_ERROR, "' + DISABLED + r'\n")); '
                    'return EFI_SUCCESS;')
        body = code[code.index("{") + 1:code.rindex("}")]
        self.assertEqual(re.sub(r"\s+", "", body), re.sub(r"\s+", "", expected))
        self.assertEqual(code.count("{"), 1)
        self.assertEqual(code.count("}"), 1)
        self.assertEqual(source.count(MARKER), 1)
        self.assertNotRegex(code, r"\b(?:Mmio\w*|IoRead\w*|IoWrite\w*|Stall|"
                            r"MicroSecondDelay|NanoSecondDelay|dt_\w*|"
                            r"Install\w*Protocol\w*|for|while|goto)\s*\(")
        self.assertNotRegex(code, r"\b(?:gBS|gRT|asm|__asm__|volatile)\b")
        # Nothing executable may be hidden outside the sole entry body.
        signature = re.sub(r"#include[^\n]*", "", code[:code.index("{")])
        self.assertEqual(re.sub(r"\s+", " ", signature).strip(),
                         "EFI_STATUS EFIAPI AppleSpiHidDxeInitialize ( "
                         "IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable )")
        self.assertEqual(code[code.rindex("}") + 1:].strip(), "")

    def test_inf_minimal_dependencies(self):
        sections = {}
        current = None
        for line in INF.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("["):
                current = line
                self.assertNotIn(current, sections)
                sections[current] = []
            else:
                sections[current].append(line)
        self.assertEqual(set(sections), {"[Defines]", "[Sources]", "[Packages]",
                                         "[LibraryClasses]", "[Depex]"})
        self.assertEqual(sections["[Sources]"], [DRIVER.name])
        self.assertEqual(sections["[Packages]"], ["MdePkg/MdePkg.dec"])
        self.assertEqual(sections["[LibraryClasses]"], ["UefiDriverEntryPoint", "DebugLib"])
        self.assertEqual(sections["[Depex]"], ["TRUE"])
        self.assertIn("ENTRY_POINT                    = AppleSpiHidDxeInitialize",
                      sections["[Defines]"])

    def test_real_entry_host_print_path(self):
        arch = {"arm64": "AArch64", "aarch64": "AArch64", "x86_64": "X64",
                "AMD64": "X64"}.get(platform.machine())
        self.assertIsNotNone(arch, "unsupported host architecture")
        # Use real UEFI and DebugLib headers/macros, mock only DebugLib services.
        # The driver is compiled as a separate translation unit, not copied.
        harness = r'''
#include <Uefi.h>
#include <Library/DebugLib.h>
extern int printf(const char *, ...);
EFI_STATUS EFIAPI AppleSpiHidDxeInitialize(EFI_HANDLE, EFI_SYSTEM_TABLE *);
BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return TRUE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN Level) {
  return Level == DEBUG_ERROR;
}
VOID EFIAPI DebugPrint(UINTN Level, CONST CHAR8 *Format, ...) {
  (VOID)Level;
  printf("%s", Format);
}
int main(void) {
  return AppleSpiHidDxeInitialize(NULL, NULL) == EFI_SUCCESS ? 0 : 1;
}
'''
        QUARANTINE.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="host-test-", dir=QUARANTINE) as temp:
            temp = Path(temp)
            (temp / "harness.c").write_text(harness)
            binary = temp / "safe-entry"
            self.run_checked([self.compiler(), *self.flags(arch), str(DRIVER),
                              str(temp / "harness.c"), "-o", str(binary)])
            self.assertEqual(self.run_checked([str(binary)]), MARKER + "\n" + DISABLED + "\n")

    def test_real_aarch64_headers_syntax(self):
        self.run_checked([self.compiler(), "--target=aarch64-none-elf", "-ffreestanding",
                          *self.flags("AArch64"), "-fsyntax-only", str(DRIVER)])


if __name__ == "__main__":
    unittest.main(verbosity=2)
