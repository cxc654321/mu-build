"""Compile actual GUI diagnostic/UnableToBoot bodies; not hardware evidence."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c").read_text()
TRACE = "static void\n" + SOURCE[SOURCE.index("TraceGuiProtocols ("):SOURCE.index("/**" + chr(10) + " This is called from BDS right before going into front page")]
UNABLE = "void\n" + SOURCE[SOURCE.index("DeviceBootManagerUnableToBoot ("):]
MOCK = r'''
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#define VOID void
#define EFI_NOT_FOUND 14
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG(x) log_debug x
typedef int EFI_STATUS;
typedef struct { int Status; } EFI_BOOT_MANAGER_LOAD_OPTION;
static int gMsSWMProtocolGuid, gMsOSKProtocolGuid, gEfiSimpleTextInputExProtocolGuid;
static int token, present, gate_status, menu_status, boot_status;
static int locates, handles, boots, frees, traces, statuses, starts;
static struct { void *ConsoleInHandle; } table, *gST = &table;
static void log_debug(int level, const char *fmt, ...) {
  (void)level;
  va_list args; va_start(args, fmt);
  if (strstr(fmt, "GUI-PROTOCOLS")) {
    assert(va_arg(args, int) == (present ? 0 : 14));
    assert(va_arg(args, int) == (present ? 0 : 14));
    assert(va_arg(args, void *) == table.ConsoleInHandle);
    assert(va_arg(args, int) == (present && table.ConsoleInHandle ? 0 : 14));
    traces++;
  }
  if (strstr(fmt, "MENU-START")) { assert(traces == 1); starts++; }
  if (strstr(fmt, "MENU-STATUS")) {
    assert(boots == 1 && !frees);
    assert(va_arg(args, int) == boot_status); statuses++;
  }
  va_end(args);
}
static int locate(void *guid, void *registration, void **out) {
  assert(guid == &gMsSWMProtocolGuid || guid == &gMsOSKProtocolGuid);
  assert(!registration && !*out); locates++;
  if (present) *out = &token;
  return present ? 0 : 14;
}
static int handle(void *h, void *guid, void **out) {
  assert(h == &token && guid == &gEfiSimpleTextInputExProtocolGuid && !*out);
  handles++; if (present) *out = &token;
  return present ? 0 : 14;
}
static struct {
  int (*LocateProtocol)(void *, void *, void **);
  int (*HandleProtocol)(void *, void *, void **);
} services = {locate, handle}, *gBS = &services;
static int WaitForSwmReady(void) { return gate_status; }
static int MsBootOptionsLibGetBootManagerMenu(EFI_BOOT_MANAGER_LOAD_OPTION *o, void *p) {
  assert(!p); o->Status = 0; return menu_status;
}
static void EfiBootManagerBoot(EFI_BOOT_MANAGER_LOAD_OPTION *o) {
  assert(starts == 1); boots++; o->Status = boot_status;
}
static void EfiBootManagerFreeLoadOption(EFI_BOOT_MANAGER_LOAD_OPTION *o) {
  assert(statuses == 1); frees++; o->Status = -1;
}
'''
MAIN = r'''
int main(void) {
  for (int scenario = 0; scenario < 6; scenario++) {
    locates = handles = boots = frees = traces = statuses = starts = 0;
    present = scenario == 0;
    table.ConsoleInHandle = scenario == 2 ? NULL : &token;
    gate_status = scenario == 3 ? 18 : 0;
    menu_status = scenario == 4 ? 14 : 0;
    boot_status = scenario == 5 ? 3 : 0;
    DeviceBootManagerUnableToBoot();
    if (gate_status || menu_status) {
      assert(!locates && !handles && !boots && !frees && !traces);
    } else {
      assert(locates == 2 && handles == (scenario != 2));
      assert(boots == 1 && frees == 1 && statuses == 1 && traces == 1);
    }
  }
  return 0;
}
'''

class GuiDiagnosticsTests(unittest.TestCase):
    def test_actual_production_bodies(self):
        with tempfile.TemporaryDirectory(prefix="j313-gui-") as tmp:
            src, exe = Path(tmp) / "gui.c", Path(tmp) / "gui"
            src.write_text(MOCK + TRACE + UNABLE + MAIN)
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                           ["-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=5)

if __name__ == "__main__":
    unittest.main()
