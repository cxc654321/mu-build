# Copyright (c) 2026 Aurora Silicon

"""Minimal flattened-device-tree reader.

Enough of the FDT spec to answer structural questions about a compiled Apple
device tree: node tree, raw property bytes, and `reg` decoded against the
address/size cells actually in force at that point in the tree.
"""
from __future__ import annotations
import struct

MAGIC = 0xD00DFEED
BEGIN_NODE, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


class Node:
    __slots__ = ("name", "props", "children", "parent")

    def __init__(self, name, parent=None):
        self.name = name
        self.props: dict[str, bytes] = {}
        self.children: list[Node] = []
        self.parent = parent

    @property
    def path(self):
        parts, n = [], self
        while n and n.parent is not None:
            parts.append(n.name)
            n = n.parent
        return "/" + "/".join(reversed(parts))

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def child(self, name):
        for c in self.children:
            if c.name == name or c.name.split("@")[0] == name:
                return c
        return None

    def at(self, path):
        n = self
        for part in path.strip("/").split("/"):
            if not part:
                continue
            n = n.child(part)
            if n is None:
                return None
        return n

    # -- typed property access -------------------------------------------------
    def str_(self, key, default=None):
        v = self.props.get(key)
        return v.split(b"\0")[0].decode() if v else default

    def strlist(self, key):
        v = self.props.get(key)
        return [s.decode() for s in v.split(b"\0") if s] if v else []

    def cells(self, key):
        v = self.props.get(key)
        if v is None:
            return None
        return list(struct.unpack(">%dI" % (len(v) // 4), v[: len(v) // 4 * 4]))

    def u32(self, key, default=None):
        c = self.cells(key)
        return c[0] if c else default

    def _cells_cfg(self):
        p = self.parent
        ac = p.u32("#address-cells", 2) if p else 2
        sc = p.u32("#size-cells", 1) if p else 1
        return ac, sc

    def reg(self):
        """[(address, size), ...] decoded with this node's parent cell counts."""
        c = self.cells("reg")
        if not c:
            return []
        ac, sc = self._cells_cfg()
        step = ac + sc
        out = []
        for i in range(0, len(c) - step + 1, step):
            a = 0
            for x in c[i : i + ac]:
                a = (a << 32) | x
            s = 0
            for x in c[i + ac : i + step]:
                s = (s << 32) | x
            out.append((a, s))
        return out


def parse(path):
    blob = open(path, "rb").read()
    magic, _tot, off_s, off_str, _off_rsv, _v, _lcv, _boot, size_str, size_s = \
        struct.unpack(">10I", blob[:40])
    if magic != MAGIC:
        raise ValueError("%s: not a DTB" % path)
    struct_blk = blob[off_s : off_s + size_s]
    strings = blob[off_str : off_str + size_str]

    root = None
    cur = None
    i = 0
    while i < len(struct_blk):
        (tok,) = struct.unpack_from(">I", struct_blk, i)
        i += 4
        if tok == BEGIN_NODE:
            end = struct_blk.index(b"\0", i)
            name = struct_blk[i:end].decode()
            i = (end + 4) & ~3
            node = Node(name, cur)
            if root is None:
                root = node
            else:
                cur.children.append(node)
            cur = node
        elif tok == END_NODE:
            cur = cur.parent
        elif tok == PROP:
            ln, noff = struct.unpack_from(">II", struct_blk, i)
            i += 8
            val = struct_blk[i : i + ln]
            i = (i + ln + 3) & ~3
            key = strings[noff : strings.index(b"\0", noff)].decode()
            cur.props[key] = val
        elif tok == NOP:
            continue
        elif tok == END:
            break
        else:
            raise ValueError("bad token %d at %d" % (tok, i - 4))
    return root
