#!/usr/bin/env python3
"""Offline J313 packer: loader | DTB | gzip(FD) | four zeros | 16 KiB padding.

Usage: python3 Tools/pack_j313_kboot.py LOADER DTB FD OUTPUT
The JSON manifest is printed to stdout; OUTPUT is exclusively created. No deployment.

Audited against ../m1n1-ci/src/payload.c and src/tinf/tinfgzip.c:
inline gzip accepts source_len=0 and returns consumed bytes; decompression uses a
2 MiB-aligned destination and passes the actual length to load_kernel, then
finalize_uncompression reserves the declared image_size without a raw overread.
The 1 GiB cap is a loader/packer limit, NOT proof of available memory. This tool
does not authenticate the loader, validate DT compatibility, or certify boot safety.
Deterministic gzip uses no filename and mtime=0 (same Python/zlib implementation).
"""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

ALIGN = 16 * 1024
MAX_IMAGE_SIZE = 1 << 30
ARM64_HEADER_SIZE = 64


def validate_dtb(dtb):
    """Validate v17 header/block bounds, not full device-tree semantics."""
    if len(dtb) < 40:
        raise ValueError("DTB header truncated (need 40 bytes)")
    (magic, total, structure, strings, reserve, version, compatible,
     _cpu, strings_size, structure_size) = struct.unpack_from(">10I", dtb)
    if magic != 0xD00DFEED:
        raise ValueError("DTB magic mismatch")
    # Exact equality is essential: the inline scanner advances by totalsize.
    if total != len(dtb):
        raise ValueError("DTB totalsize must equal input length")
    if version != 17 or compatible > 17 or compatible < 16:
        raise ValueError("DTB requires version 17 and compatible version 16 or 17")
    blocks = []
    for name, offset, size in (("structure", structure, structure_size),
                               ("strings", strings, strings_size)):
        if offset < 40 or offset > total or size > total - offset:
            raise ValueError("DTB " + name + " block outside totalsize")
        blocks.append((offset, offset + size))
    if structure % 4 or structure_size < 4 or structure_size % 4:
        raise ValueError("DTB structure alignment/size invalid")
    if reserve < 40 or reserve % 8 or reserve > total - 16:
        raise ValueError("DTB reservation map offset invalid")
    end = reserve
    while True:
        if end + 16 > total:
            raise ValueError("DTB reservation map unterminated")
        address, size = struct.unpack_from(">QQ", dtb, end)
        end += 16
        if address == 0 and size == 0:
            break
    blocks.append((reserve, end))
    for i, (start, end) in enumerate(blocks):
        for other_start, other_end in blocks[i + 1:]:
            if start < other_end and other_start < end:
                raise ValueError("DTB blocks overlap")


def validate_fd(fd, max_image_size=MAX_IMAGE_SIZE):
    if not ARM64_HEADER_SIZE <= max_image_size <= MAX_IMAGE_SIZE:
        raise ValueError("image size limit must be between 64 bytes and 1 GiB")
    if len(fd) < ARM64_HEADER_SIZE:
        raise ValueError("FD ARM64 header truncated (need 64 bytes)")
    if fd[0x38:0x3c] != b"ARM\x64":
        raise ValueError("FD ARM64 magic mismatch")
    image_size = struct.unpack_from("<Q", fd, 0x10)[0]
    if image_size < len(fd):
        raise ValueError("FD image_size smaller than actual FD length")
    if image_size > max_image_size:
        raise ValueError("FD image_size exceeds configured limit (at most 1 GiB)")
    return image_size


def fingerprint(data):
    return {"length": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def build_package(loader, dtb, fd, max_image_size=MAX_IMAGE_SIZE):
    """Return packed bytes and a JSON-compatible manifest; never modify FD."""
    if not loader or len(loader) % ALIGN:
        raise ValueError("loader must be nonempty and 16 KiB aligned")
    validate_dtb(dtb)
    image_size = validate_fd(fd, max_image_size)
    stream = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=stream,
                       compresslevel=9, mtime=0) as compressed:
        compressed.write(fd)
    payload = stream.getvalue()
    offset = len(loader) + len(dtb)
    blob = loader + dtb + payload + bytes(4)
    padding = (-len(blob)) % ALIGN
    # The scanner probes ARM64 magic at +0x38 before checking four zeros.
    # Retain the four-byte terminator, but guarantee 64 readable zero bytes.
    if padding < 60:
        padding += ALIGN
    blob += bytes(padding)
    manifest = {
        "format": "j313-kboot-gzip-v1",
        "layout": "loader|DTB|gzip(FD)|4 zero bytes|16 KiB padding",
        "inputs": {"loader": fingerprint(loader), "dtb": fingerprint(dtb),
                   "fd": fingerprint(fd)},
        "output": fingerprint(blob),
        "fd_declared_reserve_size": image_size,
        "max_image_size": max_image_size,
        "gzip": {"offset": offset, "length": len(payload), "mtime": 0},
        "terminator": {"offset": offset + len(payload), "length": 4},
        "padding_length": padding,
        "deployed": False,
        "warning": "Size limit is not proof of free memory or boot safety.",
    }
    return blob, manifest


def pack_files(loader_path, dtb_path, fd_path, output_path,
               max_image_size=MAX_IMAGE_SIZE):
    """Read inputs, validate, then exclusively create output (including symlinks)."""
    blob, manifest = build_package(Path(loader_path).read_bytes(),
                                   Path(dtb_path).read_bytes(),
                                   Path(fd_path).read_bytes(), max_image_size)
    with Path(output_path).open("xb") as output:
        output.write(blob)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("loader", "dtb", "fd", "output"):
        parser.add_argument(name)
    parser.add_argument("--max-image-size", type=lambda value: int(value, 0),
                        default=MAX_IMAGE_SIZE,
                        help="FD reserve-size cap in bytes, at most 1 GiB; not a memory guarantee")
    args = parser.parse_args(argv)
    try:
        manifest = pack_files(args.loader, args.dtb, args.fd, args.output,
                              args.max_image_size)
    except (OSError, ValueError) as error:
        parser.exit(1, "pack_j313_kboot: " + str(error) + "\n")
    json.dump(manifest, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
