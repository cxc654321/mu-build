"""Pinned-source patch tests and compiled real lazy-ConIn callback (not hardware)."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("menu_trace", ROOT / "Tools/j313-menu-trace.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


class MenuTraceTests(unittest.TestCase):
    def fixture(self, root):
        for name in TRACE.TARGETS:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())

    def test_apply_check_idempotent_and_unchanged_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            original = {name: (root / name).read_bytes() for name in TRACE.TARGETS}
            TRACE.patch_tree(root)
            self.assertEqual(original, {n: (root / n).read_bytes() for n in original})
            TRACE.patch_tree(root, True)
            patched = {name: (root / name).read_bytes() for name in original}
            TRACE.patch_tree(root, True)
            for name, raw in patched.items():
                self.assertEqual(raw, (root / name).read_bytes())
                self.assertEqual(TRACE.BLOCK.sub('', raw.decode()).encode(), original[name])
                self.assertNotIn('Stall', ''.join(TRACE.BLOCK.findall(raw.decode())))
            boot = patched[next(n for n in patched if n.endswith('BmBoot.c'))].decode()
            self.assertLess(boot.index('START-BEGIN'), boot.index('Status = gBS->StartImage'))
            self.assertLess(boot.index('Status = gBS->StartImage'), boot.index('START-END'))

    def test_dirty_final_target_rejects_without_partial_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            last = root / list(TRACE.TARGETS)[-1]
            last.write_bytes(last.read_bytes() + b'// user edit\n')
            before = {n: (root / n).read_bytes() for n in TRACE.TARGETS}
            with self.assertRaises(ValueError):
                TRACE.patch_tree(root, True)
            self.assertEqual(before, {n: (root / n).read_bytes() for n in before})

    def test_modified_marker_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            TRACE.patch_tree(root, True)
            first = root / next(iter(TRACE.TARGETS))
            first.write_text(first.read_text().replace('BOOT-ENTER', 'BROKEN-ENTER'))
            with self.assertRaises(ValueError):
                TRACE.patch_tree(root, True)

    def test_compile_actual_callback_warning_returns(self):
        name = next(n for n in TRACE.TARGETS if n.endswith('BdsEntry.c'))
        text = TRACE.transform(name, (ROOT / name).read_text())
        start = text.index('BdsDxeOnConnectConInCallBack (')
        end = text.index('\n}\n', start) + 3
        body = 'void\n' + text[start:end]
        mock = r'''
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define IN
#define VOID void
#define EFI_EVENT void *
#define EFI_STATUS int
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG_WARN 2
#define DEBUG(x) log_debug x
#define ConIn 0
static int phase, result, warnings;
static void log_debug(int level, const char *fmt, ...) {
  (void)level;
  if (strstr(fmt, "CALLBACK-BEGIN")) { assert(phase == 0); phase = 1; }
  else if (strstr(fmt, "PLATFORM-END")) { assert(phase == 2); phase = 3; }
  else if (strstr(fmt, "CONNECT-END")) { assert(phase == 4); phase = 5; }
  else if (strstr(fmt, "Connect ConIn failed")) { assert(phase == 5); warnings++; }
  else if (strstr(fmt, "CALLBACK-END")) { assert(phase == 5); phase = 6; }
  else assert(0);
}
static void PlatformBootManagerOnDemandConInConnect(void) { assert(phase == 1); phase = 2; }
static int EfiBootManagerConnectConsoleVariable(int con) { assert(con == 0 && phase == 3); phase = 4; return result; }
'''
        main = r'''
int main(void) {
  for (int i = 0; i < 2; ++i) {
    phase = warnings = 0; result = i ? 14 : 0;
    BdsDxeOnConnectConInCallBack(0, 0);
    assert(phase == 6 && warnings == i);
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source, exe = Path(directory) / 'callback.c', Path(directory) / 'callback'
            source.write_text(mock + body + main)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
