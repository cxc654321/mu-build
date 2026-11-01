#!/bin/sh
# Copyright (c) 2026 Aurora Silicon

# Compile a vendored Asahi device tree to a DTB.
#
# Tools/socfacts.py and Tools/add-soc.py read compiled trees rather than .dtsi
# text, because SoC-level facts are spread across an include chain and depend on
# cell counts the tree itself declares. This turns the vendored sources under
# Silicon/Apple/AppleSiliconPkg/DeviceTree into those compiled trees.
#
# Everything it needs is vendored: the tree closure and the dt-bindings headers
# both live in that directory, so this needs no Linux checkout. It does need a C
# preprocessor and dtc.
#
#   Tools/mkdtb.sh t8112-j413            -> build-out-devicetree/t8112-j413.dtb
#   Tools/mkdtb.sh --all
#
# Two spellings need rewriting on the way through. Asahi builds with a patched
# dtc that accepts fixed-point cell literals (<34.0>) and bare negative ones
# (<-50>); upstream dtc rejects both. They appear only in GPU pstate-tuning
# properties, which nothing here reads. The rewrite is reported, never silent.

set -e

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dts_dir="$root/Silicon/Apple/AppleSiliconPkg/DeviceTree"
# Not under Build/: Tools/mu-build refuses to run if that exists, so that
# native output stays isolated from a container build. build-out* is
# already ignored.
out_dir="${NTASI_DTB_DIR:-$root/build-out-devicetree}"

cpp_bin=${CPP:-}
if [ -z "$cpp_bin" ]; then
    for c in clang gcc cc; do
        if command -v "$c" >/dev/null 2>&1; then cpp_bin=$c; break; fi
    done
fi
[ -n "$cpp_bin" ] || { echo "mkdtb: no C preprocessor found; set CPP=" >&2; exit 1; }
command -v dtc >/dev/null 2>&1 || {
    echo "mkdtb: dtc not found (brew install dtc / apt-get install device-tree-compiler)" >&2
    exit 1
}

build_one() {
    name=$1
    src="$dts_dir/$name.dts"
    [ -f "$src" ] || { echo "mkdtb: no such tree: $name" >&2; return 1; }
    mkdir -p "$out_dir"
    "$cpp_bin" -E -P -nostdinc \
        -I "$dts_dir/include" -I "$dts_dir" \
        -undef -D__DTS__ -x assembler-with-cpp "$src" \
      | python3 "$root/Tools/dtcompat.py" \
      > "$out_dir/$name.pp"
    dtc -q -I dts -O dtb -o "$out_dir/$name.dtb" "$out_dir/$name.pp"
    rm -f "$out_dir/$name.pp"
    echo "$out_dir/$name.dtb"
}

if [ "$1" = "--all" ]; then
    for f in "$dts_dir"/*.dts; do
        build_one "$(basename "$f" .dts)"
    done
else
    [ $# -ge 1 ] || { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
    for n in "$@"; do build_one "$n"; done
fi
