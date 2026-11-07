"""Synthetic bounded production parser tests; no hardware support claim."""
import ctypes as C
import pathlib, struct, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
D = ROOT / 'Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe'
def prop(k, v):
    return k.encode().ljust(32,b'\0') + struct.pack('<I',len(v)) + v + b'\0'*((-len(v))%4)
def node(name, props=(), children=()):
    props=[('name',name.encode()+b'\0')]+list(props)
    return struct.pack('<II',len(props),len(children))+b''.join(prop(*p) for p in props)+b''.join(children)
def u32(n): return struct.pack('<I',n)
def reg(a,s): return struct.pack('<QQ',a,s)
def fixture(cycle=False, target=b'j313\0'):
    devices=[]
    for ident,parent,off,idx in [(1,0,1,0),(2,1,0x26,0),(3,1,0x10,1),(0x34,1,0x4b,0)]:
        d=bytearray(48);d[3]=ident;d[4]=ident if cycle and ident==1 else parent;d[10]=off;d[11]=idx;devices.append(d)
    spi=node('spi3',[('compatible',b'spi-1,spimc\0'),('reg',reg(0x3510c000,0x4000)),('clock-gates',u32(0x34))],[node('ipd',[('compatible',b'hid-transport,spi\0')])])
    gpio=node('gpio',[('reg',reg(0x3c100000,0x4000)),('clock-gates',u32(2))])
    nub=node('nub-gpio',[('reg',reg(0x3d1f0000,0x4000)),('clock-gates',u32(3))])
    pmgr=node('pmgr',[('reg',reg(0x3b700000,0x100000)+reg(0x3d280000,0x4000)),('devices',b''.join(devices)),('ps-regs',struct.pack('<IIIIII',0,0,0,1,0,0))])
    arm=node('arm-io',[('compatible',b'arm-io,t8103\0'),('ranges',struct.pack('<QQQ',0,0x200000000,0x100000000))],[spi,gpio,nub,pmgr])
    return node('device-tree',[('target-type',target)],[node('chosen',[('chip-id',u32(0x8103))]),arm])
class Resources(C.Structure):
    _fields_=[('target',C.c_char*16),('chip',C.c_uint32)]+[(n,C.c_uint64) for n in ['spi','ss','ap','aps','nub','ns','pmgr','ps']]+[('gate',C.c_uint32),('count',C.c_uint32),('offsets',C.c_uint32*16)]
class TestBoard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); lib=pathlib.Path(cls.tmp.name)/'board.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DJ313_HOST_TEST','-shared','-fPIC','-I'+str(D),'-I'+str(D/'Core'),str(D/'J313BoardResources.c'),'-o',str(lib)],check=True)
        cls.lib=C.CDLL(str(lib)); cls.parse=cls.lib.J313BoardResourcesParse
        cls.parse.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources)];cls.parse.restype=C.c_int
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def check(self,b,ok):
        r=Resources();C.memset(C.byref(r),0xa5,C.sizeof(r));data=C.create_string_buffer(b)
        status=self.parse(data,len(b),C.byref(r));self.assertEqual(status==0,ok)
        if not ok:self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        return r
    def test_valid(self):
        r=self.check(fixture(),True);self.assertEqual(r.spi,0x23510c000);self.assertEqual(r.count,4)
        self.assertEqual(list(r.offsets)[:4],[8,0x130,0x1b80080,0x258])
    def test_all_prefixes(self):
        b=fixture()
        for n in range(len(b)):self.check(b[:n],False)
    def test_root_slash_and_large_population(self):
        b=fixture().replace(prop('name',b'device-tree\0'),prop('name',b'/\0'),1)
        self.check(b,True)
        # Real-length derived budget, beyond the old 256-node host workspace.
        b=fixture(); b=b[:4]+struct.pack('<I',302)+b[8:]
        b+=b''.join(node('extra%d'%i) for i in range(300))
        self.check(b,True)
    def test_opaque_template(self):
        b=fixture(); b=struct.pack('<I',3)+b[4:]
        at=8+len(prop('name',b'device-tree\0'))+len(prop('target-type',b'j313\0'))
        template=prop('unrelated-template',b'opaque\0')
        template=template[:32]+struct.pack('<I',0x80000007)+template[36:]
        self.check(b[:at]+template+b[at:],True)
    def test_cycle_identity_and_trailing(self):
        self.check(fixture(True),False);self.check(fixture(target=b'j314\0'),False);self.check(fixture()+b'\0',False)
if __name__=='__main__': unittest.main()
