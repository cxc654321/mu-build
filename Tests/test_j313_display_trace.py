import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("display", ROOT / "Tools/j313-display-trace.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class DisplayTraceTests(unittest.TestCase):
    def test_pinned_idempotent_and_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / mod.TARGET
            path.parent.mkdir(parents=True)
            original = (ROOT / mod.TARGET).read_bytes()
            path.write_bytes(original)
            mod.patch_tree(directory)
            self.assertEqual(path.read_bytes(), original)
            mod.patch_tree(directory, True)
            patched = path.read_bytes()
            mod.patch_tree(directory, True)
            self.assertEqual(path.read_bytes(), patched)
            self.assertEqual(mod.BLOCK.sub('', patched.decode()).encode(), original)
            text = patched.decode()
            self.assertIn('DE-DRAW-END\\n"', text)
            self.assertIn('DE-FIRST-WAIT-END: %r\\n"', text)
            # Compile the exact added C blocks, including the one-time wait state.
            blocks = ''.join(mod.BLOCK.findall(text))
            source = Path(directory) / 'markers.c'
            exe = Path(directory) / 'markers'
            source.write_text('#include <assert.h>\n#define STATIC static\n#define BOOLEAN int\n#define TRUE 1\n#define FALSE 0\n#define DEBUG_ERROR 1\n#define DEBUG(x) record x\nstatic int count;\nstatic void record(int level, const char *fmt, ...) { (void)level; (void)fmt; ++count; }\nint main(void) { int Status=0;\n' + blocks + '\nassert(count == 8 && !J313FirstWait); return 0; }\n')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
            self.assertLess(text.index('DE-DRAW-BEGIN'), text.index('LocalCanvas->Base.Draw'))
            self.assertLess(text.index('DE-FIRST-WAIT-BEGIN'), text.index('mSWMProtocol->WaitForEvent (EventNum'))
            path.write_bytes(patched.replace(b'DE-DRAW-END', b'BROKEN'))
            before = path.read_bytes()
            with self.assertRaises(ValueError):
                mod.patch_tree(directory, True)
            self.assertEqual(before, path.read_bytes())

if __name__ == '__main__':
    unittest.main()
