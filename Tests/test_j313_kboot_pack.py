"""Host-only tests of the production packer; no firmware or ESP access.

Run: python3 -B -m unittest discover -s Tests -p 'test_j313_kboot_pack.py' -v
"""
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

TOOL = Path(__file__).resolve().parents[1] / "Tools" / "pack_j313_kboot.py"
SPEC = importlib.util.spec_from_file_location("pack_j313_kboot", TOOL)
pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack)


def dtb_fixture(extra=0):
    # Empty root node, END_NODE, END; empty strings and terminated reserve map.
    structure = struct.pack(">4I", 1, 0, 2, 9)
    total = 56 + len(structure) + extra
    return (struct.pack(">10I", 0xD00DFEED, total, 56, 72, 40,
                        17, 16, 0, 0, len(structure))
            + bytes(16) + structure + bytes(extra))


def fd_fixture(image_size=0x100E0000):
    fd = bytearray(bytes(range(256)) * 4)
    struct.pack_into("<Q", fd, 0x10, image_size)
    fd[0x38:0x3c] = b"ARM\x64"
    return bytes(fd)


def changed_u32(data, offset, value):
    result = bytearray(data)
    struct.pack_into(">I", result, offset, value)
    return bytes(result)


class KbootPackTests(unittest.TestCase):
    def setUp(self):
        self.loader = b"L" * pack.ALIGN
        self.dtb = dtb_fixture()
        self.fd = fd_fixture()

    def build(self, **changes):
        inputs = dict(loader=self.loader, dtb=self.dtb, fd=self.fd)
        inputs.update(changes)
        return pack.build_package(**inputs)

    def test_deterministic_roundtrip_layout_and_manifest(self):
        blob, manifest = self.build()
        self.assertEqual((blob, manifest), self.build())
        self.assertEqual(json.loads(json.dumps(manifest)), manifest)
        offset, length = manifest["gzip"]["offset"], manifest["gzip"]["length"]
        self.assertEqual(offset, len(self.loader) + len(self.dtb))
        self.assertEqual(blob[:offset], self.loader + self.dtb)
        compressed = blob[offset:offset + length]
        self.assertEqual(compressed[:10], b"\x1f\x8b\x08\x00" + bytes(4) + b"\x02\xff")
        restored = gzip.decompress(compressed)
        self.assertEqual(restored, self.fd)
        self.assertEqual(restored[:64], self.fd[:64])
        decoder = zlib.decompressobj(31)
        self.assertEqual(decoder.decompress(blob[offset:]), self.fd)
        self.assertTrue(decoder.eof)
        self.assertEqual(decoder.unused_data, blob[offset + length:])
        tail = blob[offset + length:]
        self.assertGreaterEqual(len(tail), 64)
        self.assertEqual(tail, bytes(len(tail)))
        self.assertEqual(len(tail), 4 + manifest["padding_length"])
        self.assertEqual(len(blob) % pack.ALIGN, 0)
        self.assertEqual(manifest["fd_declared_reserve_size"], 0x100E0000)
        self.assertFalse(manifest["deployed"])
        for name, data in (("loader", self.loader), ("dtb", self.dtb), ("fd", self.fd)):
            self.assertEqual(manifest["inputs"][name],
                             {"length": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        self.assertEqual(manifest["output"],
                         {"length": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})

    def test_tail_lookahead_at_alignment_boundary(self):
        _, initial = self.build()
        length = initial["gzip"]["length"]
        for old_padding in (0, 1, 59, 60):
            with self.subTest(old_padding=old_padding):
                extra = (-(len(self.dtb) + length + 4 + old_padding)) % pack.ALIGN
                blob, manifest = self.build(dtb=dtb_fixture(extra))
                expected = old_padding + (pack.ALIGN if old_padding < 60 else 0)
                self.assertEqual(manifest["padding_length"], expected)
                end = manifest["gzip"]["offset"] + length
                self.assertEqual(blob[end:end + 64], bytes(64))
                self.assertEqual(len(blob) % pack.ALIGN, 0)

    def test_invalid_loaders(self):
        for loader in (b"", b"L", self.loader[:-1], self.loader + b"L"):
            with self.subTest(length=len(loader)), self.assertRaisesRegex(ValueError, "loader"):
                self.build(loader=loader)

    def test_invalid_dtb_headers_and_bounds(self):
        cases = [self.dtb[:39], self.dtb[:-1], self.dtb + b"x",
                 changed_u32(self.dtb, 0, 0),
                 changed_u32(self.dtb, 4, 39),
                 changed_u32(self.dtb, 4, len(self.dtb) + 1),
                 changed_u32(self.dtb, 8, 36),
                 changed_u32(self.dtb, 12, len(self.dtb) + 1),
                 changed_u32(self.dtb, 16, 41),
                 changed_u32(self.dtb, 20, 16),
                 changed_u32(self.dtb, 24, 18),
                 changed_u32(self.dtb, 32, 0xFFFFFFFF),
                 changed_u32(self.dtb, 36, 0xFFFFFFFF),
                 changed_u32(self.dtb, 8, 40)]
        bad_reserve = bytearray(self.dtb)
        bad_reserve[40:] = b"x" * (len(bad_reserve) - 40)
        cases.append(bytes(bad_reserve))
        for dtb in cases:
            with self.subTest(dtb=dtb.hex()), self.assertRaisesRegex(ValueError, "DTB"):
                self.build(dtb=dtb)

    def test_truncated_fd_header_and_bad_magic(self):
        for size in (0, 16, 56, 60, 63):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "header"):
                self.build(fd=self.fd[:size])
        fd = bytearray(self.fd)
        fd[0x38:0x3c] = b"NOPE"
        with self.assertRaisesRegex(ValueError, "magic"):
            self.build(fd=bytes(fd))

    def test_fd_size_limits(self):
        for size in (0, len(self.fd) - 1, pack.MAX_IMAGE_SIZE + 1, (1 << 64) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                self.build(fd=fd_fixture(size))
        for size in (len(self.fd), pack.MAX_IMAGE_SIZE):
            with self.subTest(size=size):
                _, manifest = self.build(fd=fd_fixture(size))
                self.assertEqual(manifest["fd_declared_reserve_size"], size)
        for limit in (0, 63, pack.MAX_IMAGE_SIZE + 1):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                self.build(max_image_size=limit)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            self.build(max_image_size=len(self.fd))

    def test_file_api_cli_and_exclusive_create(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [root / name for name in ("loader", "dtb", "fd")]
            for path, data in zip(paths, (self.loader, self.dtb, self.fd)):
                path.write_bytes(data)
            output = root / "packed"
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                self.assertEqual(pack.main([str(p) for p in paths + [output]]), 0)
            expected, manifest = self.build()
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(json.loads(captured.getvalue()), manifest)
            output.write_bytes(b"existing output must survive")
            with self.assertRaises(FileExistsError):
                pack.pack_files(*paths, output)
            self.assertEqual(output.read_bytes(), b"existing output must survive")
            link = root / "link"
            link.symlink_to(output)
            with self.assertRaises(FileExistsError):
                pack.pack_files(*paths, link)
            self.assertEqual(output.read_bytes(), b"existing output must survive")
            # An output aliasing an input must also be refused, never truncated.
            with self.assertRaises(FileExistsError):
                pack.pack_files(*paths, paths[2])
            self.assertEqual(paths[2].read_bytes(), self.fd)
            paths[2].write_bytes(self.fd[:63])
            with self.assertRaises(ValueError):
                pack.pack_files(*paths, root / "invalid")
            self.assertFalse((root / "invalid").exists())


if __name__ == "__main__":
    unittest.main()
