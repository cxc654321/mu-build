# GenericBoardPkg

The board tier for machines whose logic board has not been measured.

## What a board package is

Firmware for a Mac is composed from four tiers, and a tier is included only
when its contents are true of the machine:

| tier | package | scope |
|---|---|---|
| silicon | `Silicon/Apple/<SoC>FamilyPkg` | the die — memory map, MADT/GTDT/PPTT, PCIe, UART, processor tree |
| chassis | `Platform/<Chassis>FamilyPkg` | the enclosure — FADT power profile, SMBIOS family |
| board | `Platform/<Board>BoardPkg` | the logic board — the ACPI that names soldered devices |
| machine | `Platform/<Machine>Pkg` | one Mac — identity only, as build defines |

The silicon tier is derived from Asahi's device tree, so it is available for
every Mac. The board tier is not derivable from anything: a keyboard's GPIO
number, an audio complex's interrupt line and a display's published GSIV come
from reading a live machine's ADT and correlating it with the alias table the
AIC HAL extension uses.

## Why this package exists

Before it, a new machine was created by copying a measured machine's board
package and rewriting the machine name through every file. That produced ACPI
whose comments said the addresses had been measured on a machine nobody owned,
citing tests and captures that did not exist — `tools/verify-j416s-av-adt.py`,
`j474s-adt.bin`, `docs/j514s-display-driver.md`. The addresses themselves were
right, because they are properties of the SoC; the claim that they had been
checked on that machine was not.

This package is what an unmeasured machine gets instead. It declares the PCIe
root bridge and the processor tree, both entirely from PCDs and generated
sources, and nothing else. A machine using it has no keyboard, trackpad, audio,
camera, display or SMC device in its DSDT — which is the truth, and is
recoverable by measuring the board and giving it a package of its own.

## What still works

Boot, PCIe enumeration, and internal NVMe: `AppleNANDStorageDxe` takes every
address from the live ADT and has two register-map variants selected from it,
so storage needs nothing from this tier. Serial arrives through DBG2 and the
framebuffer is inherited from the bootloader.
