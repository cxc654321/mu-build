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
    def test_ap_public_window(self):
        b=fixture().replace(reg(0x3c100000,0x4000),reg(0x3c100000,0x100000),1)
        self.assertEqual(self.check(b,True).aps,0x100000)
        self.check(b.replace(reg(0x3c100000,0x100000),reg(0x3c100000,0x100004),1),False)
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
class Diagnostic(C.Structure):
    _fields_=[('reason',C.c_uint32),('offset',C.c_size_t),('node',C.c_size_t),('key',C.c_char*33),('size',C.c_size_t),('consumed',C.c_size_t),('tail_size',C.c_size_t),('tree_complete',C.c_uint32),('tail_all_zero',C.c_uint32)]

class TestDiagnostic(TestBoard):
    def detail(self,b):
        data=C.create_string_buffer(b);r=Resources();d=Diagnostic()
        C.memset(C.byref(r),0xa5,C.sizeof(r));C.memset(C.byref(d),0xa5,C.sizeof(d))
        f=self.lib.J313BoardResourcesParseEx
        f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic)]
        status=f(data,len(b),C.byref(r),C.byref(d))
        if status:self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        self.assertLessEqual(d.offset,len(b))
        return status,d
    def test_real_failure_locations(self):
        b=fixture();bad=b[:40]+u32(0x7fffffff)+b[44:]
        status,d=self.detail(bad)
        self.assertEqual((status,d.reason,d.offset,d.node,d.key),(1,11,44,0,b'name'))
        at=b.index(prop('name',b'spi3\0'))
        bad=b[:at+36]+b'spi/\0'+b[at+41:]
        status,d=self.detail(bad)
        self.assertEqual((status,d.reason,d.offset,d.node,d.key),(1,14,at+36,3,b'name'))
        for tail in [b'\0',b'\0'*4096,b'private-secret']:
            status,d=self.detail(b+tail)
            self.assertEqual((status,d.reason,d.offset,d.key),(1,17,len(b),b''))
    def test_container_tail_diagnostics_and_legacy(self):
        b=fixture()
        for tail in [b'',b'\0',b'\0'*4096,b'\xff',b'\0'*4095+b'\x01',b'\x01'+b'\0'*4095,node('second-root')]:
            with self.subTest(length=len(tail),zero=not any(tail)):
                status,d=self.detail(b+tail)
                self.assertEqual(status,1 if tail else 0)
                self.assertEqual((d.size,d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero),
                                 (len(b+tail),len(b),len(tail),1,int(not any(tail))))
                self.assertEqual(d.node,C.c_size_t(-1).value)
                self.assertEqual(d.key,b'')
                self.check(b+tail,not tail) # legacy API remains strict
        # Failed structural prefixes must not report guessed tree/tail lengths.
        for n in range(len(b)):
            status,d=self.detail(b[:n])
            self.assertEqual((d.size,d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero),(n,0,0,0,0))
    def test_structural_reason_branches(self):
        cases=[
            (b'\xd0\x0d\xfe\xed'+b'\0'*4,3,0),
            (u32(0xffffffff)+u32(0),7,0),
            (u32(1)+u32(0)+b'x',8,8),
            (node('x')[:-1],12,46),
            (u32(0)+u32(0),15,8),
            (node('x',[('name',b'y\0')]),10,48),
            (node('root',children=[node('x'),node('x')]),16,144),
        ]
        b=node('x');cases.append((b[:40]+u32(0x80000002)+b[44:],13,40))
        deep=node('x')
        for _ in range(64):deep=node('x',children=[deep])
        cases.append((deep,4,64*48))
        for b,reason,offset in cases:
            with self.subTest(reason=reason):
                status,d=self.detail(b)
                self.assertEqual((status,d.reason,d.offset),(1,reason,offset))
    def test_name_padding_not_speculatively_relaxed(self):
        b=fixture().replace(prop('name',b'spi3\0'),prop('name',b'spi3\0\0'))
        status,d=self.detail(b);self.assertEqual((status,d.reason,d.key),(1,14,b'name'))
    def test_opaque_unrelated_compatible_and_identity_gate(self):
        for value in [b'',b'\0',b'x\0\0',b'opaque\xff']:
            b=fixture();b=b[:4]+u32(3)+b[8:]+node('unrelated',[('compatible',value)])
            self.check(b,True)
        for value in [b'',b'\0',b'arm-io,t8103',b'arm-io,t8103\0bad']:
            b=fixture().replace(prop('compatible',b'arm-io,t8103\0'),prop('compatible',value))
            status,d=self.detail(b);self.assertEqual(status,2)
    def test_bounded_sanitized_key(self):
        b=fixture();status,d=self.detail(b[:8]+b'X'*32+b[40:])
        self.assertEqual((status,d.reason,d.key),(1,9,b'X'*32))
        self.assertEqual(bytes(d)[Diagnostic.key.offset+32],0)
        status,d=self.detail(b[:8]+b'na\x01e'+b[12:])
        self.assertEqual((status,d.reason,d.offset,d.key),(1,9,10,b'na?e'))
    def test_diagnostic_reset_and_all_prefixes(self):
        b=fixture();status,d=self.detail(b)
        self.assertEqual((status,d.reason,d.offset,d.key),(0,0,len(b),b''))
        for n in range(len(b)):
            status,d=self.detail(b[:n]);self.assertEqual(status,1);self.assertNotEqual(d.reason,0)
    def test_guard_page_all_prefixes(self):
        # A subprocess protects the test runner; every prefix ends at PROT_NONE.
        # Unlike create_string_buffer this detects even a one-byte overread.
        import sys
        script = r'''
import ctypes as C, mmap, sys
sys.path.insert(0,sys.argv[1])
from test_j313_board_resources import fixture, Resources, Diagnostic
lib=C.CDLL(sys.argv[2])
f=getattr(lib,sys.argv[3])
f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic)]
container=sys.argv[3].endswith('Container')
page=mmap.PAGESIZE;m=mmap.mmap(-1,page*2,prot=mmap.PROT_READ|mmap.PROT_WRITE)
addr=C.addressof(C.c_char.from_buffer(m));syslib=C.CDLL(None)
syslib.mprotect.argtypes=[C.c_void_p,C.c_size_t,C.c_int]
assert syslib.mprotect(addr+page,page,0)==0
b=fixture();assert len(b)<page
cases=[b[:n] for n in range(len(b)+1)]
cases += [b[:8]+b'X'*32+b[40:],b[:40]+(0x7fffffff).to_bytes(4,'little')+b[44:]]
cases += [b+b'\0',b+b'\xff',b+b'\0'*(page-len(b)),b+b'\0'*(page-len(b)-1)+b'\x01']
for data in cases:
    p=addr+page-len(data);C.memmove(p,data,len(data));r=Resources();d=Diagnostic()
    result=f(p,len(data),C.byref(r),C.byref(d))
    success=(data==b or (container and data.startswith(b)))
    assert (result==0)==success
    assert d.size==len(data)
    if len(data)>=len(b) and data[:len(b)]==b:
        assert (d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero)==(len(b),len(data)-len(b),1,int(not any(data[len(b):])))
    else:
        assert (d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero)==(0,0,0,0)
    assert bytes(r)==bytes(C.sizeof(r)) if result else True
assert syslib.mprotect(addr+page,page,mmap.PROT_READ|mmap.PROT_WRITE)==0
'''
        for name in ['J313BoardResourcesParseEx','J313BoardResourcesParseContainer']:
            subprocess.run([sys.executable,'-c',script,str(ROOT/'Tests'),str(pathlib.Path(self.tmp.name)/'board.so'),name],check=True)

class TestContainer(TestBoard):
    def container(self,b):
        f=self.lib.J313BoardResourcesParseContainer
        f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic)]
        f.restype=C.c_int
        data=C.create_string_buffer(b);r=Resources();d=Diagnostic()
        C.memset(C.byref(r),0xa5,C.sizeof(r));C.memset(C.byref(d),0xa5,C.sizeof(d))
        status=f(data,len(b),C.byref(r),C.byref(d))
        if status:self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        return status,r,d

    def test_arbitrary_tail_first_root_only(self):
        b=fixture();expected=bytes(self.check(b,True))
        for tail in [b'',b'\0',b'\0'*4096,b'\xff',b'\0'*4095+b'\x01',fixture(target=b'j314\0'),fixture(True)]:
            with self.subTest(tail=len(tail)):
                status,r,d=self.container(b+tail)
                self.assertEqual(status,0);self.assertEqual(bytes(r),expected)
                self.assertEqual((d.reason,d.offset,d.node,d.key),(0,len(b),C.c_size_t(-1).value,b''))
                self.assertEqual((d.size,d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero),
                                 (len(b+tail),len(b),len(tail),1,int(not any(tail))))
                self.check(b+tail,not tail)

    def test_incomplete_first_root_rejected(self):
        b=fixture()
        for n in range(len(b)):
            status,r,d=self.container(b[:n])
            self.assertEqual(status,1)
            self.assertEqual((d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero),(0,0,0,0))
        # Declared last property value exceeds even appended candidate root.
        at=b.index(prop('ps-regs',struct.pack('<IIIIII',0,0,0,1,0,0)))
        bad=b[:at+32]+u32(0x7fffffff)+b[at+36:]+b
        status,r,d=self.container(bad)
        self.assertEqual((status,d.reason,d.tree_complete),(1,11,0))

    def test_tail_cannot_replace_identity_or_resources(self):
        good=fixture()
        for bad,code in [(fixture(target=b'j314\0'),2),
                         (good.replace(u32(0x8103),u32(0x8104),1),2),
                         (good.replace(reg(0x3510c000,0x4000),reg(0x35110000,0x4000),1),3),
                         (good.replace(reg(0x3c100000,0x4000),reg(0x3c100000,0x100004),1),3),
                         (fixture(True),4)]:
            status,r,d=self.container(bad+good)
            self.assertEqual(status,code)
            self.assertEqual((d.reason,d.consumed,d.tree_complete),(0,len(bad),1))

    def test_legacy_abi_and_optional_diagnostic(self):
        # Existing symbols and struct layout are unchanged; guards detect writes past ABI.
        class Guard(C.Structure):
            _fields_=[('d',Diagnostic),('guard',C.c_ubyte*32)]
        b=fixture();data=C.create_string_buffer(b+b'\xff');r=Resources()
        for name in ['J313BoardResourcesParseEx','J313BoardResourcesParseContainer']:
            f=getattr(self.lib,name)
            f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic)]
            g=Guard();C.memset(C.byref(g),0xa5,C.sizeof(g))
            self.assertEqual(f(data,len(b),C.byref(r),C.byref(g.d)),0)
            self.assertEqual(bytes(g.guard),b'\xa5'*32)
            self.assertEqual(f(data,len(b)+1,C.byref(r),None),int(name.endswith('Ex')))
        self.check(b,True)

class TestIdentityBypass(TestBoard):
    def experimental(self,b):
        f=self.lib.J313BoardResourcesParseExperimentalIdentityBypass
        f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic),C.POINTER(C.c_uint32)]
        f.restype=C.c_int
        data=C.create_string_buffer(b);r=Resources();d=Diagnostic();mask=C.c_uint32(0xffffffff)
        C.memset(C.byref(r),0xa5,C.sizeof(r))
        status=f(data,len(b),C.byref(r),C.byref(d),C.byref(mask))
        if status:self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        return status,r,d,mask.value

    def mismatches(self):
        return [
            (prop('target-type',b'j313\0'),prop('target-type',b'j314\0')),
            (prop('chip-id',u32(0x8103)),prop('chip-id',u32(0x8104))),
            (prop('compatible',b'arm-io,t8103\0'),prop('compatible',b'arm-io,t8104\0')),
            (prop('compatible',b'spi-1,spimc\0'),prop('compatible',b'spi-2,spimc\0')),
            (prop('compatible',b'hid-transport,spi\0'),prop('compatible',b'hid-transport,usb\0')),
        ]

    def strict_identity(self,b):
        data=C.create_string_buffer(b);r=Resources();d=Diagnostic()
        self.assertEqual(self.parse(data,len(b),C.byref(r)),2)
        self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        for name in ['J313BoardResourcesParseEx','J313BoardResourcesParseContainer']:
            f=getattr(self.lib,name)
            f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic)]
            C.memset(C.byref(r),0xa5,C.sizeof(r))
            self.assertEqual(f(data,len(b),C.byref(r),C.byref(d)),2)
            self.assertEqual(bytes(r),bytes(C.sizeof(r)))
            self.assertEqual(d.reason,0)

    def test_each_mismatch_and_combined_mask(self):
        good=fixture();combined=good
        for bit,(old,new) in enumerate(self.mismatches()):
            with self.subTest(bit=bit):
                bad=good.replace(old,new,1);combined=combined.replace(old,new,1)
                self.strict_identity(bad)
                status,r,d,mask=self.experimental(bad)
                self.assertEqual((status,mask),(0,1<<bit))
                self.assertEqual(bytes(r),bytes(self.check(good,True)))
                self.assertEqual((r.target,r.chip),(b'j313',0x8103)) # assumptions only
        self.strict_identity(combined)
        self.assertEqual(self.experimental(combined)[3],0x1f)
        self.assertEqual(self.experimental(good)[3],0)

    def test_missing_identity_properties_and_nodes(self):
        for bit,(old,_) in enumerate(self.mismatches()):
            # Rename identity property, preserving valid node/property counts.
            bad=fixture().replace(old,b'ignored'.ljust(32,b'\0')+old[32:],1)
            self.strict_identity(bad)
            status,r,d,mask=self.experimental(bad)
            self.assertEqual((status,mask),(0,1<<bit))
        # Missing resource-bearing nodes still fail; no fabricated resources.
        for name,mask in [('arm-io',0x1c),('spi3',0x18)]:
            bad=fixture().replace(prop('name',name.encode()+b'\0'),prop('name',b'absent\0'),1)
            self.strict_identity(bad)
            status,r,d,actual=self.experimental(bad)
            self.assertEqual((status,actual),(3,mask))

    def test_resource_and_power_checks_remain_enforced(self):
        good=fixture(target=b'j314\0')
        cases=[
            (good.replace(reg(0x3510c000,0x4000),reg(0x35110000,0x4000),1),3),
            (good.replace(reg(0x3c100000,0x4000),reg(0x3c100000,0x100004),1),3),
            (good.replace(struct.pack('<QQQ',0,0x200000000,0x100000000),struct.pack('<QQQ',0,0x200000000,0),1),3),
            (good.replace(prop('clock-gates',u32(0x34)),prop('clock-gates',u32(0x35)),1),4),
            (fixture(True,target=b'j314\0'),4),
            (good.replace(reg(0x3d280000,0x4000),reg(0x3d290000,0x4000),1),4),
            (good.replace(prop('ps-regs',struct.pack('<IIIIII',0,0,0,1,0,0)),prop('ps-regs',b'bad'),1),4),
        ]
        for bad,code in cases:
            for tail in [b'',fixture()]:
                status,r,d,mask=self.experimental(bad+tail)
                self.assertEqual((status,mask,d.tree_complete),(code,1,1))

    def test_structure_prefixes_and_tail_policy(self):
        b=fixture(target=b'j314\0')
        for n in range(len(b)):
            status,r,d,mask=self.experimental(b[:n])
            self.assertEqual((status,mask,d.tree_complete),(1,0,0))
        for tail in [b'',b'\0'*4096,b'\xff',fixture(True),fixture()]:
            status,r,d,mask=self.experimental(b+tail)
            self.assertEqual((status,mask),(0,1))
            self.assertEqual((d.consumed,d.tail_size,d.tree_complete,d.tail_all_zero),
                             (len(b),len(tail),1,int(not any(tail))))
        bad=b[:40]+u32(0x7fffffff)+b[44:]
        self.assertEqual(self.experimental(bad)[0],1)

    def test_experimental_optional_outputs_and_diagnostic_abi(self):
        class Guard(C.Structure):
            _fields_=[('d',Diagnostic),('guard',C.c_ubyte*32)]
        b=fixture(target=b'j314\0');data=C.create_string_buffer(b);r=Resources()
        self.experimental(b) # install prototype
        f=self.lib.J313BoardResourcesParseExperimentalIdentityBypass
        g=Guard();C.memset(C.byref(g),0xa5,C.sizeof(g))
        self.assertEqual(f(data,len(b),C.byref(r),C.byref(g.d),None),0)
        self.assertEqual(bytes(g.guard),b'\xa5'*32)
        mask=C.c_uint32(0xffffffff)
        self.assertEqual(f(data,len(b),C.byref(r),None,C.byref(mask)),0)
        self.assertEqual(mask.value,1)
        self.assertEqual(f(None,0,C.byref(r),None,C.byref(mask)),1)
        self.assertEqual(mask.value,0)
        self.assertEqual(bytes(r),bytes(C.sizeof(r)))

if __name__=='__main__': unittest.main()
