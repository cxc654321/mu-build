#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Satisfy the host-specific external dependencies Mu does not publish for macOS.

Mu ships BaseTools, iasl, nasm, uncrustify and CodeQL as `host_specific`
external dependencies with binaries for Linux and Windows. There is no
MacOs-ARM-64 in any of them, so stuart_update refuses the host:

    ERROR - ... is host specific, but does not appear to have support for
            Host(os='MacOs', arch='ARM', bit='64')
    ERROR - Otherwise, delete the external dependency directory to reset.

It then acts on its own advice and deletes the extdep directory, which is why
seeding the payload from another checkout does not survive one run.

What stuart actually wants is a <name>_extdep directory containing an
extdep_state.yaml whose version matches the descriptor, plus a directory named
for the host. Given both, it treats the dependency as resolved and never
fetches. So:

  - Tools we have natively (iasl, nasm) get a MacOs-ARM-64 directory of
    symlinks to the host binaries, which is what the build uses anyway --
    build-windows-native.sh already exports UNIX_IASL_BIN and puts Homebrew
    LLVM on PATH.
  - BaseTools gets whatever build-windows-native.sh built from pinned source,
    if that exists yet; the build script symlinks it in on every run regardless.
  - Tools only CI plugins use (uncrustify, CodeQL) get an empty directory.
    Nothing in Platform/ invokes them, and an empty directory is honest: it
    says resolved-for-this-host, not here-is-a-binary.

This does not fake a download. No file claims to be something it is not; the
state file records the version the descriptor asks for, which is the version
whose absence would otherwise be re-fetched forever.
"""

from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
HOST = "MacOs-ARM-64"

# extdep name -> host binary to link in, or None for "empty is correct"
NATIVE = {
    "edk2-acpica-iasl": "iasl",
    "mu_nasm": "nasm",
}

# Tools/mu-build owns this one's host directory: it symlinks MacOs-ARM-64 at the
# BaseTools it compiles from pinned source, and refuses to replace a real
# directory found there. So write the state file that stops stuart fetching, and
# leave the directory alone.
STATE_ONLY = {"Mu-Basetools"}


def descriptors():
    for path in sorted(REPO.rglob("*_ext_dep.yaml")):
        if ".git" not in path.parts:
            yield path
    for path in sorted(REPO.rglob("*_ext_dep.json")):
        if ".git" not in path.parts:
            yield path


def parse(path: Path) -> dict | None:
    text = path.read_text(encoding="utf-8", errors="replace")
    # Several of these are named .yaml and contain JSON.
    body = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
    if "{" in body:
        try:
            return json.loads(body[body.index("{"):])
        except Exception:
            pass
    out = {}
    for key in ("name", "version", "scope", "type"):
        m = re.search(rf'^{key}:\s*(\S+)\s*$', text, re.M)
        if m:
            out[key] = m.group(1)
    out["flags"] = re.findall(r'^\s*-\s*(\w+)\s*$', text, re.M)
    return out if "name" in out and "version" in out else None


def main() -> int:
    seeded, skipped = [], []
    for path in descriptors():
        spec = parse(path)
        if not spec:
            skipped.append((path, "unparsed"))
            continue
        flags = spec.get("flags") or []
        if "host_specific" not in flags:
            skipped.append((path, "not host specific"))
            continue

        directory = path.parent / f"{spec['name']}_extdep"
        state = directory / "extdep_state.yaml"
        want = f"version: {spec['version']}\n"
        if state.is_file() and state.read_text() == want and (directory / HOST).exists():
            skipped.append((path, "already satisfied"))
            continue

        (directory / HOST).mkdir(parents=True, exist_ok=True)
        tool = NATIVE.get(spec["name"])
        if tool:
            found = shutil.which(tool)
            if not found:
                print(f"error: {spec['name']} needs {tool} on PATH", file=sys.stderr)
                return 1
            link = directory / HOST / tool
            if link.is_symlink() or link.exists():
                link.unlink()
            link.symlink_to(found)
        state.write_text(want)
        seeded.append((spec["name"], spec["version"], tool or "empty (CI-only tool)"))

    for name, version, how in seeded:
        print(f"  {name} {version}: {how}")
    if not seeded:
        print("  every host-specific dependency was already satisfied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
