#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Rewrite the two cell spellings upstream dtc rejects. Used by Tools/mkdtb.sh.

Asahi builds its device trees with a patched dtc. Two of its tolerances show up
in the vendored trees, both only ever in GPU pstate-tuning properties:

    <34.0>   fixed-point literal   -> <34>
    <-50>    bare negative literal -> <(-50)>

Neither spelling appears in any property Tools/socfacts.py reads -- CPU
topology, memory, interrupt controller, MMIO `reg`, PCIe `ranges` -- so neither
rewrite can change a derived fact. The lookarounds require a cell delimiter or
separator on both sides, which keeps them off string contents such as
compatible = "foo-1.0". Counts go to stderr so the substitution is never silent.
"""

import re
import sys


def main() -> int:
    src = sys.stdin.read()
    src, floats = re.subn(r'(?<=[\s<(])(-?\d+)\.\d+(?=[\s>),])', r'\1', src)
    src, negatives = re.subn(r'(?<=[\s<])(-\d+)(?=[\s>,])', r'(\1)', src)
    sys.stdout.write(src)
    if floats or negatives:
        sys.stderr.write(
            "dtcompat: rewrote %d fixed-point and %d bare-negative cell literals\n"
            % (floats, negatives)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
