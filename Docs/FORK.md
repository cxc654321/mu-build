# What this fork is, and what it adds

Base: `bfe3e9d2` (NT-for-ASi/apple_silicon_platforms_mu, 2026-07-08).
Divergence: 137 ours, 0 theirs. The base is current; there is no upstream debt
here, which is the opposite of the m1n1 situation.

## Purpose

Mu turns a Mac into a machine Windows recognises. Everything it does falls into
three jobs.

**1. Describe the machine in ACPI.** Windows learns hardware from ACPI, and a
Mac publishes none. Mu produces the MADT, GTDT, PPTT, CSRT, FADT, MCFG, DSDT and
the SSDT-shaped device blocks. This is the job.

**2. Provide the boot-time drivers UEFI needs and nothing more.** Enough storage
to read the Windows installer and the ESP. Enough display to draw. Enough input
to press a key. Interrupts, DART, PCIe. Each of these has a Windows driver that
takes over later; Mu's copy exists only for the pre-OS window.

**3. Hand off state Windows cannot reconstruct.** The wireless DART reservation,
the GPU initdata region, ANS ownership. Mu is the second half of contracts m1n1
starts.

### What does not belong

- Reimplementing a device Windows already drives, past what UEFI needs to boot.
- Machine constants that iBoot's Apple Device Tree already carries.
- Whole-product build orchestration and artifact sealing.
- Tests that assert on the text of source files.

## Answering the question that prompted this

> I believe mainly what we should be doing is defining platform
> packages/families, but outside of that, are we doing much more?

Much more. Of 26,419 added lines, platform and family definition is 4,163, about
16%.

| Area | Lines | What it is | Verdict |
| --- | --- | --- | --- |
| `AppleNANDStorageDxe` | 6744 | NVMe over Apple ANS/RTKit, plus a `Shared/` core split so it can be host-tested | **Keep.** The largest thing here and the most clearly necessary: no other UEFI can read the internal SSD, so without it there is no installer and no ESP. |
| `AcpiPlatformDxe` | 4351 | Runtime ACPI generation via AmlLib | **Keep, refactor.** Job 1 exactly. But see below: it is 4838 lines in one file with 82 hardcoded MMIO literals, inside the SoC-agnostic package. |
| `Platform/MacBookProEarly2023Pkg` | 2917 | J414s tables and build | **Keep.** |
| `Tests` | 2874 | 16 tests, of which 10 asserted on source text | **Cut, done.** 3141 lines removed; the five `.c` tests and their harnesses stay. |
| `Tools` | 2041 | Profile manifest sealing plus four shim wrappers | **Move.** Sealing is auroradbg's job. Cross-repo; see below. |
| `AppleSiliconPkg/Include` | 1762 | ABI headers shared with m1n1 and the drivers | **Keep.** |
| `AppleUsb4BringupDxe` | 1389 | USB4 router/tunnel bring-up | **Quarantine.** Nothing on the boot path needs USB4. Matches `acio*` in m1n1: two large speculative subsystems for the same feature. |
| `T602XFamilyPkg` | 1246 | M2 Pro/Max family | **Keep.** |
| `AppleUsbTypeCBringupDxe` | 1166 | Type-C bring-up | **Keep.** |
| `BootRamdiskHelperDxe` | 703 | Appended ramdisk | **Keep for now.** The installer should make this unnecessary. |
| `AppleAicDxe` | 277 | AIC v1/v2 | **Keep.** |
| Others | 462 | SimpleFb, Smbios, Dart | **Keep.** |

## The layering problem

`AcpiPlatform.c` lives in `Silicon/Apple/AppleSiliconPkg`, the package that is
meant to hold everything common to all Apple SoCs. It is 4838 lines and makes
11 Apple Device Tree lookups against 82 hardcoded MMIO literals of 32 bits or
more, in tables like:

```c
STATIC CONST NTASI_MEDIA_WINDOW  mNtasiMcaWindows[] = {
  { 0x39B600000ULL, 0x10000ULL },  // MCA cluster registers
  { 0x39B500000ULL, 0x20000ULL },  // MCA switch / DMA glue
  ...
```

Those are J414s addresses. `mNtasiDisplayWindows`, `mNtasiAopWindows`,
`mNtasiIspWindows`, `mNtasiBatteryProperties` are the same. A second machine
either forks the file or grows a parallel table beside every existing one.

The J414s DSDT has 53 such literals and `MCA.asl` has 64. Every one of them is
in the ADT that iBoot hands us at boot.

## The device-tree question

> is it possible to improve the way we write packages/device families? take a
> look at linux device trees

The answer is already in the tree, written by the upstream author on 2025-05-25
in `T602XFamilyVirtualMemoryMapDefines.h`:

> What I would really want is a way to generalize this based on a memory map
> passed in. This *seems* like it should fall to the DeviceTree...

And someone tried: `Silicon/Apple/T600XFamilyPkg/DeviceTree/` held 3817 lines of
Asahi's `.dts`/`.dtsi` for T6002, vendored and referenced by no build file. That
copy is gone. Its replacement is `Silicon/Apple/AppleSiliconPkg/DeviceTree/`,
which vendors the complete include closure of every Mac Asahi describes, pinned
by `SOURCE.json`, compiled by `Tools/mkdtb.sh`, and read by `Tools/socfacts.py`
and `Tools/add-soc.py` when a SoC family package is created. The output of that
reading is committed as static source, so no build file references the trees
either -- the difference is that now something does.

Copying Linux's model is the wrong move, and we do not need it. Linux ships a
device tree per machine because it has to. We are handed the machine's real
device tree at boot: iBoot's ADT describes every MMIO window, interrupt line,
clock gate and DART on the machine, authored by the vendor for that exact unit.
`AppleDTLib` already parses it and 20 files already use it. We are ignoring a
better source than the one we would be copying.

### Proposed shape

Three tiers, replacing five hand-written files per machine.

**Tier 1: read it from the ADT.** Anything that is a base address, a size, an
interrupt number, a DART, a power domain, or a clock. `dt_get()` and
`dt_node_reg()` already do this. This should be the default, and a hardcoded
literal should need a comment saying why the ADT could not supply it.

**Tier 2: one per-machine descriptor.** What the ADT genuinely does not carry,
because it is about Windows rather than about the hardware: which GSIV aliases
the CSRT publishes and why, which devices Windows should not see, the `_LPI`
states m1n1's PSCI validator accepts, SMBIOS strings, quirks. One file per
machine, data not code, consumed by `AcpiPlatformDxe`. This is where the J414s
knowledge currently buried in `mNtasi*` tables goes, and it is small: the real
Windows-specific content in `AcpiPlatform.c` is a few hundred lines, not 4838.

**Tier 3: SoC family package.** Stays as-is, minus the memory map, which comes
from tier 1. That alone is most of a family package: `MemoryInitPeiLib.c` is
1174 lines on T602X and 447 on T810X.

The per-machine `.dsc`, `.fdf`, `.dec` and `PlatformBuild.py` become one
descriptor plus a shared base. The four stock `PlatformBuild.py` files differ
by 2 to 40 lines out of 251; that is not five machines described, it is one
machine described five times.

### What this costs to add a machine, today and after

| | Today | After |
| --- | --- | --- |
| `PlatformBuild.py` | 251 lines, 96-99% shared | one import plus a name |
| `.dsc` / `.fdf` / `.dec` | ~660 lines | one include plus PCD overrides |
| `DSDT.asl` and friends | 237-682 lines, hand-written | generated from ADT plus the tier-2 descriptor |
| SoC family package | 879-2389 lines, new per SoC | ~600, minus the memory map |

## How many M-series chips could we support

> pretty much all M series chips can be easily supported. how much of plumbing
> would really be needed?

The optimism is justified and the estimate needs one correction: it is not
"solely based on linux device trees", it is based on the ADT, with Asahi's
device trees as a cross-check for things the ADT names badly.

What exists now:

| Family | SoC | State |
| --- | --- | --- |
| `T810XFamilyPkg` | M1 | 1132 lines, complete |
| `T811XFamilyPkg` | M2 | **Readme only** |
| `T600XFamilyPkg` | M1 Pro/Max/Ultra | 879 lines, no ACPI tables |
| `T602XFamilyPkg` | M2 Pro/Max | 2389 lines, complete, the working target |
| `T8122FamilyPkg` | M3 | **Readme only** |
| `T8142FamilyPkg` | M5 | complete, on `M5-Dev` |

So M2 and M3 are empty stubs, and M4 has no package at all. The gap between M1
and M2 is small: same core count layout, same AIC generation, an MMIO map that
mostly moves rather than changes shape.

The honest sequence is: do the tier-1 ADT work first, then add SoCs. Adding four
more families at today's cost means four more copies of a 1000-line memory map
that the ADT could have told us. Doing it in the other order means each new
family is a `.dec`, a `.dsc.inc`, and the handful of things that are genuinely
per-SoC: the AIC version, the PPTT topology, and the timer.

What still needs hardware per machine, and cannot be generated: which of the
machine's peripherals actually work under Windows. That is a driver question,
not a firmware one, and it is why "boots" and "supported" are different claims.

## M5-Dev

Unlike m1n1's, this one merges. 19 commits ahead, 3 behind, on a shared base.
`git merge origin/M5-Dev` produces:

- 2 submodule pointer bumps (`Common/MU`, `Silicon/ARM/TIANO`) -- take theirs;
- `.gitmodules`, 5 lines;
- `AppleAicV2Dxe.c`, 4 hunks and 73 lines, AIC2 against AIC3;
- `Tools/j414s_mu_profile_manifest.py`, a whole-file conflict that existed only
  because `M5-Dev` renamed `mu_profile_manifest.py` to be machine-specific and
  then added a `j813` copy beside it. That shim is now deleted and the tool
  carries a `TARGETS` table, so `j813` is a five-line entry rather than a
  second copy, and the conflict is gone.

`AcpiPlatform.c`, `MemoryInitPeiLib.c`, `PlatformBuild.py` and
`FrontpageDsc.inc` all auto-merge.

What it brings, and all of it is worth having:

- `T8142FamilyPkg`, a complete M5 family package.
- `MacBookAir2026Pkg` and `MacBookProLate2025Pkg`.
- `AppleMtpHidDxe`. Keyboard and trackpad over MTP inside UEFI. This is the
  difference between an installer you can use and one that needs a USB keyboard.
- `WindowsPmuCompatDxe`, PMU access compatibility before handoff.
- `AppleDartIoMmuDxe` identity mapping, which T8142's USB DART requires.
- It also *reduces* `AcpiPlatform.c`, which is the direction this fork should be
  going anyway.

Two things to drop on the way in: `Silicon/Apple/AppleSiliconPkg/RAMDisk/ramtest.img`,
a 16 MB binary, and the second copy of the build tooling.

Recommendation: merge it, as its own PR, after this one lands.

## Cross-repo, not done here

`NTASI-AIC2-OVERLAY.json` at the repo root is 150 lines of JSON that nothing
reads for content; it exists to be hashed into a manifest. It should go.

## Done on this branch

- 3141 lines of source-substring tests removed. Four of the sixteen tests in
  `Tests/` were already failing on `main` and nobody had noticed.
- The four stock `PlatformBuild.py` files collapsed from 251 lines each to 38,
  over a 213-line shared module. That found three drift bugs, including two
  platforms whose `ArchSupported = ("AARCH64")` was a string rather than a
  tuple, so `SetArchitectures(["AARCH64"])` raised on the only architecture
  they support.

## Next

1. Land this branch.
2. Merge `M5-Dev`.
3. Tier-1 ADT work in `AcpiPlatformDxe`: replace the `mNtasi*` literal tables
   with ADT lookups, one subsystem at a time, each verified against a live ADT
   capture from the machine it describes.
4. Move the sealing tools to auroradbg, in one commit spanning both repos.
6. Then add SoC families, cheaply.
