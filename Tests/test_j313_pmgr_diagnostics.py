"""Synthetic upstream-layout fixtures, NOT a captured hardware ADT."""
if __package__:
    from .test_j313_board_resources import *
else:
    from test_j313_board_resources import *

class Pmgr(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ['attempted','reason','source','gate','id','width','devices','ps','record','ps_index','reg_index','gate_length']]

def replace_property(b,key,value):
    at=b.index(key.encode().ljust(32,b'\0'))
    size=struct.unpack_from('<I',b,at+32)[0]
    return b[:at]+prop(key,value)+b[at+36+((size+3)&~3):]

def devices(b):
    at=b.index(b'devices'.ljust(32,b'\0'))
    size=struct.unpack_from('<I',b,at+32)[0]
    return bytearray(b[at+36:at+36+size])

class TestPmgr(TestBoard):
    def detail(self,b,reason=0):
        f=self.lib.J313BoardResourcesParseDetailed
        f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic),C.POINTER(C.c_uint32),C.POINTER(Pmgr)]
        r=Resources(); d=Diagnostic(); mask=C.c_uint32(); p=Pmgr()
        C.memset(C.byref(p),0xa5,C.sizeof(p))
        status=f(C.create_string_buffer(b),len(b),C.byref(r),C.byref(d),C.byref(mask),C.byref(p))
        self.assertEqual((status,p.reason),(4 if reason else 0,reason))
        self.assertEqual(p.attempted,1)
        if status:self.assertEqual(bytes(r),bytes(C.sizeof(r)))
        return p
    def test_layouts(self):
        b=fixture(); p=self.detail(b)
        self.assertEqual((p.width,p.devices,p.ps,p.record,p.ps_index,p.reg_index),(8,192,24,3,0,0))
        ds=devices(b)
        for i in range(0,len(ds),48):
            ds[i+26]=ds[i+3]; ds[i+3]=0
        self.assertEqual(self.detail(replace_property(b,'devices',ds)).width,16)
    def test_property_failures(self):
        b=fixture()
        self.detail(replace_property(b,'clock-gates',u32(0x35)),1)
        for v in [b'',bytes(48),bytes(97)]:self.detail(replace_property(b,'devices',v),2)
        for v in [b'',bytes(13)]:self.detail(replace_property(b,'ps-regs',v),3)
        self.detail(b.replace(b'ps-regs'.ljust(32,b'\0'),b'ps-groups'.ljust(32,b'\0')),18)
        for value in [b'',b'abc',bytes(68)]:
            self.detail(b.replace(prop('clock-gates',u32(2)),prop('clock-gates',value)),4)
        for ident in [0,0x10000,0x10000002]:
            self.detail(b.replace(prop('clock-gates',u32(2)),prop('clock-gates',u32(ident))),5)
    def test_walk_failures(self):
        b=fixture()
        p=self.detail(b.replace(prop('clock-gates',u32(2)),prop('clock-gates',u32(99))),6)
        self.assertEqual((p.source,p.gate,p.id,p.record),(2,99,99,0xffffffff))
        ds=devices(b);ds[3*48+3]=2
        self.detail(replace_property(b,'devices',ds),7)
        self.detail(fixture(cycle=True),8)
        ds=devices(b);ds[48+11]=2
        p=self.detail(replace_property(b,'devices',ds),10)
        self.assertEqual((p.id,p.record,p.ps_index,p.reg_index),(2,1,2,0xffffffff))
        self.detail(replace_property(b,'ps-regs',struct.pack('<IIIIII',2,0,0,1,0,0)),11)
        for offset in [1,0x100000]:
            self.detail(replace_property(b,'ps-regs',struct.pack('<IIIIII',0,offset,0,1,0,0)),12)
        self.detail(b.replace(reg(0x3d280000,0x4000),reg(0x3d284000,0x4000)),13)
        ds=devices(b);ds[48+10]=1
        self.detail(replace_property(b,'devices',ds),15)
        ds=devices(b)
        for i in range(0,len(ds),48):ds[i]=0x10
        self.detail(replace_property(b,'devices',ds),17)
    def test_walk_budget(self):
        b=fixture(); ds=devices(b)
        # 17-node acyclic chain exceeds the unchanged 16-entry walk bound.
        ds[48+4]=4
        for ident in range(4,20):
            d=bytearray(48);d[0]=0x10;d[3]=ident;d[4]=ident+1 if ident<19 else 0;ds+=d
        self.detail(replace_property(b,'devices',ds),9)
    def test_diagnostic_reset_before_power(self):
        f=self.lib.J313BoardResourcesParseDetailed
        f.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Resources),C.POINTER(Diagnostic),C.POINTER(C.c_uint32),C.POINTER(Pmgr)]
        p=Pmgr();C.memset(C.byref(p),0xa5,C.sizeof(p));r=Resources()
        self.assertEqual(f(None,0,C.byref(r),None,None,C.byref(p)),1)
        self.assertEqual((p.attempted,p.reason,p.record),(0,0,0xffffffff))

if __name__=='__main__':unittest.main()
