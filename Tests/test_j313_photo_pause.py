"""Compile real photo helper + terminal callback; mock Stall never sleeps."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c").read_text()
HELPER = SOURCE[SOURCE.index("// User-requested photo/log diagnostic build."):SOURCE.index("/** Observe UI protocols")]
BODY = SOURCE[SOURCE.index("VOID\nEFIAPI\nDeviceBootManagerUnableToBoot ("):]
MOCK = r'''
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define VOID void
#define IN
#define CONST const
#define CHAR8 char
#define EFIAPI
#define EFI_STATUS int
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG(x) record x
#define TPL_APPLICATION 4
static unsigned tpl, samples, stalls, n;
static int gate_status, menu_status;
static char events[32][160];
typedef struct { int Status; } EFI_BOOT_MANAGER_LOAD_OPTION;
static void event(const char *s) {
  assert(n < 32); snprintf(events[n++], 160, "%s", s);
}
static unsigned EfiGetCurrentTpl(void) { samples++; return tpl; }
static int stall(unsigned long us) {
  assert(tpl == TPL_APPLICATION && us == 15000000); stalls++;
  event("stall"); return 0;
}
static struct { int (*Stall)(unsigned long); } services = {stall}, *gBS = &services;
static void record(int level, const char *fmt, ...) {
  assert(level == DEBUG_ERROR);
  va_list ap; va_start(ap, fmt);
  if (strstr(fmt, "[J313-PHOTO]")) {
    const char *stage = va_arg(ap, const char *);
    char text[160];
    assert(strstr(fmt, "15"));
    const char *action = strstr(fmt, "skip") ? "skip" : strstr(fmt, "continue") ? "continue" : "pause";
    if (!strcmp(action, "skip")) assert(strstr(fmt, "continue"));
    snprintf(text, sizeof(text), "%s:%s", stage, action); event(text);
  } else if (strstr(fmt, "NO-BOOT-OPTION")) event("NO-BOOT-OPTION");
  else if (strstr(fmt, "SWM-GATE-FAILED")) event("SWM-GATE-FAILED");
  else if (strstr(fmt, "MENU-NOT-FOUND")) event("MENU-NOT-FOUND");
  else if (strstr(fmt, "MENU-START")) event("MENU-START");
  else if (strstr(fmt, "MENU-CALL BEGIN")) event("MENU-CALL");
  else if (strstr(fmt, "MENU-CALL END")) event("MENU-CALL-END");
  else if (strstr(fmt, "MENU-STATUS")) {
    assert(va_arg(ap, int) == 7); event("MENU-STATUS");
  } else if (strstr(fmt, "MENU-RETURNED")) event("MENU-RETURNED");
  else assert(0);
  va_end(ap);
}
static void TraceGuiProtocols(void) { event("trace"); }
static int WaitForSwmReady(void) { event("gate"); return gate_status; }
static int MsBootOptionsLibGetBootManagerMenu(EFI_BOOT_MANAGER_LOAD_OPTION *o, void *p) {
  assert(o && !p); event("get-menu"); return menu_status;
}
static void EfiBootManagerBoot(EFI_BOOT_MANAGER_LOAD_OPTION *o) { event("boot"); o->Status = 7; }
static void EfiBootManagerFreeLoadOption(EFI_BOOT_MANAGER_LOAD_OPTION *o) {
  assert(o->Status == 7); event("free");
}
'''
MAIN = r'''
static unsigned cursor;
static void expect(const char *s) {
  if (cursor >= n || strcmp(events[cursor], s)) {
    fprintf(stderr, "event %u expected %s, got %s\n", cursor, s, cursor < n ? events[cursor] : "<end>");
    assert(0);
  }
  cursor++;
}
static void expect_pause(const char *stage) {
  char text[160];
#if J313_PHOTO_PAUSE_ENABLED
  if (tpl == TPL_APPLICATION) {
    snprintf(text, sizeof(text), "%s:pause", stage); expect(text);
    expect("stall");
    snprintf(text, sizeof(text), "%s:continue", stage); expect(text);
  } else {
    snprintf(text, sizeof(text), "%s:skip", stage); expect(text);
  }
#else
  (void)stage; (void)text;
#endif
}
int main(void) {
  // Application, callback, notify and high TPL; both failures and returned boot.
  unsigned levels[] = {4, 8, 16, 31};
  for (unsigned l = 0; l < 4; l++) for (int path = 0; path < 3; path++) {
    tpl = levels[l]; n = cursor = stalls = samples = 0;
    gate_status = path == 0 ? 18 : 0;
    menu_status = path == 1 ? 14 : 0;
    DeviceBootManagerUnableToBoot();
    expect("NO-BOOT-OPTION"); expect_pause("NO-BOOT-OPTION"); expect("gate");
    if (path == 0) {
      expect("SWM-GATE-FAILED"); expect_pause("SWM-GATE-FAILED");
    } else {
      expect("get-menu");
      if (path == 1) {
        expect("MENU-NOT-FOUND"); expect_pause("MENU-NOT-FOUND");
      } else {
        expect("trace"); expect("MENU-START"); expect_pause("MENU-START");
        expect("MENU-CALL"); expect("boot"); expect("MENU-CALL-END"); expect("MENU-STATUS"); expect_pause("MENU-STATUS");
        expect("free"); expect("MENU-RETURNED");
      }
    }
    assert(cursor == n);
#if J313_PHOTO_PAUSE_ENABLED
    assert(samples == (path == 2 ? 3U : 2U));
    assert(stalls == (tpl == TPL_APPLICATION ? samples : 0));
#else
    assert(!samples && !stalls);
#endif
  }
  // Keep strict compilation checking the mock signatures in the disabled build.
  (void)EfiGetCurrentTpl; (void)gBS;
  return 0;
}
'''


class PhotoPauseTests(unittest.TestCase):
    def test_actual_helper_and_callback_enabled_and_disabled(self):
        with tempfile.TemporaryDirectory(prefix="j313-photo-") as tmp:
            src, exe = Path(tmp) / "photo.c", Path(tmp) / "photo"
            src.write_text(MOCK + HELPER + BODY + MAIN)
            for flags in ([], ["-DJ313_PHOTO_PAUSE_ENABLED=0"]):
                with self.subTest(flags=flags):
                    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] + flags +
                                   [str(src), "-o", str(exe)], check=True)
                    subprocess.run([str(exe)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
