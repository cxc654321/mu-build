"""Execute production BDS summary, without firmware/hardware."""
from pathlib import Path
import platform, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class Summary(unittest.TestCase):
 def test_menu_order(self):
  s=(ROOT/'Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/DeviceBootManagerLib.c').read_text()
  self.assertLess(s.index('J313PhotoPause ("MENU-START")'),s.index('  J313DiagnosticSummary ();'))
  self.assertLess(s.index('  J313DiagnosticSummary ();'),s.index('MENU-CALL BEGIN'))
 def test_real_summary(self):
  inc=ROOT/'MU_BASECORE/MdePkg/Include'
  arch='AArch64' if platform.machine() in ('arm64','aarch64') else 'X64'
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'summary'
   args=['clang','-std=c11','-Wall','-Wextra','-Werror','-fshort-wchar','-fsanitize=address,undefined','-I',str(inc),'-I',str(inc/arch),'-I',str(ROOT/'Silicon/Apple/AppleSiliconPkg/Include'),str(ROOT/'Tests/test_j313_snapshot_summary.c'),'-o',str(out)]
   r=subprocess.run(args,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
   r=subprocess.run([out],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__': unittest.main()
