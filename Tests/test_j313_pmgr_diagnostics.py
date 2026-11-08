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

class TestMissingGpio(TestPmgr):
    def public(self, missing=('gpio', 'nub-gpio'), u16=False):
        if __package__:
            from .test_j313_resource_transport_contract import public_fixture
        else:
            from test_j313_resource_transport_contract import public_fixture
        return public_fixture(missing, u16)

    def test_missing_recovery_real_record_ids(self):
        for wide in [False, True]:
            b=self.public(u16=wide); ds=devices(b)
            # No hardcoded IDs; exercise high byte of actual u16 ID.
            if wide: ds[3*48+26:3*48+28]=struct.pack('<H',0x345)
            else: ds[3*48+3]=0x75
            self.assertEqual(self.detail(replace_property(b,'devices',ds)).width,16 if wide else 8)
            self.check(replace_property(b,'devices',ds),True)

    def test_present_invalid_never_recovers(self):
        b=self.public(missing=())
        for v in [b'', b'abc', bytes(68)]:
            self.detail(b.replace(prop('clock-gates',u32(4)),prop('clock-gates',v)),4)
        old=prop('clock-gates',u32(4))
        flagged=old[:32]+u32(0x80000004)+old[36:]
        p=self.detail(b.replace(old,flagged),21)
        self.assertEqual((p.source,p.gate_length),(2,4))
        self.detail(b.replace(old,prop('clock-gates',u32(99))),6)

    def test_candidate_uniqueness_address_not_name(self):
        b=self.public(); ds=devices(b)
        # Names are not an undocumented spelling gate, nor sufficient evidence.
        for name in [bytes(16),b'GPIO'.ljust(16,b'\0'),b'gpio'.ljust(16,b'\0'),b'X'*16]:
            ds[3*48+32:4*48]=name; ds[4*48+32:5*48]=name
            self.detail(replace_property(b,'devices',ds))
        wrong=bytearray(ds); wrong[3*48+10]=0x27
        self.detail(replace_property(b,'devices',wrong),19)
        for sameid in [False,True]:
            duplicate=bytearray(ds[3*48:4*48])
            if not sameid: duplicate[3]=99
            self.detail(replace_property(b,'devices',ds+duplicate),20)
        for offset,value,reason in [(3*48,0x10,19),(3*48+11,255,10)]:
            bad=bytearray(ds);bad[offset]=value
            self.detail(replace_property(b,'devices',bad),reason)
        bad=bytearray(ds);bad[3*48+3]=0
        self.detail(replace_property(b,'devices',bad),5)

    def test_unresolvable_physical_records_fail_closed(self):
        b=self.public(); ds=devices(b)
        bad=bytearray(ds);bad[11]=255
        p=self.detail(replace_property(b,'devices',bad),10)
        self.assertEqual((p.id,p.record,p.ps_index,p.reg_index),(1,0,255,0xffffffff))
        # A virtual unrelated record does not require a physical PS address.
        extra=bytearray(48);extra[0]=0x10;extra[3]=99;extra[11]=255
        self.detail(replace_property(b,'devices',ds+extra))
        for ps,reason in [(struct.pack('<IIIIII',255,0,0,1,0,0),11),
                          (struct.pack('<IIIIII',0,1,0,1,0,0),12)]:
            self.detail(replace_property(b,'ps-regs',ps),reason)

    def test_fallback_retains_actual_parent_first_order(self):
        b=self.public(); ds=devices(b);ds[3*48+4]=2
        r=self.check(replace_property(b,'devices',ds),True)
        self.assertEqual(list(r.offsets)[:r.count],[0x1c0,0x1c8,0x130,0x1b80080,0x218,0x258])

    def test_recovered_device_still_validates_power_graph(self):
        b=self.public(); ds=devices(b)
        for offset,value,reason in [(3*48+4,99,6),(3*48+4,4,8),
                                     (5*48+3,4,7),(5*48+11,255,10)]:
            bad=bytearray(ds);bad[offset]=value
            self.detail(replace_property(b,'devices',bad),reason)
        self.detail(replace_property(b,'ps-regs',b'bad'),3)
        self.detail(replace_property(b,'devices',ds[:-1]),2)
        self.detail(b.replace(reg(0x3d280000,0x4000),reg(0x3d284000,0x4000)),19)

if __name__=='__main__':unittest.main()
