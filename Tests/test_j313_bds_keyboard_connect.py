"""Compile production BDS keyboard binding helper; no physical-input claims."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c").read_text()
BODY = "static void" + chr(10) + SOURCE[SOURCE.index("ConnectPublishedJ313Keyboard ("):SOURCE.index(chr(10).join(["EFI_HANDLE", "EFIAPI", "DeviceBootManagerBeforeConsole ("]))]
MOCK = r'''
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define DEBUG(x) do {} while (0)
#define EFI_ERROR(x) ((x) != 0)
#define TPL_APPLICATION 4
#define ByProtocol 1
#define ConIn 1
#define HARDWARE_DEVICE_PATH 1
#define HW_VENDOR_DP 4
#define END_DEVICE_PATH_LENGTH 4
#define J313_KEYBOARD_DEVICE_PATH_GUID {42}
typedef int EFI_STATUS;
typedef size_t UINTN;
typedef void *EFI_HANDLE;
typedef struct { unsigned value; } EFI_GUID;
typedef struct { unsigned char Type, SubType; unsigned short Length; } EFI_DEVICE_PATH_PROTOCOL;
typedef struct { EFI_DEVICE_PATH_PROTOCOL Header; EFI_GUID Guid; } VENDOR_DEVICE_PATH;
typedef struct { VENDOR_DEVICE_PATH Vendor; EFI_DEVICE_PATH_PROTOCOL End; } PATH;
static PATH path;
static int tpl=4, fail_lookup, fail_update, fail_connect, no_path;
static int lookups, updates, connects, frees, gEfiSimpleTextInProtocolGuid;
static unsigned EfiGetCurrentTpl(void) { return tpl; }
#define DevicePathType(p) ((p)->Type)
#define DevicePathSubType(p) ((p)->SubType)
#define DevicePathNodeLength(p) ((p)->Length)
#define NextDevicePathNode(p) ((EFI_DEVICE_PATH_PROTOCOL *)((char *)(p)+(p)->Length))
#define IsDevicePathEnd(p) ((p)->Type == 127 && (p)->SubType == 255)
static int CompareGuid(const EFI_GUID *a, const EFI_GUID *b) { return a->value == b->value; }
static EFI_DEVICE_PATH_PROTOCOL *DevicePathFromHandle(void *h) {
  assert(h == &path); return no_path ? NULL : &path.Vendor.Header;
}
static int locate(int kind, void *guid, void *reg, size_t *count, void ***out) {
  assert(kind == ByProtocol && guid == &gEfiSimpleTextInProtocolGuid && !reg);
  lookups++; if (fail_lookup) return 14;
  *out = malloc(sizeof(void *)); assert(*out); (*out)[0] = &path; *count = 1; return 0;
}
static int EfiBootManagerUpdateConsoleVariable(int kind, void *p, void *remove) {
  assert(kind == ConIn && p == &path && !remove); updates++; return fail_update;
}
static int connect(void *h, void *drivers, void *remaining, int recursive) {
  assert(h == &path && !drivers && !remaining && recursive && updates && !fail_update);
  connects++; return fail_connect;
}
static void FreePool(void *p) { frees++; free(p); }
static struct {
  int (*LocateHandleBuffer)(int, void *, void *, size_t *, void ***);
  int (*ConnectController)(void *, void *, void *, int);
} services = {locate, connect}, *gBS = &services;
'''
MAIN = r'''
int main(void) {
  for (int scenario=0; scenario<12; scenario++) {
    tpl=4; fail_lookup=fail_update=fail_connect=no_path=0;
    lookups=updates=connects=frees=0;
    path=(PATH){{{1,4,sizeof(VENDOR_DEVICE_PATH)},{42}},{127,255,4}};
    switch(scenario) {
      case 1: tpl=8; break;
      case 2: fail_lookup=1; break;
      case 3: no_path=1; break;
      case 4: path.Vendor.Header.Type=2; break;
      case 5: path.Vendor.Header.SubType=5; break;
      case 6: path.Vendor.Header.Length=4; break;
      case 7: path.Vendor.Guid.value=43; break;
      case 8: path.End.Type=1; break;
      case 9: path.End.Length=8; break;
      case 10: fail_update=1; break;
      case 11: fail_connect=1; break;
    }
    ConnectPublishedJ313Keyboard();
    assert(lookups == (scenario!=1));
    assert(frees == (scenario!=1 && scenario!=2));
    assert(updates == (scenario==0 || scenario==10 || scenario==11));
    assert(connects == (scenario==0 || scenario==11));
    if (scenario==0) {
      ConnectPublishedJ313Keyboard();
      assert(updates==2 && connects==2 && frees==2);
    }
  }
  return 0;
}
'''
class BdsKeyboardConnectTests(unittest.TestCase):
    def test_real_uefi_aarch64_headers(self):
        headers = ["Uefi.h", "Guid/J313Keyboard.h", "Protocol/SimpleTextIn.h",
                   "Library/BaseMemoryLib.h", "Library/DebugLib.h",
                   "Library/DevicePathLib.h", "Library/MemoryAllocationLib.h",
                   "Library/UefiBootManagerLib.h", "Library/UefiBootServicesTableLib.h",
                   "Library/UefiLib.h"]
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "real_headers.c"
            source.write_text(chr(10).join("#include <" + h + ">" for h in headers) +
                              chr(10) + BODY + chr(10) +
                              "VOID Probe(VOID) { ConnectPublishedJ313Keyboard(); }")
            includes = ["MU_BASECORE/MdePkg/Include", "MU_BASECORE/MdePkg/Include/AArch64",
                        "MU_BASECORE/MdeModulePkg/Include", "Silicon/Apple/AppleSiliconPkg/Include"]
            flags = [arg for inc in includes for arg in ("-I", str(ROOT / inc))]
            subprocess.run(["clang", "--target=aarch64-none-elf", "-ffreestanding",
                            "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar",
                            "-fsyntax-only", *flags, str(source)], check=True)

    def test_production_helper(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, exe = Path(tmp)/"connect.c", Path(tmp)/"connect"
            source.write_text(MOCK + BODY + MAIN)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=5)

if __name__ == "__main__":
    unittest.main()
