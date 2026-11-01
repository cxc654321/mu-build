#!/bin/sh
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Get this tree to the point where it can build firmware.
#
# WHY THIS EXISTS
#
# `git submodule update --init --recursive` on this tree downloads about 2 GB,
# most of it MU_BASECORE. If another checkout of mu is already on this machine,
# --reference makes that nearly free. But --reference alone is not enough: the
# clone lands on the *reference's* HEAD, not on the commit this tree pins, and
# a nested submodule can end up at a different commit with no warning. That is
# how BrotliCompress ends up missing brotli/c/common/constants.h and BaseTools
# fails to build with a header-not-found error that says nothing about
# submodules.
#
# So this initialises with --reference where one is available, then walks every
# submodule -- including nested ones -- and forces it to the pinned SHA.
#
#   Tools/setup-build.sh [reference-checkout]
#
# With no argument it looks for a sibling mu checkout, then falls back to a
# plain network clone.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

reference=${1:-}
if [ -z "$reference" ]; then
    for candidate in ../mu ../../AuroraSilicon/mu ~/Developer/AuroraSilicon/mu; do
        if [ -d "$candidate/.git" ] && [ "$(cd "$candidate" && pwd)" != "$root" ]; then
            reference=$(cd "$candidate" && pwd)
            break
        fi
    done
fi

if [ -n "$reference" ]; then
    echo "seeding from $reference"
    git submodule update --init --recursive --reference "$reference" || true
else
    echo "no local reference; cloning from the network"
    git submodule update --init --recursive
fi

# --reference leaves each clone on the reference's HEAD. Pin them.
echo "pinning every submodule to the recorded commit"
fixed=0
pin() {
    parent=$1
    git -C "$parent" ls-tree -r HEAD 2>/dev/null | awk '$2 == "commit" { print $3, $4 }' |
    while read -r sha path; do
        full=$parent/$path
        [ -d "$full/.git" ] || [ -f "$full/.git" ] || continue
        have=$(git -C "$full" rev-parse HEAD 2>/dev/null || echo none)
        if [ "$have" != "$sha" ]; then
            echo "  $full: $have -> $sha"
            git -C "$full" checkout -q -f "$sha" 2>/dev/null || {
                echo "    fetching $sha"
                git -C "$full" fetch -q origin "$sha" 2>/dev/null || git -C "$full" fetch -q origin
                git -C "$full" checkout -q -f "$sha"
            }
        fi
        pin "$full"
    done
}
# A submodule whose parent failed to recurse during init gets cloned at the
# reference's HEAD and is only reachable once the parent exists, so converge
# rather than assuming one pass is enough.
pass=1
while [ $pass -le 4 ]; do
    pin .
    if [ "$(git submodule status --recursive 2>/dev/null | grep -c '^[-+]' || true)" = "0" ]; then
        break
    fi
    pass=$((pass + 1))
done

# Mu publishes BaseTools binaries for Linux and Windows only, and stuart's
# dependency resolver refuses a host it has no entry for:
#
#   ERROR - Verify support for detected host: Host(os='MacOs', arch='ARM', bit='64')
#
# build-windows-native.sh answers that by symlinking a MacOs-ARM-64 directory at
# the C tools it builds from pinned source -- but stuart still has to resolve
# the extdep first, and on a fresh tree there is nothing there to resolve. The
# payload is a download, not source, so seed it from the reference checkout when
# there is one. Clone-on-write, so it costs no disk on APFS.
extdep=MU_BASECORE/BaseTools/Bin/Mu-Basetools_extdep
if [ -n "$reference" ] && [ ! -f "$extdep/extdep_state.yaml" ] &&
   [ -f "$reference/$extdep/extdep_state.yaml" ]; then
    echo "seeding BaseTools extdep from $reference"
    mkdir -p "$extdep"
    for host in Linux-ARM-64 Linux-x86 Windows-ARM-64 Windows-x86; do
        [ -d "$reference/$extdep/$host" ] || continue
        cp -cR "$reference/$extdep/$host" "$extdep/" 2>/dev/null || true
    done
    cp "$reference/$extdep/extdep_state.yaml" "$extdep/"
fi

echo "verifying"
off=$(git submodule status --recursive 2>/dev/null | grep '^[-+]' || true)
# Unit-test frameworks and libspdm's crypto backends are not part of a firmware
# build. They are large (openssl alone dwarfs everything else here), often
# unreachable behind a --reference seed, and nothing in Platform/ depends on
# them, so being off-commit is reported rather than fatal.
optional='UnitTestFrameworkPkg|unit_test|SpdmLib|googletest|cmocka|openssl|mbedtls'
required=$(printf '%s\n' "$off" | grep -v '^$' | grep -Ev "$optional" || true)
if [ -n "$off" ]; then
    printf '%s\n' "$off" | grep -E "$optional" | sed 's/^/  optional, ignored: /' || true
fi
if [ -n "$required" ]; then
    printf '%s\n' "$required" >&2
    echo "error: the above submodule(s) are needed for a firmware build" >&2
    exit 1
fi
echo "every submodule a firmware build needs matches the tree"
