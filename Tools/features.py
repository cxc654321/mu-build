#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Command-line face of Platform/Features.py, for the shell to call.

Kept separate so Tools/mu-build needs no inline Python and Features.py stays a
plain data module both the build and PlatformBuild.py import.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Platform"))
import Features  # noqa: E402


def main(argv: list[str]) -> int:
    if argv[:1] == ["--list"]:
        print("devices:")
        for name, spec in sorted(Features.DEVICES.items()):
            print(f"  {name:8} {spec['description']}")
        print("\nfeatures:")
        for name in sorted(Features.FEATURES):
            print(f"  {name}")
        print("\ngroups:")
        for name, members in sorted(Features.GROUPS.items()):
            print(f"  {name:8} = {' '.join(members)}")
        return 0

    if argv[:1] == ["--resolve"] and len(argv) == 4:
        device, with_, without = argv[1], argv[2].split(), argv[3].split()
        plat = Features.platform(device)
        print(plat["pkg"])
        print(plat["platform"])
        print(Features.slug(device, with_, without))
        print(plat["fd"])
        return 0

    if argv[:1] == ["--show"] and len(argv) == 4:
        print(Features.summary(argv[1], argv[2].split(), argv[3].split()))
        return 0

    print("usage: features.py --list | --resolve DEVICE WITH WITHOUT | "
          "--show DEVICE WITH WITHOUT", file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (ValueError, Features.UnknownFeature) as error:
        raise SystemExit(str(error))
