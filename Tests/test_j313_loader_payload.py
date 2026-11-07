"""Host-only production loader + real tinf contract test (NOT hardware boot).

Run: python Tests/test_j313_loader_payload.py --m1n1-source /explicit/source
Or set M1N1_SOURCE_ROOT for unittest discovery. No adjacent checkout is assumed.
Missing parameter explicitly skips; an explicit bad source/compiler fails.
CI: checkout https://github.com/NT-for-ASi/m1n1_windows at
69affa5a867a44cb281c6cf8716f5a38e3b0b063, then pass its path with the option above.
Requires Python stdlib and clang/GCC with ASan/UBSan; no firmware build required.

No vendoring: source hashes pin original functions, header, tinf and MIT LICENSE.
Functions/header are extracted verbatim into a temporary translation unit with
original MIT license alongside. Original tinf files retain their own license.
Mock heapblock/printf/kernel-only dispatcher; synthetic Linux-header FD. Does
not test full payload scanner, its separate 64-byte terminal padding contract,
MMU/cache, firmware/hardware startup or general malicious-input safety.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import os
import re
import shlex
import struct
import subprocess
import tempfile
import unittest

UPSTREAM = "https://github.com/NT-for-ASi/m1n1_windows"
COMMIT = "69affa5a867a44cb281c6cf8716f5a38e3b0b063"
HASHES = {
    "LICENSE": "11ca423a564b52d3b7f260398baa20d016c25ae180fbdd0067c27b7d1ed7b420",
    "src/payload.c": "03d5063c324a25750f007745ee0618ca0cdb61dd8245c89bac4b6dde1afb66e4",
    "src/kboot.h": "4045a2bb9174b3a063118f90c9c5f80e62ea4298190e920a9a7edd37955590d6",
    "src/tinf/tinf.h": "30bc66f2a23341e01dbc7da6e3837927189f4f077446b139fcae2ab73f59def1",
    "src/tinf/tinfgzip.c": "87fa693dc600f7ce0cebb6c602c616c46eea42d213f0e572236498a4c4536bec",
    "src/tinf/tinflate.c": "7d885d395a4797706f10344b2c436cda464de90570d93d444859ab3d0fd00d99",
    "src/tinf/crc32.c": "7f9b8a8149134e4b9c8ed36d2f0426eebd7264f4117d4648a971fe207cea2350",
}


def extract(text, pattern):
    matches = list(re.finditer(pattern, text, re.M | re.S))
    if len(matches) != 1:
        raise AssertionError(f"Expected one verbatim source region: {pattern}")
    return matches[0][0]


PREAMBLE = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sanitizer/asan_interface.h>
#include "tinf.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
'''

MOCKS = r'''
_Static_assert(sizeof(struct kernel_header) == 64, "Linux header size");
_Static_assert(offsetof(struct kernel_header, image_size) == 16, "image size offset");
_Static_assert(offsetof(struct kernel_header, magic) == 0x38, "magic offset");
static struct kernel_header *kernel;
static u8 *heap;
static size_t cursor, calls, copies, copied, dispatches;
static size_t sizes[4], aligns[4];
static const size_t accessible = 65536;
static void *heapblock_alloc_aligned(size_t size, size_t align)
{
    assert(calls < 4 && align && !(align & (align - 1)));
    sizes[calls] = size;
    aligns[calls++] = align;
    cursor = (cursor + align - 1) & ~(align - 1);
    assert(cursor <= accessible && size <= accessible - cursor);
    void *result = heap + cursor;
    cursor += size;  /* Reservation only: MUST NOT touch poisoned padding. */
    return result;
}
static int mock_printf(const char *format, ...)
{
    (void)format;
    return 0;
}
static void *spy_memcpy(void *dest, const void *source, size_t size)
{
    copies++;
    copied += size;
    return memcpy(dest, source, size);
}
static void *load_one_payload(void *start, size_t size);
#define printf mock_printf
#define memcpy spy_memcpy
'''

HARNESS = r'''
#undef printf
#undef memcpy
/* Explicit MOCK dispatcher, not production payload scanning. */
static void *load_one_payload(void *start, size_t size)
{
    dispatches++;
    assert(size >= sizeof(struct kernel_header));
    assert(!memcmp((u8 *)start + 0x38, "ARM\x64", 4));
    return load_kernel(start, size);
}
static u8 *read_file(const char *name, size_t *length)
{
    FILE *f = fopen(name, "rb");
    assert(f && !fseek(f, 0, SEEK_END));
    long n = ftell(f);
    assert(n > 0 && !fseek(f, 0, SEEK_SET));
    *length = (size_t)n;
    /* Actual malloc redzone at EOF catches memcpy(image_size) overreads. */
    u8 *data = malloc(*length);
    assert(data && fread(data, 1, *length, f) == *length);
    assert(!fclose(f));
    return data;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    size_t fd_len, gz_len;
    u8 *fd = read_file(argv[2], &fd_len);
    u8 *gz = read_file(argv[3], &gz_len);
    uint64_t image_size;
    memcpy(&image_size, fd + 16, sizeof(image_size));
    assert(fd_len % 64 != 0 && image_size > fd_len && image_size < accessible);
    assert((uintptr_t)fd % 8 == 0 && (uintptr_t)fd % KERNEL_ALIGN != 0);

    /* Full production 1GiB extent reserved virtually; only 64KiB accessible.
       No 1GiB physical allocation/memset. This is NOT hardware RAM. */
    size_t mapping_size = (1UL << 30) + KERNEL_ALIGN;
    void *mapping = mmap(NULL, mapping_size, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    heap = (u8 *)(((uintptr_t)mapping + KERNEL_ALIGN - 1) &
                  ~((uintptr_t)KERNEL_ALIGN - 1));
    assert(!mprotect(heap, accessible, PROT_READ | PROT_WRITE));
    memset(heap, 0xa5, accessible);
    /* Neither reserve nor loader may read/write bytes after actual FD end. */
    __asan_poison_memory_region(heap + fd_len, accessible - fd_len);
    assert(__asan_address_is_poisoned(heap + fd_len));
    assert(__asan_address_is_poisoned(gz + gz_len));
    assert(__asan_address_is_poisoned(fd + fd_len));

    if (!strcmp(argv[1], "gzip-known") || !strcmp(argv[1], "gzip-scan")) {
        int scan = !strcmp(argv[1], "gzip-scan");
        void *next = decompress_gz(gz, scan ? 0 : gz_len);
        assert(next == gz + gz_len);  /* compressed endpoint, NOT image_size */
        assert(kernel == (void *)heap && (uintptr_t)kernel % KERNEL_ALIGN == 0);
        assert(!memcmp(kernel, fd, fd_len));
        assert(!copies && dispatches == 1 && calls == 3);
        assert(sizes[0] == 0 && aligns[0] == KERNEL_ALIGN);
        assert(sizes[1] == fd_len && aligns[1] == KERNEL_ALIGN);
        assert(sizes[2] == image_size - fd_len && aligns[2] == 1);
        assert(cursor == image_size);
    } else if (!strcmp(argv[1], "raw-aligned-known") ||
               !strcmp(argv[1], "raw-aligned-unknown")) {
        memcpy(heap, fd, fd_len);
        int unknown = !strcmp(argv[1], "raw-aligned-unknown");
        void *next = load_kernel(heap, unknown ? 0 : fd_len);
        assert(next == (unknown ? NULL : heap + image_size));
        assert(kernel == (void *)heap && !copies && !calls && !dispatches);
        assert(!memcmp(kernel, fd, fd_len));
    } else if (!strcmp(argv[1], "raw-unaligned-known")) {
        void *next = load_kernel(fd, fd_len);
        /* Numeric endpoint: production computes beyond source EOF. */
        assert((uintptr_t)next == (uintptr_t)fd + image_size);
        assert(kernel == (void *)heap && copies == 1 && copied == fd_len);
        assert(calls == 1 && sizes[0] == image_size && aligns[0] == KERNEL_ALIGN);
        assert(!memcmp(kernel, fd, fd_len));
    } else if (!strcmp(argv[1], "gzip-bad-crc")) {
        gz[gz_len - 8] ^= 1;
        assert(!decompress_gz(gz, gz_len));
        assert(!kernel && !dispatches && !copies && calls == 1 && !cursor);
    } else {
        assert(!"unknown test case");
    }
    __asan_unpoison_memory_region(heap, accessible);
    for (size_t i = fd_len; i < accessible; i++)
        assert(heap[i] == 0xa5);  /* Reserve did not zero/copy padding. */
    assert(!munmap(mapping, mapping_size));
    free(fd);
    free(gz);
    puts("PASS: real loader + tinf; host MOCK memory only, not hardware boot");
    return 0;
}
'''


class LoaderPayloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = os.environ.get("M1N1_SOURCE_ROOT")
        if not root:
            raise unittest.SkipTest("Set M1N1_SOURCE_ROOT or --m1n1-source to pinned "
                                    f"{UPSTREAM}@{COMMIT}; no implicit checkout")
        root = Path(root).resolve()
        source = {}
        for rel, expected in HASHES.items():
            data = (root / rel).read_bytes()
            actual = hashlib.sha256(data).hexdigest()
            if actual != expected:
                raise AssertionError(f"Untrusted/drifted source {rel}: {actual}; "
                                     f"expected {UPSTREAM}@{COMMIT} hash {expected}")
            source[rel] = data.decode("utf-8")
        cls.temp = tempfile.TemporaryDirectory(prefix="j313-loader-")
        cls.addClassCleanup(cls.temp.cleanup)
        tmp = Path(cls.temp.name)
        payload = source["src/payload.c"]
        align = extract(payload, r"^#define KERNEL_ALIGN [^\n]+")
        header = extract(source["src/kboot.h"], r"^struct kernel_header \{\n.*?^\};")
        functions = []
        for name in ("load_kernel", "finalize_uncompression", "decompress_gz"):
            # Pinned formatting: top-level closing brace is at column zero.
            # No substitutions within any extracted function body.
            functions.append(extract(payload, rf"^static void \*?{name}\([^\n]+\)\n\{{\n.*?^\}}"))
        (tmp / "LICENSE.m1n1").write_text(source["LICENSE"])
        unit = tmp / "loader_host.c"
        unit.write_text(f"/* Extracted unmodified from {UPSTREAM}@{COMMIT}.\n"
                        " * See accompanying LICENSE.m1n1 (MIT). */\n" + PREAMBLE +
                        align + "\n" + header + "\n" + MOCKS +
                        "\n\n".join(functions) + "\n" + HARNESS)
        # Deliberately non-64-byte-aligned file size exposes reserve alignment.
        fd = bytearray((i * 37 + i // 7) % 256 for i in range(4109))
        fd[:64] = bytes(64)
        struct.pack_into("<Q", fd, 16, 16384)
        fd[0x38:0x3c] = b"ARM\x64"
        cls.fd = tmp / "synthetic-linux-header.fd"
        cls.gz = tmp / "synthetic-linux-header.fd.gz"
        cls.fd.write_bytes(fd)
        cls.gz.write_bytes(gzip.compress(fd, mtime=0))
        cls.binary = tmp / "loader-host"
        cmd = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-I", str(root / "src/tinf"), str(unit),
            *[str(root / "src/tinf" / f) for f in ("tinfgzip.c", "tinflate.c", "crc32.c")],
            "-o", str(cls.binary)]
        subprocess.run(cmd, check=True, timeout=120)

    def run_case(self, name):
        env = dict(os.environ)
        env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1:detect_leaks=0"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(self.binary), name, str(self.fd), str(self.gz)],
                                env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("host MOCK memory only", result.stdout)

    def test_gzip_known_size_alignment_and_reserve(self):
        self.run_case("gzip-known")

    def test_gzip_scanner_unknown_size_stops_at_compressed_eof(self):
        self.run_case("gzip-scan")

    def test_aligned_raw_known_size_is_zero_copy(self):
        self.run_case("raw-aligned-known")

    def test_aligned_raw_unknown_size_is_zero_copy(self):
        self.run_case("raw-aligned-unknown")

    def test_unaligned_raw_copies_only_file_bytes_not_image_size(self):
        self.run_case("raw-unaligned-known")

    def test_real_tinf_crc_failure_does_not_finalize(self):
        self.run_case("gzip-bad-crc")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m1n1-source", type=Path,
                        help="Explicit pinned m1n1 source root; never inferred")
    args, remaining = parser.parse_known_args()
    if args.m1n1_source is not None:
        os.environ["M1N1_SOURCE_ROOT"] = str(args.m1n1_source)
    unittest.main(argv=[__file__, *remaining], verbosity=2)
