"""Pinned source and real callback tests; no hardware claims."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bypass", ROOT / "Tools/j313-conin-bypass.py")
B = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(B)

@unittest.skipUnless(all((ROOT / n).is_file() for n in B.TARGETS), "requires pinned submodules")
class Tests(unittest.TestCase):
    def fixture(self, root, traced=False):
        for n in B.TARGETS:
            p = root / n
            p.parent.mkdir(parents=True, exist_ok=True)
            raw = (ROOT / n).read_bytes()
            if traced:
                raw = B.TRACE.transform(n, raw.decode()).encode()
            p.write_bytes(raw)

    def snapshot(self, root):
        return {n: (root / n).read_bytes() for n in B.TARGETS}

    def test_check_apply_inverse_and_idempotence(self):
        for traced in (False, True):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                self.fixture(root, traced)
                original = self.snapshot(root)
                B.patch_tree(root)
                self.assertEqual(original, self.snapshot(root))
                B.patch_tree(root, True)
                patched = self.snapshot(root)
                B.patch_tree(root, True)
                self.assertEqual(patched, self.snapshot(root))
                for n, raw in patched.items():
                    self.assertEqual(B.unpatch(raw.decode()).encode(), original[n])
                    additions = ''.join(m[0] for m in B.BLOCK.finditer(raw.decode()))
                    for bad in ('Stall (', 'WaitForEvent', 'ReadKeyStroke', 'StartImage', 'InstallProtocol', 'SetVariable', 'ConnectConsoleVariable', 'ConnectAll ()', 'gST->ConIn'):
                        self.assertNotIn(bad, additions)
                    if n.endswith('FrontPage.c'):
                        self.assertNotIn(b'  EfiBootManagerConnectAll ();', raw)
                        for token in (b'gMsOSKProtocolGuid', b'gMsSWMProtocolGuid', b'InitializeFrontPageUI ()'):
                            self.assertIn(token, raw)

    def test_transform_preserves_lf_and_crlf(self):
        for n in B.TARGETS:
            lf = (ROOT / n).read_text()
            for traced in (False, True):
                source = B.TRACE.transform(n, lf) if traced else lf
                expected = B.transform(n, source)
                crlf = B.transform(n, source.replace("\n", "\r\n"))
                self.assertEqual(crlf, expected.replace("\n", "\r\n"))
                self.assertEqual(B.unpatch(crlf), source.replace("\n", "\r\n"))

    def test_fail_closed(self):
        for token in (None, b'repeat skipped', b'CONIN-CALLBACK-BEGIN'):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                self.fixture(root, True)
                if token:
                    B.patch_tree(root, True)
                    p = root / next(iter(B.TARGETS))
                    p.write_bytes(p.read_bytes().replace(token, b'tampered'))
                else:
                    p = root / list(B.TARGETS)[-1]
                    p.write_bytes(p.read_bytes() + b'// dirty\n')
                before = self.snapshot(root)
                with self.assertRaises(ValueError):
                    B.patch_tree(root, True)
                self.assertEqual(before, self.snapshot(root))
        with self.assertRaises(ValueError):
            B.patch_tree(ROOT, device='j314')

    def test_compiled_callback_first_repeat_and_nested(self):
        n = next(n for n in B.TARGETS if n.endswith('BdsEntry.c'))
        for traced in (False, True):
            text = (ROOT / n).read_text()
            if traced:
                text = B.TRACE.transform(n, text)
            text = B.transform(n, text)
            start = text.index('BdsDxeOnConnectConInCallBack (')
            body = 'void\n' + text[start:text.index('\n}\n', start) + 3]
            mock = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define IN
#define VOID void
#define EFI_EVENT void *
#define EFI_STATUS int
#define STATIC static
#define BOOLEAN int
#define FALSE 0
#define TRUE 1
#define EFI_ERROR(s) ((s) != 0)
#define DEBUG_ERROR 1
#define DEBUG_WARN 2
#define DEBUG(x) log_debug x
#define ConIn 0
static int result, platform_calls, console_calls, warnings, skipped, began, returned;
static void log_debug(int level, const char *fmt, ...) {
  (void)level;
  if (strstr(fmt, "Connect ConIn failed")) warnings++;
  if (strstr(fmt, "repeat skipped")) skipped++;
  if (strstr(fmt, "first real attempt begin")) began++;
  if (strstr(fmt, "first real attempt returned")) returned++;
}
void BdsDxeOnConnectConInCallBack(void *, void *);
static void PlatformBootManagerOnDemandConInConnect(void) {
  assert(++platform_calls == 1);
  BdsDxeOnConnectConInCallBack(0, 0);
}
static int EfiBootManagerConnectConsoleVariable(int con) {
  assert(con == 0 && ++console_calls == 1);
  return result;
}
'''
            main = r'''
int main(int argc, char **argv) {
  assert(argc == 2); result = atoi(argv[1]);
  BdsDxeOnConnectConInCallBack(0, 0);
  assert(began == 1 && returned == 1 && skipped == 1);
  assert(warnings == (result != 0));
  BdsDxeOnConnectConInCallBack(0, 0);
  assert(began == 1 && returned == 1 && skipped == 2);
  assert(platform_calls == 1 && console_calls == 1);
  assert(warnings == (result != 0));
  return 0;
}
'''
            with tempfile.TemporaryDirectory() as d:
                source, binary = Path(d) / 'callback.c', Path(d) / 'callback'
                source.write_text(mock + body + main)
                subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(source), '-o', str(binary)], check=True)
                for result in ('0', '14'):
                    subprocess.run([str(binary), result], check=True, timeout=5)

if __name__ == '__main__':
    unittest.main()
