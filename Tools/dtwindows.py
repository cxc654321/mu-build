#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Generate MMIO window tables from Asahi's device trees.

WHY

Every per-machine address in AcpiPlatformDxe was read off a live Mac and pasted
into C. Asahi's device trees already carry the same numbers, per node, per
machine, reviewed by people who boot Linux on the hardware -- and they cover 41
Macs across seven SoC families where we describe two.

Reading them at BUILD time rather than reading the ADT at runtime is the whole
point: a node that is missing becomes a build error, not a device that silently
fails to appear on a machine nobody has in front of them.

WHAT IT DOES NOT DO

It reads `reg` and nothing else. Interrupts are not taken from the tree: the
GSIVs we publish are a renumbering of the physical AIC lines that has to agree
with m1n1's alias table and the CSRT, and exists nowhere in any device tree.
Neither are _HID, _DSD names, or AML. See Docs/PLATFORMS.md.

It also does not preprocess. Two of the ninety `reg` properties in
t602x-die0.dtsi use a macro; this refuses those rather than guessing, which is
why it needs no cpp, no dt-bindings headers, and no Linux checkout.

USAGE

    Tools/dtwindows.py generate            # rewrite the generated headers
    Tools/dtwindows.py check               # non-zero if they are stale (CI)
    Tools/dtwindows.py show j414s          # print what it resolved
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DT = REPO / "Silicon/Apple/AppleSiliconPkg/DeviceTree"
OUT = REPO / "Silicon/Apple/AppleSiliconPkg/Include/Platform"

#
# What each machine needs, in the order its _CRS lists them. The node key is a
# device-tree label or a unit name; both are matched. Adding a machine is an
# entry here plus its .dts closure under DeviceTree/.
#
MACHINES = {
    "j414s": {
        "title": "MacBook Pro (14-inch, M2 Pro, 2023)",
        "soc": "T6020",
        "roots": ["t602x-die0.dtsi", "t602x-common.dtsi", "t602x-dieX.dtsi"],
        "groups": {
            "Mca": [
                ("MCA_CLUSTER", "mca@39b600000", 0, "MCA cluster registers"),
                ("MCA_SWITCH", "mca@39b600000", 1, "MCA switch / DMA glue"),
                ("ADMAC", "dma-controller@39b400000", 0, "ADMAC (audio DMA)"),
                ("NCO", "clock-controller@28e03c000", 0, "NCO clock generator"),
                ("I2C1", "i2c@39b044000", 0, "i2c1 -- left amps"),
                ("I2C3", "i2c@39b04c000", 0, "i2c3 -- right amps"),
                ("PINCTRL_AP", "pinctrl@39b028000", 0, "pinctrl_ap -- speaker SDZ is pin 57"),
            ],
        },
    },
}

NODE = re.compile(
    r'(?:(?:DIE_NODE\s*\(\s*)?(?P<label>[\w-]+)\s*\)?\s*:\s*)?'
    r'(?P<name>[\w,.+-]+@[0-9a-fA-F]+)\s*\{', re.M)
REG = re.compile(r'\breg\s*=\s*(.*?);', re.S)
CELLS = re.compile(r'<([^>]*)>')
CELL = re.compile(r'^(?:0[xX][0-9a-fA-F]+|\d+)$')


class Unresolved(Exception):
    pass


def node_bodies(text: str) -> dict[str, str]:
    """Map every label and unit name to its brace body."""
    found: dict[str, str] = {}
    for m in NODE.finditer(text):
        start = text.index("{", m.start())
        depth, end = 0, start
        for i in range(start, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        body = text[start + 1:end]
        found[m.group("name")] = body
        if m.group("label"):
            found[m.group("label")] = body
    return found


def window(body: str, node: str, index: int) -> tuple[int, int]:
    """The node's index'th reg entry, as (base, size).

    A node can describe several windows -- mca@39b600000 has two, and our _CRS
    lists both -- so the index is part of what a machine asks for.
    """
    m = REG.search(body)
    if not m:
        raise Unresolved(f"{node}: no reg property")
    # A multi-window node writes <a b c d>, <e f g h>; across several lines.
    cells = " ".join(CELLS.findall(m.group(1))).split()
    if not cells or len(cells) % 4:
        raise Unresolved(f"{node}: reg has {len(cells)} cells, not a multiple of 4")
    entries = len(cells) // 4
    if index >= entries:
        raise Unresolved(f"{node}: reg[{index}] requested, node has {entries}")
    quad = cells[index * 4:index * 4 + 4]
    if not all(CELL.match(c) for c in quad):
        raise Unresolved(f"{node}: reg[{index}] is not literal ({' '.join(quad)})")
    hi, lo, shi, slo = (int(c, 0) for c in quad)
    return (hi << 32) | lo, (shi << 32) | slo


def resolve(machine: str) -> dict[str, list[tuple[str, str, int, int]]]:
    spec = MACHINES[machine]
    bodies: dict[str, str] = {}
    for root in spec["roots"]:
        path = DT / root
        if not path.is_file():
            raise Unresolved(f"{machine}: missing vendored {root}")
        bodies.update(node_bodies(path.read_text(encoding="utf-8", errors="replace")))
    out = {}
    for group, nodes in spec["groups"].items():
        rows = []
        for key, node, index, why in nodes:
            if node not in bodies:
                raise Unresolved(f"{machine}: {node} not in {', '.join(spec['roots'])}")
            base, size = window(bodies[node], node, index)
            label = node if index == 0 else f"{node} reg[{index}]"
            rows.append((key, label, why, base, size))
        out[group] = rows
    return out


def header_for(machine: str) -> str:
    spec = MACHINES[machine]
    source = json.loads((DT / "SOURCE.json").read_text())
    resolved = resolve(machine)
    guard = f"NTASI_{machine.upper()}_WINDOWS_H_"
    lines = [
        "/** @file",
        f"  {spec['title']} MMIO windows, generated from Asahi's device trees.",
        "",
        "  DO NOT EDIT. Regenerate with Tools/dtwindows.py generate.",
        "",
        f"  Source: {source['repo']} @ {source['commit'][:12]}",
        f"          {source['path']}",
        "  Those files are GPL-2.0+ OR MIT; the addresses below are taken under MIT.",
        "",
        "  Only `reg` comes from the tree. Interrupts do not: the GSIVs we publish are",
        "  a renumbering of the physical AIC lines that must agree with m1n1's alias",
        "  table and the CSRT, and exists in no device tree. See Docs/PLATFORMS.md.",
        "",
        "  Copyright (c) 2026 Aurora Silicon",
        "  SPDX-License-Identifier: MIT",
        "**/",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    for group, rows in resolved.items():
        lines.append(f"//")
        lines.append(f"// {group}. One macro per window so a table can list them in _CRS order")
        lines.append(f"// and interleave the entries that no device-tree node describes.")
        lines.append(f"//")
        keyw = max(len(k) for k, _, _, _, _ in rows)
        for key, node, why, base, size in rows:
            name = f"NTASI_{machine.upper()}_W_{key}"
            lines.append(f"/* {why} -- {node} */")
            lines.append(f"#define {name.ljust(keyw + 16)} {{ 0x{base:X}ULL, 0x{size:X}ULL }}")
        lines.append("")
    lines += [f"#endif // {guard}", ""]
    return "\n".join(lines)


def generate(write: bool) -> int:
    stale = 0
    for machine in sorted(MACHINES):
        path = OUT / f"Ntasi{machine.capitalize()}Windows.h"
        want = header_for(machine)
        have = path.read_text(encoding="utf-8") if path.is_file() else None
        if have == want:
            print(f"  {path.relative_to(REPO)}: up to date")
            continue
        stale += 1
        if write:
            path.write_text(want, encoding="utf-8")
            print(f"  {path.relative_to(REPO)}: written")
        else:
            print(f"  {path.relative_to(REPO)}: STALE", file=sys.stderr)
    return stale


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("generate", "check", "show"))
    parser.add_argument("machine", nargs="?")
    args = parser.parse_args()

    if args.command == "show":
        machine = args.machine or next(iter(MACHINES))
        for group, rows in resolve(machine).items():
            print(f"{machine} {group}:")
            for node, why, base, size in rows:
                print(f"  0x{base:011X}  0x{size:08X}  {node:32} {why}")
        return 0
    stale = generate(write=args.command == "generate")
    if args.command == "check" and stale:
        print(f"\n{stale} generated header(s) are stale; run Tools/dtwindows.py generate",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Unresolved as error:
        raise SystemExit(f"dtwindows: {error}")
