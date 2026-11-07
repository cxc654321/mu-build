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
#include <stdint.h>
#define VOID void
#define STATIC static
#define CONST const
#define CHAR8 char
#define TRUE 1
#define FALSE 0
#define EFI_NOT_FOUND 14
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG(x) log_debug x
#define ByProtocol 0
#define HARDWARE_DEVICE_PATH 1
#define HW_VENDOR_DP 4
#define END_DEVICE_PATH_LENGTH 4
#define J313_KEYBOARD_DEVICE_PATH_GUID 42
typedef int EFI_STATUS, EFI_GUID, BOOLEAN;
typedef size_t UINTN;
typedef uint32_t UINT32;
typedef void *EFI_HANDLE;
typedef struct { unsigned Type, SubType, Length; } EFI_DEVICE_PATH_PROTOCOL;
typedef struct { EFI_DEVICE_PATH_PROTOCOL Header; EFI_GUID Guid; } VENDOR_DEVICE_PATH;
static struct { VENDOR_DEVICE_PATH Vendor; EFI_DEVICE_PATH_PROTOCOL End; } path;
#define DevicePathType(p) ((p)->Type)
#define DevicePathSubType(p) ((p)->SubType)
#define DevicePathNodeLength(p) ((p)->Length)
#define NextDevicePathNode(p) ((EFI_DEVICE_PATH_PROTOCOL *)((char *)(p) + (p)->Length))
#define IsDevicePathEnd(p) ((p)->Type == 127)
#define CompareGuid(a,b) (*(a) == *(b))
typedef struct { UINT32 HorizontalResolution, VerticalResolution; } EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct { UINT32 MaxMode, Mode; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info; UINTN SizeOfInfo; } GOP_MODE;
typedef struct { GOP_MODE *Mode; } EFI_GRAPHICS_OUTPUT_PROTOCOL;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static GOP_MODE mode;
static EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
typedef struct { int Status; } EFI_BOOT_MANAGER_LOAD_OPTION;
static int gMsSWMProtocolGuid, gMsOSKProtocolGuid, gEfiSimpleTextInputExProtocolGuid;
static int gEfiGraphicsOutputProtocolGuid, gEfiHiiFontProtocolGuid, gEfiFormBrowser2ProtocolGuid;
static int gEdkiiFormDisplayEngineProtocolGuid, gEfiSimpleTextInProtocolGuid;
static int token, scenario, gate_status, menu_status, boot_status;
static int locates, handles, boots, frees, traces, statuses, starts, scans, pools, begun, ended;
static EFI_HANDLE providers[65];
static struct { void *ConsoleInHandle; } table, *gST = &table;
static void J313PhotoPause(const char *stage) {
  if (!strcmp(stage, "MENU-START")) assert(traces == 8 && !begun);
}
static void log_debug(int level, const char *fmt, ...) {
  (void)level;
  va_list args; va_start(args, fmt);
  if (strstr(fmt, "[J313-GUI]")) {
    const char *name = strstr(fmt, "J313-keyboard-provider") ? "provider" : va_arg(args, const char *);
    const char *result = va_arg(args, const char *);
    const char *expected = "PASS";
    if (scenario == 1) expected = "MISSING";
    if (scenario == 2 || scenario == 3) expected = "INVALID";
    if (scenario >= 4 && scenario <= 8 && !strcmp(name, "GOP")) expected = "INVALID";
    if (scenario == 9 && !strcmp(name, "TextInEx(ConsoleIn)")) expected = "MISSING";
    if (scenario == 10 && !strcmp(name, "provider")) expected = "MISSING";
    if (scenario == 11 && !strcmp(name, "provider")) expected = "INVALID";
    assert(!strcmp(result, expected)); traces++;
  }
  if (strstr(fmt, "MENU-START")) { assert(traces == 8); starts++; }
  if (strstr(fmt, "MENU-CALL BEGIN")) { assert(starts == 1 && !boots); begun++; }
  if (strstr(fmt, "MENU-CALL END")) { assert(boots == 1); ended++; }
  if (strstr(fmt, "MENU-STATUS")) {
    assert(boots == 1 && ended == 1 && !frees);
    assert(va_arg(args, int) == boot_status); statuses++;
  }
  va_end(args);
}
static int lookup(void *guid, void **out) {
  assert(!*out);
  if (scenario == 1) return 14;
  if (scenario == 2) return 7;
  if (scenario != 3) *out = guid == &gEfiGraphicsOutputProtocolGuid ? (void *)&gop : &token;
  return 0;
}
static int locate(void *guid, void *registration, void **out) {
  assert(!registration); locates++; return lookup(guid, out);
}
static int handle(void *h, void *guid, void **out) {
  assert(h == &token && guid == &gEfiSimpleTextInputExProtocolGuid);
  handles++; return lookup(guid, out);
}
static int locate_handles(int search, void *guid, void *registration, UINTN *count, EFI_HANDLE **out) {
  assert(!search && guid == &gEfiSimpleTextInProtocolGuid && !registration && !*out && !*count);
  if (scenario == 1) return 14;
  if (scenario == 2) return 7;
  if (scenario != 3) { *out = providers; *count = scenario == 11 ? 65 : 1; }
  return 0;
}
static EFI_DEVICE_PATH_PROTOCOL *DevicePathFromHandle(EFI_HANDLE h) {
  (void)h; scans++; return &path.Vendor.Header;
}
static void FreePool(void *p) { assert(p == providers); pools++; }
static struct {
  int (*LocateProtocol)(void *, void *, void **);
  int (*HandleProtocol)(void *, void *, void **);
  int (*LocateHandleBuffer)(int, void *, void *, UINTN *, EFI_HANDLE **);
} services = {locate, handle, locate_handles}, *gBS = &services;
static int WaitForSwmReady(void) { return gate_status; }
static int MsBootOptionsLibGetBootManagerMenu(EFI_BOOT_MANAGER_LOAD_OPTION *o, void *p) {
  assert(!p); o->Status = 0; return menu_status;
}
static void EfiBootManagerBoot(EFI_BOOT_MANAGER_LOAD_OPTION *o) {
  assert(starts == 1 && begun == 1); boots++; o->Status = boot_status;
}
static void EfiBootManagerFreeLoadOption(EFI_BOOT_MANAGER_LOAD_OPTION *o) {
  assert(statuses == 1); frees++; o->Status = -1;
}
'''
MAIN = r'''
int main(void) {
  for (scenario = 0; scenario < 15; scenario++) {
    locates = handles = boots = frees = traces = statuses = starts = scans = pools = begun = ended = 0;
    info = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){1920, 1080};
    mode = (GOP_MODE){1, 0, &info, sizeof(info)}; gop.Mode = &mode;
    if (scenario == 4) gop.Mode = NULL;
    if (scenario == 5) mode.Info = NULL;
    if (scenario == 6) mode.SizeOfInfo = 0;
    if (scenario == 7) mode.Mode = 1;
    if (scenario == 8) info.HorizontalResolution = 0;
    path.Vendor.Header = (EFI_DEVICE_PATH_PROTOCOL){1, 4, sizeof(VENDOR_DEVICE_PATH)};
    path.Vendor.Guid = scenario == 10 || scenario == 11 ? 99 : 42;
    path.End = (EFI_DEVICE_PATH_PROTOCOL){127, 255, 4};
    table.ConsoleInHandle = scenario == 9 ? NULL : &token;
    gate_status = scenario == 12 ? 18 : 0;
    menu_status = scenario == 13 ? 14 : 0;
    boot_status = scenario == 14 ? 3 : 0;
    DeviceBootManagerUnableToBoot();
    if (gate_status || menu_status) {
      assert(!locates && !handles && !boots && !frees && !traces);
    } else {
      assert(locates == 6 && handles == (scenario != 9));
      assert(boots == 1 && frees == 1 && statuses == 1 && traces == 8);
      assert(scans == (scenario == 11 ? 64 : scenario >= 1 && scenario <= 3 ? 0 : 1));
      assert(pools == !(scenario >= 1 && scenario <= 3));
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
