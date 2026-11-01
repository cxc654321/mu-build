#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Read SoC-level facts out of a compiled Asahi device tree.

WHY A SECOND DEVICE-TREE READER

Tools/dtwindows.py reads `reg` out of the .dtsi *text* and refuses anything it
cannot resolve without cpp. That is the right trade for the per-machine MMIO
windows it generates: those live in a handful of nodes, they are macro-free, and
keeping the tool free of a Linux checkout means CI can check the generated
headers are current.

SoC-family facts are a different shape. CPU topology is spread across an include
chain, PCIe windows arrive through `ranges` with cell counts declared by an
ancestor, and the MMIO map is the union of ninety-odd `reg` properties from
every node under /soc. Answering those from text means reimplementing cpp and
the FDT cell rules. So this reads the compiled tree instead, where every macro
is already resolved and the cell counts are stated by the tree itself.

The trade is that it needs a DTB. That is fine, because unlike dtwindows.py this
is not a build step: it runs once when a SoC family package is created, and its
output is committed as static C and DEC files that are then reviewed and owned
by hand. See Tools/add-soc.py.

    Tools/socfacts.py t8112-j413.dtb
    Tools/socfacts.py t8112-j413.dtb --json

BUILDING A DTB

Asahi's tree needs its own dt-bindings headers and is built with a patched dtc
that tolerates two spellings upstream rejects. Tools/mkdtb.sh does both.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fdt  # noqa: E402

GB = 1 << 30
MB = 1 << 20

#
# Apple's core designations, oldest first. The device tree names the
# microarchitecture rather than the role, so this is the lookup that says which
# of them is an efficiency core. Efficiency class 0 is the slow one: that is the
# ACPI convention and also the order Windows expects to see in the PPTT.
#
E_CORES = {
    "apple,icestorm",   # M1, M1 Pro/Max/Ultra
    "apple,blizzard",   # M2, M2 Pro/Max/Ultra
    "apple,sawtooth",   # M3, M3 Pro/Max/Ultra
    "apple,donan-e",    # M4  (t8132)
    "apple,tahiti-e",   # A18 Pro (t8140). Not an upstream spelling -- see
                        # DeviceTree/t8140-j700.dts, which authors it from
                        # m1n1's MIDR part name for this core.
}
P_CORES = {
    "apple,firestorm",  # M1, M1 Pro/Max/Ultra
    "apple,avalanche",  # M2, M2 Pro/Max/Ultra
    "apple,everest",    # M3, M3 Pro/Max/Ultra
    "apple,donan-p",    # M4  (t8132)
    "apple,tahiti-p",   # A18 Pro (t8140)
}


def _caches(root):
    """phandle -> node, for every node that looks like a cache."""
    out = {}
    for n in root.walk():
        ph = n.u32("phandle")
        if ph is None:
            continue
        if "cache-level" in n.props or "cache-size" in n.props or "cache" in n.name:
            out[ph] = n
    return out


def cpu_topology(root):
    """Clusters in MPIDR order, each with its core count, UIDs and L2 size.

    This is the SoC *ceiling*. Apple bins these parts, so a given machine may
    have fewer cores enabled than the tree describes -- see the note in
    Tools/add-soc.py about why that is safe here.
    """
    cpus = root.child("cpus")
    if cpus is None:
        raise SystemExit("device tree has no /cpus node")
    caches = _caches(root)

    cores = []
    for c in cpus.children:
        if not c.name.startswith("cpu@"):
            continue
        mpidr = int(c.name.split("@", 1)[1], 16)
        comp = c.str_("compatible", "")
        l2 = caches.get(c.u32("next-level-cache"))
        cores.append({
            "mpidr": mpidr,
            "cluster": (mpidr >> 8) & 0xFF,
            "compatible": comp,
            "kind": "E" if comp in E_CORES else "P" if comp in P_CORES else "?",
            "l2_size": l2.u32("cache-size") if l2 is not None else None,
            "i_cache_size": c.u32("i-cache-size"),
            "d_cache_size": c.u32("d-cache-size"),
        })
    cores.sort(key=lambda c: c["mpidr"])

    unknown = sorted({c["compatible"] for c in cores if c["kind"] == "?"})
    if unknown:
        raise SystemExit(
            "unclassified core type(s): %s\n"
            "Add them to E_CORES or P_CORES in %s -- guessing which of these is "
            "an efficiency core would silently mis-order the PPTT."
            % (", ".join(unknown), Path(__file__).name)
        )

    clusters = []
    uid = 0
    for cid in sorted({c["cluster"] for c in cores}):
        members = [c for c in cores if c["cluster"] == cid]
        clusters.append({
            "id": cid,
            "kind": members[0]["kind"],
            "count": len(members),
            "uid_base": uid,
            "l2_size": members[0]["l2_size"],
            "i_cache_size": members[0]["i_cache_size"],
            "d_cache_size": members[0]["d_cache_size"],
            "mpidrs": [c["mpidr"] for c in members],
        })
        uid += len(members)

    return {
        "cpu_count": len(cores),
        "cluster_count": len(clusters),
        "clusters": clusters,
        # Homogeneous means every cluster is the same core type, which is what
        # decides whether the PPTT may advertise a single efficiency class.
        "homogeneous": len({c["kind"] for c in clusters}) == 1,
    }


def _pcie_controllers(root):
    out = []
    for soc in soc_nodes(root):
        for n in soc.children:
            if not n.name.startswith("pcie@"):
                continue
            regs = n.reg()
            out.append({
                "name": n.name,
                "die": soc.name,
                "compatible": n.strlist("compatible"),
                "ecam_base": regs[0][0] if regs else None,
                "ecam_size": regs[0][1] if regs else None,
                "ranges": _ranges(n),
            })
    return out


def _ranges(node):
    """Decode a PCI `ranges` property into (flags, child, parent, size)."""
    c = node.cells("ranges")
    if not c:
        return []
    pac = node.u32("#address-cells", 3)
    psc = node.u32("#size-cells", 2)
    # Parent address cells come from the node's own parent.
    parent_ac = node.parent.u32("#address-cells", 2) if node.parent else 2
    step = pac + parent_ac + psc
    out = []
    for i in range(0, len(c) - step + 1, step):
        flags = c[i]
        child = 0
        for x in c[i + 1: i + pac]:
            child = (child << 32) | x
        parent = 0
        for x in c[i + pac: i + pac + parent_ac]:
            parent = (parent << 32) | x
        size = 0
        for x in c[i + pac + parent_ac: i + step]:
            size = (size << 32) | x
        out.append({
            "flags": flags,
            "space": {0: "config", 1: "io", 2: "mem32", 3: "mem64"}.get((flags >> 24) & 3),
            "prefetchable": bool(flags & (1 << 30)),
            "child": child,
            "parent": parent,
            "size": size,
        })
    return out


def _range_map(node):
    """This node's `ranges` as [(child, parent, size), ...] in address order.

    Returns None when the node has no `ranges` at all, which is the device-tree
    way of saying its children live in a private address space that does not map
    into the parent's -- an i2c bus, a SPI bus, an OF graph, or a device with
    numbered sub-blocks. An empty `ranges` is the identity map.
    """
    raw = node.props.get("ranges")
    if raw is None:
        return None
    if len(raw) == 0:
        return []          # identity
    cells = node.cells("ranges")
    ac = node.u32("#address-cells", 2)
    sc = node.u32("#size-cells", 2)
    pac = node.parent.u32("#address-cells", 2) if node.parent else 2
    step = ac + pac + sc
    out = []
    for i in range(0, len(cells) - step + 1, step):
        def join(lo, hi):
            v = 0
            for x in cells[lo:hi]:
                v = (v << 32) | x
            return v
        out.append((join(i, i + ac),
                    join(i + ac, i + ac + pac),
                    join(i + ac + pac, i + step)))
    return out


def _cpu_addressed(node, translate=lambda a: a):
    """Yield (node, cpu_base, size) for every `reg` that reaches a CPU address.

    A node's `reg` is expressed in its parent's address space, so it is a CPU
    address only once every bus between it and the root has translated it. Three
    cases matter here:

      * empty `ranges`  -- identity, the simple-bus case, keep going
      * `ranges` triples -- translate, which is how the Ultra parts put die 1
        at the same child addresses as die 0 offset by 0x2000000000
      * no `ranges` at all -- children are in a private space, stop

    Stopping is the important one. Without it a graph `port@0` saying
    `reg = <0>` becomes a request to map physical page zero.
    """
    stack = [(node, translate)]
    while stack:
        n, tr = stack.pop()
        if tr is not None:
            for base, size in n.reg():
                mapped = tr(base)
                if mapped is not None:
                    yield n, mapped, size

        # A PCI bus translates too, but its children's `reg` is a config-space
        # B/D/F triple rather than an address. The BAR windows we do want come
        # from its `ranges`, decoded separately.
        if n.str_("device_type") == "pci":
            continue

        rmap = _range_map(n)
        if rmap is None:
            child_tr = None
        elif not rmap:
            child_tr = tr
        else:
            def child_tr(addr, rmap=rmap, tr=tr):
                for child, parent, size in rmap:
                    if child <= addr < child + size:
                        return tr(addr - child + parent)
                return None
        for c in n.children:
            stack.append((c, child_tr))


def soc_nodes(root):
    """Every top-level SoC bus. The Ultra parts have one per die."""
    return [c for c in root.children if c.name == "soc" or c.name.startswith("soc@")]


def mmio_windows(root, granule=GB):
    """Aligned blocks covering every CPU-addressed `reg` under /soc, coalesced.

    The virtual memory map maps these as device memory before any driver runs,
    so the requirement is coverage, not tightness: a block spanning a hole costs
    nothing because nothing dereferences it, whereas a peripheral outside every
    block faults the first time it is touched.

    That failure mode is not hypothetical. T602X's hand-written table left a 3GB
    hole at [0x2C0000000, 0x380000000) containing ANS, and it went unnoticed
    until ANS was enabled for the first time and the first SART register read
    took a translation fault. It still leaves a second hole at
    [0x300000000, 0x340000000) over dcpext and its two DARTs, which nothing has
    touched yet. Deriving the windows from the tree that already declares those
    nodes is what makes that class of bug impossible rather than merely fixed.
    """
    socs = soc_nodes(root)
    if not socs:
        raise SystemExit("device tree has no /soc node")

    blocks = set()
    for soc in socs:
        for _n, base, size in _cpu_addressed(soc):
            if size == 0:
                continue
            start = base & ~(granule - 1)
            end = (base + size + granule - 1) & ~(granule - 1)
            blocks.update(range(start, end, granule))

    merged = []
    for b in sorted(blocks):
        if merged and merged[-1][0] + merged[-1][1] == b:
            merged[-1][1] += granule
        else:
            merged.append([b, granule])
    return [tuple(m) for m in merged]


def facts(dtb: Path):
    root = fdt.parse(dtb)
    socs = soc_nodes(root)
    soc = socs[0] if socs else None
    compat = root.strlist("compatible")
    soc_name = next((c.split(",")[1] for c in compat if c.startswith("apple,t")), None)

    mem = root.child("memory")
    serial = None
    aliases = root.child("aliases")
    if aliases is not None:
        path = aliases.str_("serial0")
        if path:
            serial = root.at(path)
    if serial is None and soc is not None:
        serial = soc.child("serial")

    aic = None
    for n in (soc.children if soc else []):
        if n.name.startswith("interrupt-controller@"):
            aic = n
            break

    return {
        "model": root.str_("model"),
        "compatible": compat,
        "soc": soc_name,
        "soc_id": int(soc_name[1:], 16) if soc_name else None,
        "memory_base": mem.reg()[0][0] if mem is not None and mem.reg() else None,
        "memory_size": mem.reg()[0][1] if mem is not None and mem.reg() else None,
        "uart_base": serial.reg()[0][0] if serial is not None and serial.reg() else None,
        "aic": {
            "compatible": aic.strlist("compatible"),
            "base": aic.reg()[0][0],
            "size": aic.reg()[0][1],
        } if aic is not None and aic.reg() else None,
        "cpu": cpu_topology(root),
        "pcie": _pcie_controllers(root),
        "mmio": [{"base": b, "size": s} for b, s in mmio_windows(root)],
    }


def _h(v):
    return hex(v) if isinstance(v, int) else v


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dtb", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    f = facts(args.dtb)
    if args.json:
        print(json.dumps(f, indent=2))
        return 0

    print(f"{f['soc']}  {f['model']}")
    print(f"  DRAM        {_h(f['memory_base'])} size {_h(f['memory_size'])}")
    print(f"  UART0       {_h(f['uart_base'])}")
    if f["aic"]:
        print(f"  AIC         {_h(f['aic']['base'])} {f['aic']['compatible'][0]}")
    c = f["cpu"]
    print(f"  CPUs        {c['cpu_count']} in {c['cluster_count']} cluster(s)"
          f"{' (homogeneous)' if c['homogeneous'] else ''}")
    for cl in c["clusters"]:
        print(f"    cluster {cl['id']}  {cl['count']}x{cl['kind']}  "
              f"uid {cl['uid_base']}..{cl['uid_base'] + cl['count'] - 1}  "
              f"L2 {_h(cl['l2_size'])}  L1i {_h(cl['i_cache_size'])} L1d {_h(cl['d_cache_size'])}")
    for p in f["pcie"]:
        print(f"  {p['name']}  ecam {_h(p['ecam_base'])} size {_h(p['ecam_size'])}")
        for r in p["ranges"]:
            print(f"    {r['space']:6} child {_h(r['child']):>14} -> "
                  f"parent {_h(r['parent']):>14} size {_h(r['size'])}")
    print(f"  MMIO        {len(f['mmio'])} window(s)")
    for w in f["mmio"]:
        print(f"    {_h(w['base']):>14} size {_h(w['size'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
