"""Compile the production SWM gate with strict host mocks; no firmware build needed."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib"
SOURCE = (LIB / "DeviceBootManagerLib.c").read_text()
GATE = SOURCE[SOURCE.index("// SWM boot gate:"):SOURCE.index("static EFI_EVENT  mPreReadyToBootEvent;")]

MOCK = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define STATIC static
#define VOID void
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 3
#define EFI_DEVICE_ERROR 7
#define EFI_NOT_FOUND 14
#define EFI_TIMEOUT 18
#define EFI_ERROR(s) ((s) != EFI_SUCCESS)
#define DEBUG(x) do {} while (0)
#define EVT_TIMER 0x80000000U
#define TPL_APPLICATION 4
#define TPL_CALLBACK 8
#define TPL_NOTIFY 16
#define TPL_HIGH_LEVEL 31
#define TimerPeriodic 1
#define TimerCancel 0
#define TimerRelative 2
#define EFIAPI
#define IN
#define OUT
typedef int EFI_STATUS;
typedef unsigned EFI_TPL;
typedef size_t UINTN;
typedef void *EFI_EVENT;
typedef int MS_SIMPLE_WINDOW_MANAGER_PROTOCOL;
static int gMsSWMProtocolGuid, token, event_token;
static unsigned tpl = TPL_APPLICATION;
static int creates, sets, waits, closes, locates, live, raises, restores;
static int ready_at = -1, create_error, set_error, wait_error, close_error;
static int published, stage2_complete;
static int locate_error_at = -1;
static EFI_TPL raise_tpl(EFI_TPL to) {
  EFI_TPL old = tpl; assert(to == TPL_HIGH_LEVEL); assert(to >= old);
  tpl = to; raises++; return old;
}
static void restore_tpl(EFI_TPL old) {
  assert(tpl == TPL_HIGH_LEVEL); tpl = old; restores++;
}
static EFI_TPL EfiGetCurrentTpl(void) {
  EFI_TPL old = raise_tpl(TPL_HIGH_LEVEL); restore_tpl(old); return old;
}
static EFI_STATUS locate(void *guid, void *registration, void **out) {
  assert(tpl == TPL_APPLICATION); assert(guid == &gMsSWMProtocolGuid);
  assert(registration == NULL); locates++;
  if (waits == locate_error_at) return EFI_DEVICE_ERROR;
  if (published) { assert(stage2_complete); *out = &token; return EFI_SUCCESS; }
  return EFI_NOT_FOUND;
}
static EFI_STATUS create(unsigned type, EFI_TPL notify_tpl,
                         void (*notify)(EFI_EVENT, void *), void *ctx, EFI_EVENT *out) {
  assert(tpl == TPL_APPLICATION); assert(type == EVT_TIMER);
  assert(notify_tpl == TPL_APPLICATION); assert(!notify && !ctx);
  assert(*out == NULL); creates++;
  if (create_error) return EFI_DEVICE_ERROR;
  assert(!live); live = 1; *out = &event_token; return EFI_SUCCESS;
}
static EFI_STATUS set(EFI_EVENT event, int type, uint64_t interval) {
  assert(tpl == TPL_APPLICATION && live && event == &event_token);
  assert(type == TimerPeriodic && interval == 1000000ULL); sets++;
  return set_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS wait_event(UINTN count, EFI_EVENT *events, UINTN *index) {
  assert(tpl == TPL_APPLICATION && count == 1 && live);
  assert(events[0] == &event_token); waits++;
  // Real WaitForEvent allows TPL_CALLBACK probes to run to completion.
  tpl = TPL_CALLBACK;
  if (!wait_error && ready_at >= 0 && waits >= ready_at) {
    published = 1;  // SWM installation precedes Stage2 completion.
    stage2_complete = 1;
  }
  tpl = TPL_APPLICATION;
  *index = 0; return wait_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS close_event(EFI_EVENT event) {
  assert(tpl == TPL_APPLICATION && live && event == &event_token); closes++;
  // A broken service may refuse close: the gate must fail closed, not retry/UI.
  if (close_error) return EFI_DEVICE_ERROR;
  live = 0; return EFI_SUCCESS;
}
static struct {
  EFI_STATUS (*LocateProtocol)(void *, void *, void **);
  EFI_STATUS (*CreateEvent)(unsigned, EFI_TPL, void (*)(EFI_EVENT,void *), void *, EFI_EVENT *);
  EFI_STATUS (*SetTimer)(EFI_EVENT, int, uint64_t);
  EFI_STATUS (*WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
  EFI_STATUS (*CloseEvent)(EFI_EVENT);
} services = {locate, create, set, wait_event, close_event}, *gBS = &services;
'''

MAIN = r'''
int main(int argc, char **argv) {
  assert(argc == 2); int scenario = atoi(argv[1]);
  int expected = EFI_SUCCESS, expected_waits = 0;
  switch (scenario) {
    case 0: ready_at = 0; break;
    case 1: ready_at = 3; expected_waits = 3; break;
    case 2: expected = EFI_TIMEOUT; expected_waits = SWM_BOOT_GATE_MAX_TICKS; break;
    case 3: create_error = 1; expected = EFI_DEVICE_ERROR; break;
    case 4: set_error = 1; expected = EFI_DEVICE_ERROR; break;
    case 5: wait_error = 1; expected = EFI_DEVICE_ERROR; expected_waits = 1; break;
    case 6: close_error = 1; ready_at = 1; expected = EFI_DEVICE_ERROR; expected_waits = 1; break;
    case 7: tpl = TPL_CALLBACK; ready_at = 0; expected = EFI_UNSUPPORTED; break;
    case 8: tpl = TPL_NOTIFY; expected = EFI_UNSUPPORTED; break;
    case 9: tpl = TPL_HIGH_LEVEL; expected = EFI_UNSUPPORTED; break;
    case 10: locate_error_at = 0; expected = EFI_DEVICE_ERROR; break;
    case 11: locate_error_at = 2; expected = EFI_DEVICE_ERROR; expected_waits = 2; break;
    case 12: ready_at = SWM_BOOT_GATE_MAX_TICKS; expected_waits = SWM_BOOT_GATE_MAX_TICKS; break;
    case 13: ready_at = SWM_BOOT_GATE_MAX_TICKS + 1; expected = EFI_TIMEOUT;
             expected_waits = SWM_BOOT_GATE_MAX_TICKS; break;
    case 14: set_error = close_error = 1; expected = EFI_DEVICE_ERROR; break;
    case 15: close_error = 1; expected = EFI_DEVICE_ERROR;
             expected_waits = SWM_BOOT_GATE_MAX_TICKS; break;
    default: abort();
  }
  unsigned old_tpl = tpl;
  if (ready_at == 0) published = stage2_complete = 1;
  assert(WaitForSwmReady() == expected);
  assert(tpl == old_tpl && raises == 1 && restores == 1);
  assert(waits == expected_waits);
  if (old_tpl != TPL_APPLICATION) {
    assert(!creates && !sets && !locates && !closes);
  } else if (scenario == 0 || scenario == 10) {
    assert(locates == 1 && !creates && !sets && !closes);
  } else if (scenario == 3) {
    assert(creates == 1 && !sets && !closes && !live);
  } else {
    assert(creates == 1 && sets == 1 && closes == 1);
    assert(live == !!close_error);
    assert(locates == 1 + waits - !!wait_error);
  }
  return 0;
}
'''


class SwmBootGateTests(unittest.TestCase):
    def test_compiled_production_helper(self):
        # Missing compiler is an error, never a silently skipped validation.
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory(prefix="swm-boot-gate-") as tmp:
            src, binary = Path(tmp) / "gate.c", Path(tmp) / "gate"
            src.write_text(MOCK + GATE + MAIN)
            for ticks in (20, 4):
                subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                     f"-DSWM_BOOT_GATE_MAX_TICKS={ticks}",
                                     str(src), "-o", str(binary)], check=True, text=True)
                for scenario in range(16):
                    with self.subTest(ticks=ticks, scenario=scenario):
                        subprocess.run([str(binary), str(scenario)], check=True, timeout=5)
            for invalid in (0, 21):
                result = subprocess.run(cc + ["-std=c11", f"-DSWM_BOOT_GATE_MAX_TICKS={invalid}",
                                             "-fsyntax-only", str(src)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("must be between 1 and 20", result.stderr)

    def test_only_ui_callsites_are_gated(self):
        priority = SOURCE[SOURCE.index("\nDeviceBootManagerPriorityBoot ("):SOURCE.index("\nDeviceBootManagerUnableToBoot (")]
        unable = SOURCE[SOURCE.index("\nDeviceBootManagerUnableToBoot ("):]
        self.assertEqual(SOURCE.count("Status = WaitForSwmReady ();"), 2)
        front = priority[priority.index("} else if (FrontPageBoot) {"):]
        self.assertNotIn("WaitForSwmReady", priority[:priority.index("} else if (FrontPageBoot) {")])
        for body, return_statement in ((front, "return Status;"), (unable, "return;")):
            self.assertRegex(body, re.compile(
                r"Status = WaitForSwmReady \(\);\s*if \(EFI_ERROR \(Status\)\) \{"
                r"[^}]*" + re.escape(return_statement) + r"\s*\}\s*"
                r"Status = MsBootOptionsLibGetBootManagerMenu", re.S))
        # Ignore historical explanatory comments when checking reset calls.
        executable = re.sub(r"//[^\n]*", "", unable)
        self.assertNotIn("RebootToFrontPage (", executable)
        self.assertLess(unable.index("WaitForSwmReady ();"), unable.index("EfiBootManagerBoot ("))

    def test_dependencies_and_no_blocking_side_effects(self):
        inf = (LIB / "DeviceBootManagerLib.inf").read_text()
        self.assertIn("#include <Protocol/SimpleWindowManager.h>", SOURCE)
        self.assertRegex(inf, r"(?m)^  UefiLib$")
        self.assertRegex(inf, r"gMsSWMProtocolGuid\s+## CONSUMES")
        for forbidden in ("Stall (", "ConnectAll", "SetVariable", "ResetSystem", "RebootToFrontPage"):
            self.assertNotIn(forbidden, GATE)
        self.assertIn("EfiGetCurrentTpl () != TPL_APPLICATION", GATE)


if __name__ == "__main__":
    unittest.main()
