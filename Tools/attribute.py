#!/usr/bin/env python3
"""Say who wrote each file, from the git history rather than from habit.

THE PROBLEM

These trees credit AppleWOA (now NT-for-ASi) in files AppleWOA never had, and
credit nobody in hundreds of files Aurora Silicon wrote outright. Both happened
the same way: a header travels with a copy-paste, and a new file starts from an
old one. Neither is a claim anyone made on purpose, which is exactly why it has
to be fixed from evidence instead of from memory.

WHAT DECIDES IT

The fork point. Every file is one of three things:

  upstream   present in the upstream tree at the fork point. Their copyright
             stands. Aurora's is added only where we have actually changed it.
  derived    we added it, but its content matches an upstream file -- a copy,
             a move, or a template for a new SoC. Both copyrights, theirs
             first, because that is the order the work happened in.
  ours       we added it and no upstream file resembles it. Aurora alone.

"Resembles" is measured, not guessed: difflib ratio against the best-matching
upstream file with the same basename, threshold 0.5.

Where a file already carries someone else's copyright, Aurora's goes after it.
A ported or imported file that lists us first reads as primary authorship of
work that is not ours.

WHAT IT WILL NOT DO

It never removes a copyright, from any file, for any reason. The first version
did: it stripped AppleWOA headers from files the fork's upstream never had, on
the theory that absence meant we wrote it. That deleted a correct notice from
m1n1's hv_psci, which came from AppleWOA by way of ARM Trusted Firmware-A and
which Asahi has never carried. A file can descend from an upstream that is not
the one you forked, so removing a name is never a class decision.

It also skips vendored trees entirely, and never adds Aurora's line to a file
we have not touched. Attribution is only worth fixing if the fix is accurate.
"""

from __future__ import annotations

import argparse
import difflib
import os
import re
import subprocess
import sys
from pathlib import Path

AURORA = "Copyright (c) 2026 Aurora Silicon"
SIMILARITY = 0.5

#
# Third-party code vendored verbatim. These carry their own upstream's
# copyright and licence, recorded alongside them, and putting Aurora's name on
# a Linux dt-bindings header because it is new to *this* repository is the same
# mistake in the opposite direction from stripping AppleWOA's.
#
VENDORED = (
    "Silicon/Apple/AppleSiliconPkg/DeviceTree/",   # Asahi's device trees, see SOURCE.json
    "Silicon/ARM/TIANO/",
    "Common/",
    "umd/vendor/",                                 # Turing Software LLC, MIT
    "rust/vendor/",
)
# Exception: files in a vendored directory that we authored ourselves.
VENDORED_EXCEPTIONS = (
    "Silicon/Apple/AppleSiliconPkg/DeviceTree/t8140-j700.dts",
)

# Comment syntax by extension. EDK2 sources use C block comments even for ASL.
BLOCK = {".c", ".h", ".aslc", ".asl", ".rs", ".java"}
HASH = {".inf", ".dec", ".dsc", ".inc", ".py", ".sh", ".fdf", ".yml", ".yaml"}

APPLEWOA = re.compile(r"^\s*(?:[/*#\s]*)Copyright\s*\(c\).*(?:amarioguy|AppleWOA).*$",
                      re.IGNORECASE)
HAS_AURORA = re.compile(r"Aurora Silicon", re.IGNORECASE)
OTHER_COPYRIGHT = re.compile(r"Copyright\b", re.IGNORECASE)
SPDX = re.compile(r"SPDX-License-Identifier")


def git(repo, *args):
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def classify(repo: Path, fork: str, tracked: list[str]) -> dict[str, str]:
    upstream = set((git(repo, "ls-tree", "-r", "--name-only", fork) or "").split())
    by_base: dict[str, list[str]] = {}
    for p in upstream:
        by_base.setdefault(os.path.basename(p), []).append(p)

    out = {}
    for f in tracked:
        if f in upstream:
            out[f] = "upstream"
            continue
        try:
            mine = (repo / f).read_text(errors="replace")
        except (OSError, UnicodeDecodeError):
            continue
        best = 0.0
        for cand in by_base.get(os.path.basename(f), []):
            other = git(repo, "show", f"{fork}:{cand}")
            if other:
                best = max(best, difflib.SequenceMatcher(None, mine, other).quick_ratio())
        out[f] = "derived" if best >= SIMILARITY else "ours"
    return out


def touched_since(repo: Path, fork: str) -> set[str]:
    diff = git(repo, "diff", "--name-only", f"{fork}..HEAD") or ""
    return set(diff.split())


def comment(path: Path, text: str, like: str | None = None) -> str | None:
    """The copyright line, in the same comment form as the line it joins.

    Form matters, not just prefix. A line that opens and closes its own block
    comment -- `/* SPDX-License-Identifier: MIT */` -- has to be mirrored whole;
    copying only its prefix yields `/* Copyright ...` and swallows the rest of
    the file into an unterminated comment.
    """
    if like is not None:
        stripped = like.strip()
        if stripped.startswith("/*") and stripped.endswith("*/"):
            indent = like[:len(like) - len(like.lstrip())]
            return f"{indent}/* {text} */"
        if stripped.startswith("//"):
            indent = like[:len(like) - len(like.lstrip())]
            return f"{indent}// {text}"
        prefix = re.match(r"^[\s*#]*", like).group(0)
        if prefix.strip():
            return prefix + text
    ext = path.suffix
    if ext in BLOCK:
        return f" * {text}"
    if ext in HASH:
        return f"# {text}"
    return None


def _below_shebang(lines: list[str], at: int) -> int:
    """Never insert above a #! line -- that is what makes the file executable."""
    # Rust inner attributes also start "#!", but they are not shebangs and a
    # comment may precede them. Only a real interpreter line must stay first.
    if at == 0 and lines and lines[0].startswith("#!") and not lines[0].startswith("#!["):
        return 1
    return at


def apply_to(path: Path, kind: str, modified: bool) -> str | None:
    """Rewrite one file's attribution. Returns a description, or None if unchanged."""
    try:
        text = path.read_text()
    except (OSError, UnicodeDecodeError):
        return None
    lines = text.splitlines(keepends=True)
    line = comment(path, AURORA)
    if line is None:
        return None

    #
    # Add only. A file absent from THIS fork's upstream may still be derived
    # from a different one: m1n1's hv_psci.c came from AppleWOA even though
    # Asahi, which m1n1 forks, never had it. Absence is not evidence of
    # authorship, so removing someone's copyright is never a class decision --
    # it needs a file looked at on its own.
    #
    if HAS_AURORA.search(text) or (kind == "upstream" and not modified):
        return None
    removed = 0

    # Convention is Copyright first, then SPDX-License-Identifier. Put the line
    # immediately above the SPDX tag, wearing that line's own comment prefix so
    # it matches whatever indentation the file already uses.
    at = None
    for i, l in enumerate(lines[:40]):
        if not SPDX.search(l):
            continue
        #
        # If someone else's copyright is already in the header, Aurora's goes
        # after it. A ported or imported file that lists us first reads as
        # primary authorship of work that is not ours -- the same overstatement
        # the AppleWOA headers were making, only pointed the other way.
        #
        last_other = None
        for j, prev in enumerate(lines[:30]):
            if OTHER_COPYRIGHT.search(prev) and not HAS_AURORA.search(prev):
                last_other = j
        if last_other is not None:
            at = last_other + 1
            line = comment(path, AURORA, like=lines[last_other]) or line
            break

        stripped = l.strip()
        if stripped.startswith("/*") and not stripped.endswith("*/"):
            # The SPDX tag opens a block comment it does not close. Inserting
            # above it would put the copyright outside every comment; go one
            # line down instead, as the block's first continuation line.
            at = i + 1
            indent = l[:len(l) - len(l.lstrip())]
            line = f"{indent} * {AURORA}"
        else:
            at = i
            line = comment(path, AURORA, like=l) or line
        break
    if at is None:
        for i, l in enumerate(lines[:6]):
            if l.strip() in ("/**", "/*"):
                at = i + 1
                break
    if at is None:
        at = 0
        prefix = "/*\n" + line + "\n */\n\n" if path.suffix in BLOCK else line + "\n\n"
        lines.insert(_below_shebang(lines, 0), prefix)
    else:
        lines.insert(_below_shebang(lines, at), line + "\n")

    path.write_text("".join(lines))
    note = f"added Aurora ({kind})"
    return note + f", removed {removed} AppleWOA line(s)" if removed else note


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("repo", type=Path)
    ap.add_argument("--fork", required=True, help="fork-point commit")
    ap.add_argument("--ext", default=".c,.h,.aslc,.asl,.inf,.dec,.dsc,.inc,.py,.sh,.rs")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    exts = set(args.ext.split(","))
    tracked = [f for f in (git(args.repo, "ls-files") or "").split()
               if Path(f).suffix in exts
               and (f in VENDORED_EXCEPTIONS or not f.startswith(VENDORED))]
    kinds = classify(args.repo, args.fork, tracked)
    modified = touched_since(args.repo, args.fork)

    counts: dict[str, int] = {}
    changes = 0
    for f, kind in sorted(kinds.items()):
        counts[kind] = counts.get(kind, 0) + 1
        if not args.apply:
            continue
        if apply_to(args.repo / f, kind, f in modified):
            changes += 1

    for k in ("upstream", "derived", "ours"):
        print(f"  {k:<10} {counts.get(k, 0)}")
    print(f"  {'changed' if args.apply else 'would change'}: {changes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
