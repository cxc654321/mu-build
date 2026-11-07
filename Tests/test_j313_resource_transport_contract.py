"""Production Parse -> TransportInit, with mock IO only (not hardware proof).

PS positions/edges/windows: Linux t8103{,-pmgr}.dtsi:
https://github.com/AsahiLinux/linux/tree/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple
SPI3 -> SIO/SPI_P; SPI_P -> SIO -> SIO_BUSIF. GPIO/NUB_GPIO have
no parents in that DTS. ADT IDs below (except SPI3=0x34), record order,
and ps-reg decomposition are synthetic, NOT a captured J313 ADT.
Address formula follows m1n1 src/pmgr.c at
69affa5a867a44cb281c6cf8716f5a38e3b0b063: reg + ps offset + (device offset << 3).
"""
import ctypes as C
import pathlib
import subprocess
import tempfile
import unittest
from test_j313_board_resources import D, Resources, fixture, node, reg, u32
import struct


def public_fixture():
    devices = []
    # Synthetic IDs; positions and parent edges are public DTS facts.
    for ident, p0, p1, offset, index in [
        (1, 0, 0, 0x38, 0),  # SIO_BUSIF +0x1c0
        (2, 1, 0, 0x39, 0),  # SIO +0x1c8
        (3, 2, 0, 0x43, 0),  # SPI_P +0x218
        (4, 0, 0, 0x26, 0),  # GPIO +0x130
        (5, 0, 0, 0x10, 1),  # mini NUB_GPIO +0x80
        (0x34, 2, 3, 0x4b, 0),  # SPI3 +0x258
    ]:
        d = bytearray(48)
        d[3:6] = bytes([ident, p0, p1])
        d[10:12] = bytes([offset, index])
        devices.append(d)
    spi = node('spi3', [('compatible', b'spi-1,spimc\0'),
        ('reg', reg(0x3510c000, 0x4000)), ('clock-gates', u32(0x34))],
        [node('ipd', [('compatible', b'hid-transport,spi\0')])])
    gpio = node('gpio', [('reg', reg(0x3c100000, 0x100000)), ('clock-gates', u32(4))])
    nub = node('nub-gpio', [('reg', reg(0x3d1f0000, 0x4000)), ('clock-gates', u32(5))])
    pmgr = node('pmgr', [('reg', reg(0x3b700000, 0x14000) + reg(0x3d280000, 0x4000)),
        ('devices', b''.join(devices)), ('ps-regs', struct.pack('<IIIIII', 0, 0, 0, 1, 0, 0))])
    arm = node('arm-io', [('compatible', b'arm-io,t8103\0'),
        ('ranges', struct.pack('<QQQ', 0, 0x200000000, 0x100000000))], [spi, gpio, nub, pmgr])
    return node('device-tree', [('target-type', b'j313\0')],
        [node('chosen', [('chip-id', u32(0x8103))]), arm])


READ = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_uint64)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint64, C.c_uint32)
NOW = C.CFUNCTYPE(C.c_uint64, C.c_void_p)
DELAY = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32)


class IO(C.Structure):
    _fields_ = [('context', C.c_void_p), ('read', READ), ('write', WRITE),
                ('now', NOW), ('delay', DELAY)]


class Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='j313-contract-')
        p = pathlib.Path(cls.tmp.name)
        (p / 'wrapper.c').write_text('''
#include "J313SpiTransport.h"
int contract_init(const J313_SPI_RESOURCES *r, const J313_SPI_IO *io) {
    J313_SPI_TRANSPORT t = {0};
    int status = J313SpiTransportInit(&t, r, io);
    return status == J313_SPI_OK && !t.Initialized ? -1 : status;
}
''')
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-DJ313_HOST_TEST', '-shared', '-fPIC', '-I'+str(D),
            str(p/'wrapper.c'), str(D/'J313BoardResources.c'),
            str(D/'J313SpiTransport.c'), str(D/'Core/j313_spi_pio.c'),
            '-o', str(p/'contract.so')], check=True, timeout=90)
        cls.lib = C.CDLL(str(p/'contract.so'))
        cls.lib.J313BoardResourcesParse.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Resources)]
        cls.lib.J313BoardResourcesParse.restype = C.c_int
        cls.lib.contract_init.argtypes = [C.POINTER(Resources), C.POINTER(IO)]
        cls.lib.contract_init.restype = C.c_int

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def parse(self, blob):
        r = Resources()
        self.assertEqual(self.lib.J313BoardResourcesParse(C.create_string_buffer(blob), len(blob), C.byref(r)), 0)
        return r

    def init(self, r):
        memory, events, clock = {}, [], [0]
        power = {r.pmgr + r.offsets[i] for i in range(min(r.count, 16))}
        def read(_, address):
            events.append(('read', address))
            value = memory.get(address, 0)
            return value | ((value & 15) << 4) if address in power else value
        def write(_, address, value):
            events.append(('write', address))
            memory[address] = value
        def now(_):
            return clock[0]
        def delay(_, us):
            clock[0] += us
        io = IO(None, READ(read), WRITE(write), NOW(now), DELAY(delay))
        status = self.lib.contract_init(C.byref(r), C.byref(io))
        return status, events

    def test_public_positions_parse_to_init(self):
        r = self.parse(public_fixture())
        expected = [0x130, 0x1b80080, 0x1c0, 0x1c8, 0x218, 0x258]
        self.assertEqual(list(r.offsets)[:r.count], expected)
        self.assertEqual(r.ps, 0x1b80084)  # envelope, not a contiguous MMIO aperture
        status, events = self.init(r)
        self.assertEqual(status, 0)
        writes = [a for op, a in events if op == 'write' and a in {r.pmgr+x for x in expected}]
        self.assertEqual(writes, [r.pmgr+x for x in expected])

    def test_original_synthetic_low_offset_rejected_without_mmio(self):
        r = self.parse(fixture())
        self.assertEqual(r.offsets[0], 8)
        self.assertEqual(self.init(r), (1, []))

    def test_invalid_resource_fields_zero_mmio(self):
        mutations = {'target': b'j314', 'chip': 0, 'gate': 0x33,
            'spi': 0, 'ss': 0x168, 'ap': 0, 'aps': 780,
            'nub': 0, 'ns': 0x10000, 'pmgr': 0, 'ps': 0x1b80080,
            'count': 0}
        for field, value in mutations.items():
            with self.subTest(field=field):
                r = self.parse(public_fixture())
                setattr(r, field, value)
                self.assertEqual(self.init(r), (1, []))
        for index, value in [(2, 8), (2, 0x14000), (2, 0x1b84000),
                             (2, 0x1c4), (2, 0x130), (5, 0x250)]:
            with self.subTest(index=index, value=hex(value)):
                r = self.parse(public_fixture())
                r.offsets[index] = value
                self.assertEqual(self.init(r), (1, []))


if __name__ == '__main__':
    unittest.main()
