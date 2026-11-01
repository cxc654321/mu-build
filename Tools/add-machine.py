#!/usr/bin/env python3
# Copyright (c) 2026 Aurora Silicon

"""Create a Mu platform package for a Mac, from its Asahi device tree.

WHAT A MACHINE PACKAGE IS, AFTER THE TIERS

Firmware is composed from four tiers -- silicon, chassis, board, machine -- and
only the last is per-Mac. See Docs/PLATFORMS.md. A machine package therefore
carries identity and bindings, and no hardware description:

    * its name, GUID, output directory and flash definition
    * which SoC family package, chassis family package and board package it uses
    * its SMBIOS model, model number and SKU, from the device tree
    * its SoC identifier, and whether that SoC has a PCIe root complex

Everything else comes from a tier it points at.

WHAT IT MUST NOT DO

Earlier versions built a machine by copying a proven machine's package whole and
rewriting the machine name through every file. The ACPI that produced was
right -- the addresses are properties of the SoC -- but the provenance was not:
comments saying a value had been measured on J414s and pinned by a named test
became claims about J416s, J474s or J514s, citing scripts, device trees and ADT
captures that do not exist.

So this does not copy ACPI. A machine either shares a board package with the
machine its board was measured on, or it uses GenericBoardPkg, which declares
only what the SoC family package's PCDs already state.

    Tools/add-machine.py --list-templates
    Tools/add-machine.py j416c --dts t6021-j416c.dts --template j414s \
        --platform MacBookPro16Max2023 --soc-pkg T602XFamilyPkg \
        --family-pkg MacBookProFamilyPkg --board-pkg J414sBoardPkg

WHAT IT DOES NOT CLAIM

A package that builds is not a machine that boots. This gets a Mac to the point
where its firmware compiles against a SoC family package derived from its own
silicon, with its own identity; it says nothing about whether its ANS, display
or input come up. Every generated package is marked accordingly.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "Platform"))

# Tables that only make sense on a machine with a lid, an internal keyboard and
# a keyboard backlight. A desktop gets the package without them.
LAPTOP_ONLY = ("KBL.asl", "LIDA.asl", "PBTN.asl", "MTP.asl")


def dt_facts(dts: Path) -> dict:
    """The per-machine facts Asahi's device tree states outright."""
    text = dts.read_text(encoding="utf-8", errors="replace")

    def one(pattern, default=None):
        m = re.search(pattern, text)
        return m.group(1) if m else default

    return {
        "model": one(r'model\s*=\s*"([^"]+)"'),
        "target": one(r'target-type:\s*(\S+)'),
        # The machine, then the SoC: compatible lists both, machine first.
        "compatible": one(r'compatible\s*=\s*"apple,(j[a-z0-9]+)"'),
        "soc": one(r'"apple,(t[0-9a-f]{4})"'),
        "chassis": one(r'apple,chassis-name\s*=\s*"([^"]+)"'),
        "mtp_firmware": one(r'firmware-name\s*=\s*"(apple/tpmtfw-[^"]+)"'),
        "panel_width_mm": one(r'width-mm\s*=\s*<(\d+)>'),
        "panel_height_mm": one(r'height-mm\s*=\s*<(\d+)>'),
        "board_type": one(r'brcm,board-type\s*=\s*"apple,([a-z0-9]+)"'),
    }


def template_soc_pkg(dsc: Path) -> str | None:
    """Which SoC family package a platform .dsc includes.

    A .dsc includes two family packages, the chassis one and the SoC one, and
    which comes first varies. SoC packages are the ones named for the silicon --
    T8103, T602X, T6031 -- so match that rather than position. Matching "the
    first <Pkg>/<Pkg>.dsc.inc" picked MacBookProFamilyPkg out of the j414s
    template and retargeted the chassis instead of the silicon.
    """
    m = re.search(r"^!include\s+(T[0-9A-Fa-fX]{4}FamilyPkg)/\S+\.dsc\.inc",
                  dsc.read_text(encoding="utf-8"), re.M)
    return m.group(1) if m else None


def retarget_soc(dst: Path, platform: str, old_pkg: str, new_pkg: str,
                 memory_size: str) -> None:
    """Point a copied platform package at a different SoC family.

    Three places name the family: the .dsc includes its .dsc.inc, the .fdf
    names it as PLATFORM_SOC_PKG, and the generated families take their memory
    size through a per-platform DEFINE. Miss any one and the build either fails
    to resolve the package or silently keeps the template's silicon.
    """
    old_name = old_pkg[:-len("FamilyPkg")]
    new_name = new_pkg[:-len("FamilyPkg")]

    dsc = dst / f"{platform}.dsc"
    text = dsc.read_text(encoding="utf-8")
    text = text.replace(old_pkg, new_pkg)
    text = text.replace(f"{old_name}_SYSTEM_MEMORY_SIZE", f"{new_name}_SYSTEM_MEMORY_SIZE")
    define = f"  DEFINE {new_name}_SYSTEM_MEMORY_SIZE = {memory_size}\n"
    if f"{new_name}_SYSTEM_MEMORY_SIZE =" not in text:
        # Every generated family takes its size this way; a template that
        # hardcoded one leaves the DEFINE absent and the build unresolved.
        text = re.sub(r"(\[Defines\]\n)", r"\1" + define.replace("\\", "\\\\"),
                      text, count=1)
    dsc.write_text(text, encoding="utf-8")

    fdf = dst / f"{platform}.fdf"
    fdf.write_text(fdf.read_text(encoding="utf-8").replace(old_pkg, new_pkg),
                   encoding="utf-8")


def retarget_family(dst: Path, platform: str, new_pkg: str) -> None:
    """Point a copied platform package at a different chassis family.

    The chassis family package decides the SMBIOS system family string and the
    FADT's preferred power management profile -- desktop, mobile or workstation.
    Inheriting the template's is how four desktops ended up reporting "MacBook
    Pro" and asking the OS for mobile power policy.
    """
    fdf = dst / f"{platform}.fdf"
    text = re.sub(r"(DEFINE PLATFORM_FAMILY_PKG\s*=\s*)\S+",
                  lambda m: m.group(1) + new_pkg, fdf.read_text(encoding="utf-8"))
    fdf.write_text(text, encoding="utf-8")

    # The include's basename is not derivable from the package name:
    # MacBookAirFamilyPkg spells its file MacBookAirFamily.dsc.inc while every
    # other family repeats the full package name. Read what is actually there.
    incs = sorted((REPO / "Platform" / new_pkg).glob("*.dsc.inc"))
    if len(incs) != 1:
        raise SystemExit(
            f"{new_pkg} has {len(incs)} .dsc.inc files; expected exactly one")

    dsc = dst / f"{platform}.dsc"
    text = re.sub(r"^!include\s+\w+FamilyPkg/\S+\.dsc\.inc",
                  f"!include {new_pkg}/{incs[0].name}",
                  dsc.read_text(encoding="utf-8"), count=1, flags=re.M)
    dsc.write_text(text, encoding="utf-8")


def retarget_board(dst: Path, platform: str, new_pkg: str) -> None:
    """Point a copied platform package at a board package.

    The board tier owns two things: the ACPI that names soldered devices, and
    the PCDs whose values were read off a live machine -- the ANS interrupt
    pair, the ANS power-domain addresses, the DWC3 counts. Both move together,
    because a machine that takes one and not the other is describing two
    different boards.
    """
    fdf = dst / f"{platform}.fdf"
    fdf.write_text(re.sub(r"(DEFINE PLATFORM_BOARD_PKG = )\S+",
                          lambda m: m.group(1) + new_pkg,
                          fdf.read_text(encoding="utf-8")),
                   encoding="utf-8")

    dsc = dst / f"{platform}.dsc"
    text = re.sub(r"^(\s*)\S+Pkg/AcpiTables/DeviceAcpiTables\.inf",
                  lambda m: m.group(1) + f"{new_pkg}/AcpiTables/DeviceAcpiTables.inf",
                  dsc.read_text(encoding="utf-8"), count=1, flags=re.M)
    text = re.sub(r"^!include \S+Pkg/\S+BoardPkg\.dsc\.inc\n", "", text, flags=re.M)
    if (REPO / "Platform" / new_pkg / f"{new_pkg}.dsc.inc").is_file():
        text = re.sub(r"^(!include \w+FamilyPkg/)",
                      f"!include {new_pkg}/{new_pkg}.dsc.inc\n" + r"\1",
                      text, count=1, flags=re.M)
    dsc.write_text(text, encoding="utf-8")


def soc_has_pcie(soc_pkg: str) -> bool:
    """Whether this SoC family package found a PCIe root complex.

    Asahi's tree describes none for T8132 (M4), so its window PCDs stay at
    zero. GenericBoardPkg's DSDT declares a root bridge only when there is one,
    because a bridge whose window is zero-length is both untrue and an iasl
    error. The DSDT learns this through ASLPP_FLAGS rather than a #if on the
    PCD, because the ASL toolchain substitutes PCDs after the preprocessor has
    already run.
    """
    inc = REPO / "Silicon/Apple" / soc_pkg / f"{soc_pkg}.dsc.inc"
    return "PcdPciExpressBaseAddress|0x" in inc.read_text(encoding="utf-8")


def generate(device: str, dts: Path, template: str, platform: str,
             laptop: bool, soc_pkg: str | None = None,
             memory_size: str = "0x200000000",
             family_pkg: str | None = None,
             board_pkg: str | None = None) -> Path:
    import Features
    tspec = Features.DEVICES[template]
    src = REPO / "Platform" / f"{tspec['platform']}Pkg"
    dst = REPO / "Platform" / f"{platform}Pkg"
    facts = dt_facts(dts)
    if not facts["model"]:
        raise SystemExit(f"{dts} has no model string")

    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__"))

    tfacts = dt_facts_for_template(template)
    subs = [
        (tspec["platform"], platform),
        (tspec["platform"].upper(), platform.upper()),
        (tspec["fd"].replace("_EFI.fd", ""), f"{platform.upper()}"),
    ]
    # Older packages prefix the FD name with the machine id ("J813MacBookAir2026").
    # Substituting the platform name alone leaves the template's machine on the
    # front of this machine's firmware image, so replace the id too.
    subs.append((template.upper(), device.upper()))
    subs.append((template, device))
    for key in ("mtp_firmware", "chassis", "board_type"):
        if tfacts.get(key) and facts.get(key):
            subs.append((tfacts[key], facts[key]))
    for key, upper in (("target", True), ("compatible", False)):
        old, new = tfacts.get(key), facts.get(key)
        if old and new:
            subs.append((old, new))
            if upper:
                subs.append((old.upper(), new.upper()))
                subs.append((old.lower(), new.lower()))
    if tfacts.get("model") and facts.get("model"):
        subs.append((tfacts["model"].replace("Apple ", ""),
                     facts["model"].replace("Apple ", "")))

    for path in sorted(dst.rglob("*")):
        if path.is_dir() or path.suffix in (".png", ".pyc"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for old, new in subs:
            if old and new:
                text = text.replace(old, new)
        path.write_text(text, encoding="utf-8")

    for path in list(dst.rglob(f"*{tspec['platform']}*")):
        path.rename(path.with_name(path.name.replace(tspec["platform"], platform)))

    # Identity the .dsc states outright, rather than inheriting the template's.
    dsc = dst / f"{platform}.dsc"
    text = dsc.read_text(encoding="utf-8")
    soc = facts["soc"]  # e.g. "t6021"
    if soc and re.fullmatch(r"t[0-9a-f]{4}", soc):
        text = re.sub(
            r"(\[PcdsFixedAtBuild[^\]]*\]\n)",
            r"\1  # This machine's SoC, from its device tree compatible. The family\n"
            r"  # package pins the base part of the family; a Max or Ultra is not it.\n"
            f"  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x{soc[1:]}\n",
            text, count=1)
    # Build-time silicon identity. AppleNANDStorageDxe keys its register map off
    # SILICON_PLATFORM, so inheriting the template's would give an M2 machine the
    # M5 NVMe layout.
    if soc and re.fullmatch(r"t[0-9a-f]{4}", soc):
        text = re.sub(r"(-DSILICON_PLATFORM=)[0-9a-fA-F]+",
                      lambda m: m.group(1) + soc[1:], text)
    # Defines named after a specific machine describe that machine, not this
    # one. NTASI_J813_EL2_SYSREG_ASSIST is set on J813 and on no other T8142
    # platform, which is what makes it recognisable as machine-scoped. The
    # substitutions above have already renamed it to this machine, so match the
    # renamed form -- carrying it over would claim an assist that only exists
    # for the template.
    text = re.sub(rf"\s*-D\s*NTASI_{device.upper()}_\w+(=\S+)?", "", text)

    model = facts["model"].replace("Apple ", "")
    text = re.sub(r'(PcdSmbiosSystemModel\|)"[^"]*"', lambda m: f'{m.group(1)}"{model}"', text)
    text = re.sub(r'(PcdSmbiosSystemModelNumber\|)"[^"]*"',
                  lambda m: f'{m.group(1)}"{facts["target"] or device}"', text)
    text = re.sub(r'(PcdSmbiosSystemSku\|)"[^"]*"',
                  lambda m: f'{m.group(1)}"{model} ({facts["target"] or device})"', text)
    dsc.write_text(text, encoding="utf-8")

    if not laptop:
        inf = dst / "AcpiTables" / "DeviceAcpiTables.inf"
        text = inf.read_text(encoding="utf-8")
        for table in LAPTOP_ONLY:
            text = text.replace(f"  {table}\n", "")
            (dst / "AcpiTables" / table).unlink(missing_ok=True)
        inf.write_text(text)

    if soc_pkg:
        old = template_soc_pkg(REPO / "Platform" / f"{tspec['platform']}Pkg"
                               / f"{tspec['platform']}.dsc")
        if old is None:
            raise SystemExit(f"cannot tell which SoC package {template} uses")
        if old != soc_pkg:
            retarget_soc(dst, platform, old, soc_pkg, memory_size)

    if family_pkg:
        retarget_family(dst, platform, family_pkg)

    if soc_pkg:
        dsc = dst / f"{platform}.dsc"
        dsc.write_text(
            re.sub(r"(DEFINE NTASI_SOC_HAS_PCIE = )\S+",
                   lambda m: m.group(1) + ("1" if soc_has_pcie(soc_pkg) else "0"),
                   dsc.read_text(encoding="utf-8")),
            encoding="utf-8")

    if board_pkg:
        retarget_board(dst, platform, board_pkg)

    # A machine package carries no ACPI of its own; its board package does.
    shutil.rmtree(dst / "AcpiTables", ignore_errors=True)

    banner = (f"# Generated by Tools/add-machine.py from Asahi's {dts.name}.\n"
              f"# {facts['model']}\n"
              "#\n"
              "# UNVERIFIED ON HARDWARE. This compiles against a proven SoC family\n"
              "# package with this machine's own identity; nothing here has been\n"
              "# booted. Its ANS, display and input paths are inherited from the\n"
              f"# template ({template}) and have not been checked on this machine.\n#\n")
    build = dst / "PlatformBuild.py"
    build.write_text(banner + build.read_text(encoding="utf-8"))
    return dst


def dt_facts_for_template(template: str) -> dict:
    cached = REPO / "Silicon/Apple/AppleSiliconPkg/DeviceTree"
    for name in (f"t6020-{template}.dts", f"{template}.dts"):
        path = cached / name
        if path.is_file():
            return dt_facts(path)
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("device")
    parser.add_argument("--dts", type=Path, required=True)
    parser.add_argument("--template", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--desktop", action="store_true")
    parser.add_argument("--soc-pkg",
                        help="SoC family package to bind to, e.g. T811XFamilyPkg. "
                             "Defaults to whatever the template uses.")
    parser.add_argument("--board-pkg", default="GenericBoardPkg",
                        help="board package supplying this machine's ACPI and "
                             "measured PCDs. Defaults to GenericBoardPkg, which "
                             "is correct until someone measures the board.")
    parser.add_argument("--family-pkg",
                        help="chassis family package, e.g. MacMiniFamilyPkg. "
                             "Sets the SMBIOS system family and the FADT power "
                             "profile. Defaults to the template's.")
    parser.add_argument("--memory-size", default="0x200000000",
                        help="build-time default for the family's "
                             "*_SYSTEM_MEMORY_SIZE define; patched from the FDT "
                             "at runtime, so this only has to be plausible")
    args = parser.parse_args()
    dst = generate(args.device, args.dts, args.template, args.platform,
                   laptop=not args.desktop, soc_pkg=args.soc_pkg,
                   memory_size=args.memory_size, family_pkg=args.family_pkg,
                   board_pkg=args.board_pkg)
    print(f"{args.device}: {dst.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
