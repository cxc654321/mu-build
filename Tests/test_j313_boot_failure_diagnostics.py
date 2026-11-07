"""Compile the actual terminal boot callback with host mocks, not hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c").read_text()
BODY = SOURCE[SOURCE.index("VOID\nEFIAPI\nDeviceBootManagerUnableToBoot ("):]
MOCK = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#define VOID void
#define EFIAPI
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG(x) record x
#define EFI_STATUS int
typedef struct { int Status; } EFI_BOOT_MANAGER_LOAD_OPTION;
static int gate_status, menu_status, gates, gets, boots, frees;
static const char *logs[8];
static unsigned nlogs;
static void record(int level, const char *format, ...) {
  assert(level == DEBUG_ERROR); assert(nlogs < 8); logs[nlogs++] = format;
}
static int WaitForSwmReady(void) { gates++; return gate_status; }
static int MsBootOptionsLibGetBootManagerMenu(EFI_BOOT_MANAGER_LOAD_OPTION *o, void *p) {
  assert(o && !p); gets++; return menu_status;
}
static void EfiBootManagerBoot(EFI_BOOT_MANAGER_LOAD_OPTION *o) { assert(o); boots++; }
static void EfiBootManagerFreeLoadOption(EFI_BOOT_MANAGER_LOAD_OPTION *o) { assert(o); frees++; }
'''
MAIN = r'''
int main(void) {
  gate_status=18;
  DeviceBootManagerUnableToBoot();
  assert(gates==1 && gets==0 && boots==0 && frees==0 && nlogs==2);
  assert(strstr(logs[0], "NO-BOOT-OPTION") && strstr(logs[1], "SWM-GATE-FAILED"));
  assert(strstr(logs[1], "final halt"));
  gates=gets=boots=frees=nlogs=0; gate_status=0; menu_status=14;
  DeviceBootManagerUnableToBoot();
  assert(gates==1 && gets==1 && boots==0 && frees==0 && nlogs==2);
  assert(strstr(logs[1], "MENU-NOT-FOUND") && strstr(logs[1], "final halt"));
  gates=gets=boots=frees=nlogs=0; menu_status=0;
  DeviceBootManagerUnableToBoot();
  assert(gates==1 && gets==1 && boots==1 && frees==1 && nlogs==3);
  assert(strstr(logs[1], "MENU-START") && strstr(logs[2], "MENU-RETURNED"));
  assert(strstr(logs[2], "final halt"));
  return 0;
}
'''

class BootDiagnosticsTests(unittest.TestCase):
    def test_actual_callback(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "test.c"
            binary = Path(tmp) / "test"
            source.write_text(MOCK + BODY + MAIN)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_pinned_bds_final_halt_contract(self):
        bds = (ROOT / "MU_BASECORE/MdeModulePkg/Universal/BdsDxe/BdsEntry.c").read_text()
        tail = bds[bds.index("PlatformBootManagerUnableToBoot ();"):]
        self.assertTrue(" ".join(tail.split()).startswith("PlatformBootManagerUnableToBoot (); CpuDeadLoop ();"))
        self.assertNotIn("which loops and calls this again", BODY)

if __name__ == "__main__":
    unittest.main()
