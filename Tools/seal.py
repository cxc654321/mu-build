#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Record what a firmware build was, beside the firmware.

The manifest names the device, the features that were on, the source it came
from and the hash of what came out. That is enough to say what is about to run,
which is the only thing a manifest is for.

What it deliberately does not carry, all of which the profile-era manifest did:

  - A profile name and a profile_abi string. There are no profiles.
  - expected_ffs_count and images_verified. Both were pinned per profile and
    both were wrong the moment a module moved -- one was pinned per *target*
    while the count tracks the module set, which is per configuration. The
    build log's own numbers are recorded instead of asserted.
  - A branch name. That never established what an image is; the commit and the
    FD hash do, and they keep doing it whatever the branch is called.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

SCHEMA = "aurora.mu-firmware.v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--with", dest="with_", default="")
    parser.add_argument("--without", default="")
    parser.add_argument("--fd", type=Path, required=True)
    parser.add_argument("--build-log", type=Path, required=True)
    parser.add_argument("--toolchain", required=True)
    args = parser.parse_args()

    sys.path.insert(0, str(args.source_root / "Platform"))
    import Features

    on = sorted(Features.resolve(args.device, args.with_.split(), args.without.split()))
    log = args.build_log.read_text(encoding="utf-8", errors="replace")
    if "PROGRESS - Success" not in log:
        raise SystemExit("build log does not report success")
    images = re.search(r"-+0*(\d+) Images Verified-+", log)

    dirty = git(args.source_root, "status", "--porcelain=v1",
                "--untracked-files=all", "--ignore-submodules=none")
    manifest = {
        "schema": SCHEMA,
        "device": args.device,
        "configuration": args.config,
        "features_on": on,
        "features_off": sorted(set(Features.FEATURES) - set(on)),
        "defines": dict(Features.defines(args.device, args.with_.split(),
                                         args.without.split())),
        "source": {
            "commit": git(args.source_root, "rev-parse", "HEAD"),
            "tree": git(args.source_root, "rev-parse", "HEAD^{tree}"),
            "clean": not dirty,
        },
        "build": {
            "toolchain": args.toolchain,
            "images_verified": int(images.group(1)) if images else None,
            "log": args.build_log.name,
        },
        "firmware": {
            "name": args.fd.name,
            "size": args.fd.stat().st_size,
            "sha256": sha256(args.fd),
        },
    }
    path = args.fd.parent / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    if dirty:
        print(f"warning: sealed from a dirty tree ({len(dirty.splitlines())} path(s))",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
