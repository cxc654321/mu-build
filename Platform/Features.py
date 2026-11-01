# @file
# What a firmware build contains, as a set of features you can toggle.
#
# This replaces thirty named profiles. Those were every combination anyone had
# ever wanted, frozen: "ans-gpu-wireless-no-xhc2", "internal-storage-dcpirq",
# "gpu-noacpi-no-wireless". Adding one variable meant adding a name, and the
# names had to be kept identical in the build script, the sealing tool and
# auroradbg. There is no such thing as a profile now -- a build is a device and
# the features that are on.
#
#     Tools/mu-build j414s
#     Tools/mu-build j414s --without wireless
#     Tools/mu-build j414s --with media --without gpu-acpi
#
# The defaults are the configuration that boots. A flag moves one variable.
#
# Copyright (c) Aurora Silicon.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

from __future__ import annotations

#
# Every feature, the -D it becomes, and how its value is spelled. "bool" is
# TRUE/FALSE, "flag" is 1/0 -- the two spellings the .dsc files already use.
#
FEATURES = {
    "ans":                ("BLD_*_NTASI_ENABLE_ANS", "bool"),
    "ans-acpi":           ("BLD_*_NTASI_ANS_PUBLISH_ACPI", "bool"),
    "ans-dxe":            ("BLD_*_NTASI_ANS_DXE_BRINGUP", "bool"),
    "ans-block-io":       ("BLD_*_NTASI_ANS_PUBLISH_BLOCK_IO", "bool"),
    "ans-preserve":       ("BLD_*_NTASI_ANS_PRESERVE_FOR_OS", "bool"),
    "gpu":                ("BLD_*_NTASI_GPU_RESOURCE_PROFILE", "flag"),
    "gpu-acpi":           ("BLD_*_NTASI_ENABLE_GPU_ACPI_PUBLICATION", "flag"),
    "display-interrupts": ("BLD_*_NTASI_ENABLE_DISPLAY_INTERRUPTS", "flag"),
    "wireless":           ("BLD_*_NTASI_ENABLE_WIRELESS_DART_HANDOFF", "flag"),
    "xhc2":               ("BLD_*_NTASI_ENABLE_XHC2", "flag"),
    "mca":                ("BLD_*_NTASI_ENABLE_MCA_PUBLICATION", "flag"),
    "aop":                ("BLD_*_NTASI_ENABLE_AOP_PUBLICATION", "flag"),
    "isp":                ("BLD_*_NTASI_ENABLE_ISP_PUBLICATION", "flag"),
    "battery":            ("BLD_*_NTASI_ENABLE_BATTERY_PUBLICATION", "flag"),
}

# Shorthand that expands to several features. "media" was the only one the
# profiles had, and splitting it apart was deliberate -- the three devices are
# independent -- so it stays a shorthand rather than a define.
GROUPS = {
    "media": ("mca", "aop", "isp"),
    "storage": ("ans", "ans-acpi", "ans-dxe", "ans-block-io", "ans-preserve"),
}

#
# Values that are not on/off. Each is (define, how-to-compute).
#
def _usb3_pipe_mask(on):
    # The left port is the boot port. With XHC2 published, the proven mask is
    # the right port; with it hidden, phase 1 isolates the left.
    return "0x4" if "xhc2" in on else "0x2"


DERIVED = {
    "BLD_*_NTASI_GPU_ACPI_HID": lambda on: "24" if "gpu-acpi" in on else "23",
    "BLD_*_NTASI_USB3_PIPE_SWITCH_PORT_MASK": _usb3_pipe_mask,
    # A routed mux switch is only ever correct on a port m1n1 has brought a
    # tunnel up on, so it is off unless something asks. 0x0 makes the Mu driver
    # a strict no-op.
    "BLD_*_NTASI_USB4_ROUTED_PIPE_SWITCH_PORT_MASK": lambda on: "0x0",
}

#
# One entry per machine. `on` is what a plain `mu-build <device>` produces: the
# configuration known to boot. Everything else is off.
#
DEVICES = {
    "j414s": {
        "platform": "MacBookProEarly2023",
        "description": "MacBook Pro (14-inch, M2 Pro, 2023)",
        "fd": "MACBOOKPROEARLY2023_EFI.fd",
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2", "battery"),
    },
    "j416s": {
        "platform": "MacBookPro16Early2023",
        "description": "MacBook Pro (16-inch, M2 Pro, 2023)",
        "fd": "MACBOOKPRO16EARLY2023_EFI.fd",
        # Same T6020 as j414s, same devices. Asahi's t6020-j414s.dts and
        # t6020-j416s.dts differ in seven lines: the model strings, the Wi-Fi
        # board type, the panel size, the chassis name, the audio model, and
        # the MTP firmware blob. None of those changes which features exist.
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2", "battery"),
    },
    # M1-era machines. Packages inherited from upstream NT-for-ASi; none of the
    # Windows feature work applies to them yet, so everything is off. They build,
    # which is the floor, not the goal.
    "j313": {
        "platform": "MacBookAirMid2020",
        "description": "MacBook Air (M1, 2020)",
        "fd": "J313MacBookAirMid2020_EFI.fd",
        "on": (),
    },
    "j274": {
        "platform": "MacMini2020",
        "description": "Mac mini (M1, 2020)",
        "fd": "J274MacMini2020_EFI.fd",
        "on": (),
    },
    "j375c": {
        "platform": "MacStudio2022",
        "description": "Mac Studio (M1 Max, 2022)",
        "fd": "J375MacStudio2022_EFI.fd",
        "on": (),
    },
    "j293": {
        "platform": "MacBookProLate2020",
        "description": "MacBook Pro (13-inch, M1, 2020)",
        "fd": "J293MACBOOKPROLATE2020_EFI.fd",
        "on": (),
    },
    "j414c": {
        "platform": "MacBookPro14Max2023",
        "description": "MacBook Pro (14-inch, M2 Max, 2023)",
        "fd": "MACBOOKPRO14MAX2023_EFI.fd",
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2", "battery"),
    },
    "j416c": {
        "platform": "MacBookPro16Max2023",
        "description": "MacBook Pro (16-inch, M2 Max, 2023)",
        "fd": "MACBOOKPRO16MAX2023_EFI.fd",
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2", "battery"),
    },
    "j474s": {
        "platform": "MacMini2023",
        "description": "Mac mini (M2 Pro, 2023)",
        "fd": "MACMINI2023_EFI.fd",
        # No battery: this machine runs on mains.
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2"),
    },
    "j475c": {
        "platform": "MacStudioMax2023",
        "description": "Mac Studio (M2 Max, 2023)",
        "fd": "MACSTUDIOMAX2023_EFI.fd",
        # No battery: this machine runs on mains.
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2"),
    },
    "j475d": {
        "platform": "MacStudioUltra2023",
        "description": "Mac Studio (M2 Ultra, 2023)",
        "fd": "MACSTUDIOULTRA2023_EFI.fd",
        # No battery: this machine runs on mains.
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2"),
    },
    "j180d": {
        "platform": "MacPro2023",
        "description": "Mac Pro (M2 Ultra, 2023)",
        "fd": "MACPRO2023_EFI.fd",
        # No battery: this machine runs on mains.
        "on": ("storage", "gpu", "gpu-acpi", "wireless", "xhc2"),
    },
    "j413": {
        "platform": "MacBookAir13M2",
        "description": "MacBook Air (13-inch, M2, 2022)",
        "fd": "J413MACBOOKAIR13M2_EFI.fd",
        "on": (),
    },
    #
    # MacBook Neo, the one machine here that is not a Mac Asahi supports. Its
    # silicon facts come from aurora-silicon/neo-bringup, which brought it up on
    # Linux against a live unit; see DeviceTree/t8140-j700.dts for the
    # provenance of each one.
    #
    "j700": {
        "platform": "MacBookNeo",
        "description": "MacBook Neo (J700)",
        "fd": "MACBOOKNEO_EFI.fd",
        "on": (),
    },
    "j813": {
        "platform": "MacBookAir2026",
        "description": "MacBook Air (M5, 2026)",
        "fd": "J813MACBOOKAIR2026_EFI.fd",
        # J813 builds no CSRT and never loads the AIC HAL extension, so the
        # wireless, GPU and USB-host work has no counterpart here yet.
        "on": ("storage", "battery"),
    },
    #
    # Machines generated by Tools/add-machine.py from Asahi's device trees, on
    # SoC family packages generated by Tools/add-soc.py from the same source.
    # None has been booted. Every feature is off: the toggles below turn on
    # driver work that was done against J414s or J813 and verified there, and
    # claiming it here would be claiming a machine we have never seen.
    #
    "j456": {
        "platform": "iMac24Late2021",
        "description": "iMac (24-inch, 4x USB-C, M1, 2021)",
        "fd": "IMAC24LATE2021_EFI.fd",
        "on": (),
    },
    "j457": {
        "platform": "iMac24Late2021TwoPort",
        "description": "iMac (24-inch, 2x USB-C, M1, 2021)",
        "fd": "IMAC24LATE2021TWOPORT_EFI.fd",
        "on": (),
    },
    "j314s": {
        "platform": "MacBookPro14Late2021",
        "description": "MacBook Pro (14-inch, M1 Pro, 2021)",
        "fd": "MACBOOKPRO14LATE2021_EFI.fd",
        "on": (),
    },
    "j316s": {
        "platform": "MacBookPro16Late2021",
        "description": "MacBook Pro (16-inch, M1 Pro, 2021)",
        "fd": "MACBOOKPRO16LATE2021_EFI.fd",
        "on": (),
    },
    "j314c": {
        "platform": "MacBookPro14MaxLate2021",
        "description": "MacBook Pro (14-inch, M1 Max, 2021)",
        "fd": "MACBOOKPRO14MAXLATE2021_EFI.fd",
        "on": (),
    },
    "j316c": {
        "platform": "MacBookPro16MaxLate2021",
        "description": "MacBook Pro (16-inch, M1 Max, 2021)",
        "fd": "MACBOOKPRO16MAXLATE2021_EFI.fd",
        "on": (),
    },
    "j375d": {
        "platform": "MacStudioUltra2022",
        "description": "Mac Studio (M1 Ultra, 2022)",
        "fd": "MACSTUDIOULTRA2022_EFI.fd",
        "on": (),
    },
    "j415": {
        "platform": "MacBookAir15M2",
        "description": "MacBook Air (15-inch, M2, 2023)",
        "fd": "MACBOOKAIR15M2_EFI.fd",
        "on": (),
    },
    "j473": {
        "platform": "MacMini2023M2",
        "description": "Mac mini (M2, 2023)",
        "fd": "MACMINI2023M2_EFI.fd",
        "on": (),
    },
    "j493": {
        "platform": "MacBookPro13M2",
        "description": "MacBook Pro (13-inch, M2, 2022)",
        "fd": "MACBOOKPRO13M2_EFI.fd",
        "on": (),
    },
    "j433": {
        "platform": "iMac24M3TwoPort",
        "description": "iMac (24-inch, 2x USB-C, M3, 2023)",
        "fd": "IMAC24M3TWOPORT_EFI.fd",
        "on": (),
    },
    "j434": {
        "platform": "iMac24M3",
        "description": "iMac (24-inch, 4x USB-C, M3, 2023)",
        "fd": "IMAC24M3_EFI.fd",
        "on": (),
    },
    "j504": {
        "platform": "MacBookPro14M3",
        "description": "MacBook Pro (14-inch, M3, 2023)",
        "fd": "MACBOOKPRO14M3_EFI.fd",
        "on": (),
    },
    "j613": {
        "platform": "MacBookAir13M3",
        "description": "MacBook Air (13-inch, M3, 2024)",
        "fd": "MACBOOKAIR13M3_EFI.fd",
        "on": (),
    },
    "j615": {
        "platform": "MacBookAir15M3",
        "description": "MacBook Air (15-inch, M3, 2024)",
        "fd": "MACBOOKAIR15M3_EFI.fd",
        "on": (),
    },
    "j604": {
        "platform": "MacBookPro14M4",
        "description": "MacBook Pro (14-inch, M4, 2024)",
        "fd": "MACBOOKPRO14M4_EFI.fd",
        "on": (),
    },
    "j623": {
        "platform": "iMac24M4TwoPort",
        "description": "iMac (24-inch, 2x USB-C, M4, 2024)",
        "fd": "IMAC24M4TWOPORT_EFI.fd",
        "on": (),
    },
    "j624": {
        "platform": "iMac24M4",
        "description": "iMac (24-inch, 4x USB-C, M4, 2024)",
        "fd": "IMAC24M4_EFI.fd",
        "on": (),
    },
    "j713": {
        "platform": "MacBookAir13M4",
        "description": "MacBook Air (13-inch, M4, 2024)",
        "fd": "MACBOOKAIR13M4_EFI.fd",
        "on": (),
    },
    "j715": {
        "platform": "MacBookAir15M4",
        "description": "MacBook Air (15-inch, M4, 2025)",
        "fd": "MACBOOKAIR15M4_EFI.fd",
        "on": (),
    },
    "j773g": {
        "platform": "MacMini2024",
        "description": "Mac mini (M4, 2024)",
        "fd": "MACMINI2024_EFI.fd",
        "on": (),
    },
    "j514s": {
        "platform": "MacBookPro14M3Pro",
        "description": "MacBook Pro (14-inch, M3 Pro, Nov 2023)",
        "fd": "MACBOOKPRO14M3PRO_EFI.fd",
        "on": (),
    },
    "j516s": {
        "platform": "MacBookPro16M3Pro",
        "description": "MacBook Pro (16-inch, M3 Pro, Nov 2023)",
        "fd": "MACBOOKPRO16M3PRO_EFI.fd",
        "on": (),
    },
    "j514c": {
        "platform": "MacBookPro14M3Max",
        "description": "MacBook Pro (14-inch, M3 Max, 16 CPU cores, Nov 2023)",
        "fd": "MACBOOKPRO14M3MAX_EFI.fd",
        "on": (),
    },
    "j516c": {
        "platform": "MacBookPro16M3Max",
        "description": "MacBook Pro (16-inch, M3 Max, 16 CPU cores, Nov 2023)",
        "fd": "MACBOOKPRO16M3MAX_EFI.fd",
        "on": (),
    },
    "j514m": {
        "platform": "MacBookPro14M3Max14Core",
        "description": "MacBook Pro (14-inch, M3 Max, 14 CPU cores, Nov 2023)",
        "fd": "MACBOOKPRO14M3MAX14CORE_EFI.fd",
        "on": (),
    },
    "j516m": {
        "platform": "MacBookPro16M3Max14Core",
        "description": "MacBook Pro (16-inch, M3 Max, 14 CPU cores, Nov 2023)",
        "fd": "MACBOOKPRO16M3MAX14CORE_EFI.fd",
        "on": (),
    },
    "j575d": {
        "platform": "MacStudioUltra2025",
        "description": "Mac Studio (M3 Ultra, 2025)",
        "fd": "MACSTUDIOULTRA2025_EFI.fd",
        "on": (),
    },
}


class UnknownFeature(ValueError):
    pass


def expand(names) -> set[str]:
    out = set()
    for name in names:
        if name in GROUPS:
            out.update(GROUPS[name])
        elif name in FEATURES:
            out.add(name)
        else:
            known = ", ".join(sorted(set(FEATURES) | set(GROUPS)))
            raise UnknownFeature(f"unknown feature {name!r}; known: {known}")
    return out


def resolve(device: str, with_=(), without=()) -> set[str]:
    """Which features are on for this build."""
    try:
        spec = DEVICES[device]
    except KeyError:
        raise ValueError(
            f"unknown device {device!r}; known: {', '.join(sorted(DEVICES))}") from None
    return (expand(spec["on"]) | expand(with_)) - expand(without)


def defines(device: str, with_=(), without=()) -> list[tuple[str, str]]:
    """(define, value) for every feature, on or off, plus the derived ones."""
    on = resolve(device, with_, without)
    out = []
    for name, (define, kind) in FEATURES.items():
        enabled = name in on
        out.append((define, ("TRUE" if enabled else "FALSE") if kind == "bool"
                    else ("1" if enabled else "0")))
    for define, compute in DERIVED.items():
        out.append((define, compute(on)))
    return sorted(out)


def summary(device: str, with_=(), without=()) -> str:
    on = resolve(device, with_, without)
    off = set(FEATURES) - on
    return (f"{device} ({DEVICES[device]['description']})\n"
            f"  on:  {' '.join(sorted(on)) or '(nothing)'}\n"
            f"  off: {' '.join(sorted(off)) or '(nothing)'}")


def platform(device: str) -> dict:
    """Where this device's package lives and what its firmware is called."""
    try:
        spec = DEVICES[device]
    except KeyError:
        raise ValueError(
            f"unknown device {device!r}; known: {', '.join(sorted(DEVICES))}") from None
    return {
        "pkg": f"{spec['platform']}Pkg",
        "platform": spec["platform"],
        "fd": spec["fd"],
        "description": spec["description"],
    }


def slug(device: str, with_=(), without=()) -> str:
    """A short, stable directory name for one configuration of one device.

    The device alone when nothing is overridden, so the common case reads as
    `build/j414s/<commit>/`. Overrides append in sorted order, so the same
    request always lands in the same place.
    """
    parts = [device]
    parts += [f"+{name}" for name in sorted(expand(with_))]
    parts += [f"-{name}" for name in sorted(expand(without))]
    return "".join(parts)
