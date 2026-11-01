# Adding a machine, and where its facts live

## The question

Every per-machine address in this tree could come from the ADT instead. iBoot
hands us a complete device tree for the machine actually booting: register
windows, interrupt lines, DART bases, power-domain paths. So why is
`AcpiPlatformDxe/AcpiPlatform.c` 4,921 lines with 31 hardcoded 64-bit MMIO
literals in it?

Mostly because nobody decided. The literals came from the ADT in the first
place: `verify-j414s-av-adt.py` in the drivers repo read them off a live J414s
and someone pasted the output into C. They are a copy with no link back to
their source, sitting in `Silicon/Apple/AppleSiliconPkg` — the package every
machine compiles.

## The answer

Read from the ADT what the ADT already states as fact about this machine.
Author what we decided about how Windows should see it.

That line is not a compromise between two positions. It falls out of what ACPI
is for. Take the media device description, which is the densest example:

```c
{
  "MCA0", "NTAS0080", "J414MCA",          // authored
  mNtasiMcaWindows,     9 entries,        // ADT fact
  mNtasiMcaInterrupts,  5 entries,        // authored
  mNtasiMcaProperties,  n entries         // authored
}
```

The windows are where the hardware is. The ADT knows; `AppleDTLib` already has
`dt_node_reg(node, index, &base, &size)` to ask.

The interrupts look like they should be ADT facts and are not. MCA0's published
GSIVs are 40, 41, 42, 43, 45. Its physical AIC lines are 1211, 1213, 1218,
1221, 1231 — all above the GIC carrier's 1019 limit and illegal as GSIVs, so
the CSRT's ALI2 tail translates them. That renumbering is ours. It has to match
m1n1's alias table and the CSRT, and it exists nowhere in the ADT.

Neither are the rest. `"NTAS0080"` is a `_HID` we invented. `ntasp,mca-cluster-count`
is a `_DSD` property name our driver reads. `_STA`, `_PS0` and power resources
are AML methods, which is code, not data — you cannot synthesise them from a
device tree without an AML compiler at runtime.

So: **windows from the ADT, everything else authored.** That is 31 literals, and
it is the whole of what dynamic buys.

## Why not go further

Three reasons, in order of how much they cost.

**The failure mode inverts.** A missing static table entry is a build error. A
missing ADT property is a device that silently does not appear, on a machine
you may not have in front of you. For a device Windows needs in order to boot,
that is the difference between a broken build and a brick.

**ACPI is a contract, not a description.** Windows drivers bind to `_HID`, read
`_DSD` by name, and acknowledge specific GSIVs. None of that is a fact about
the silicon. Generating it from a device tree means inventing the contract at
runtime, on the machine, where you cannot see it.

**Property names are not stable.** The ADT is Apple's, and it changes between
iBoot versions. A static table that disagrees with the machine is a bug you can
find by reading. A dynamic lookup that returns nothing is a bug you find by
bisecting boots.

## Where the windows should actually come from

Not the live ADT. Asahi's device trees.

`aurora-silicon/linux` on the `asahi` branch carries 110 per-machine `.dts`
files over 112 SoC `.dtsi` files. Filtering to Macs, that is 41 machines across
seven SoC families: T8103, T8112, T600X, T602X, T603X, T8122, T8132.

The complete include closure of those 41 -- 103 files, plus the eight
dt-bindings headers they include -- is vendored under
`Silicon/Apple/AppleSiliconPkg/DeviceTree/`, pinned by `SOURCE.json`.
`Tools/mkdtb.sh` compiles any of them without a Linux checkout, and
`Tools/socfacts.py` reads the compiled tree. All 41, plus J813, now have a
platform package.

One machine is not from that set. The MacBook Neo (J700, A18 Pro / T8140) is
not a Mac Asahi supports, and its bring-up happened in
`aurora-silicon/neo-bringup` against a live unit. `DeviceTree/t8140-j700.dts` is
authored rather than vendored, assembled from the rows of that repo's
`docs/hardware-inventory.md` graded "A; locally verified", with the evidence for
each address named in the file. It then goes through the same
`mkdtb.sh -> socfacts.py -> add-soc.py` path as every other SoC, which is the
point of authoring a tree rather than hand-writing the family package.

They are already layered the way this tree wants to be. `t6020-j414s.dts` is 47
lines and does nothing but include `t6020.dtsi` for the SoC and
`t602x-j414-j416.dtsi` for the chassis.

And they carry the exact numbers we hardcoded. Six of the seven MCA windows in
`Include/Platform/NtasiMediaJ414s.h` resolve to a named node in
`t602x-die0.dtsi`:

| our literal | node |
| --- | --- |
| `0x39B600000` | `mca@39b600000` |
| `0x39B500000` | `audio-controller@39b500000` |
| `0x39B400000` | `dma-controller@39b400000` |
| `0x28E03C000` | `clock-controller@28e03c000` |
| `0x39B044000` | `i2c@39b044000` |
| `0x39B04C000` | `i2c@39b04c000` |
| `0x39B028000` | not in that file |

That is better than the live ADT for our purposes, and it removes my main
objection above. A device tree is read at **build** time, so a missing node is
a build error rather than a device that silently does not appear on a machine
nobody has in front of them. It has also been reviewed against real hardware by
people who boot Linux on it, and its node names are stable across machines in a
way Apple's ADT property names are not.

So the shape is: generate the window tables from the `.dts` at build time, emit
them as a checked-in generated header, and keep the current literals as the pin
for J414s. The generator is the next piece of work, and 41 machines is what it
buys.

## The rule for the windows we do move

The literal does not go away. It becomes the pin.

```c
{ 0x39B600000ULL, 0x10000ULL, "/arm-io/mca0", 0 },
```

Resolve the path. If it resolves and matches, nothing changed. If it resolves
and disagrees, say so loudly and keep the literal, because a disagreement means
the path-to-window mapping is wrong and the literal is the one that was checked
on hardware. If there is no literal — a new machine — the ADT supplies it.

That gives a new machine no literals to transcribe, keeps J414s behaviour
identical, and turns a silent transcription error into a message.

## The tiers

The answer above says which facts are ADT and which are authored. It does not
say *where the authored ones live*, and getting that wrong is what produced 43
copies of the same ACPI.

Firmware for a Mac is composed from four tiers. A tier is included only when
its contents are true of the machine.

| tier | package | scope | comes from |
| --- | --- | --- | --- |
| silicon | `Silicon/Apple/<SoC>FamilyPkg` | the die | Asahi's device tree, via `Tools/add-soc.py` |
| chassis | `Platform/<Chassis>FamilyPkg` | the enclosure | authored: FADT power profile, SMBIOS family |
| board | `Platform/<Board>BoardPkg` | the logic board | authored, from a measured machine |
| machine | `Platform/<Machine>Pkg` | one Mac | its identity, as build defines |

The tiers are not a taxonomy imposed on the tree; they are what the measurements
already showed.

**Silicon is derivable.** Memory map, CPU topology, MMIO windows, PCIe windows,
UART base, interrupt-controller base — every one of them is stated by the SoC's
`.dtsi`, and `Tools/add-soc.py` reads them out of a compiled tree. That is why
eleven SoC family packages cover forty-two machines.

**Board is not derivable, and it is not per-machine either.** A keyboard's GPIO
number and an audio complex's published GSIV come from reading a live machine.
But the eight T602X machines were compared with comments stripped and their ACPI
code is byte-identical: they share a board, so they share the tables. Naming the
package for the machine it was measured on — `J414sBoardPkg`, not
`T602XBoardPkg` — keeps the provenance in the name.

**A machine that has not been measured gets `GenericBoardPkg`**, which declares
the PCIe root bridge from PCDs and nothing else. No keyboard, no audio, no
display. That is the honest description, and it is recoverable: measure the
board, give it a package.

### What went wrong before the tiers

Machines were added by copying a measured machine's package and rewriting the
machine name through every file. The addresses that produced were right, because
they are properties of the SoC. The comments were not:

```
  Tests/test_j416s_media_acpi_contract.py pins this file against that C table.
  tools/verify-j474s-av-adt.py asserts the address and the register count.
  Linux arch/arm64/boot/dts/apple/t602x-j416-j416.dtsi
  the pinned J514s geometry
```

None of those files exist. Each is a real citation from J414s with the machine
name substituted, so a table whose numbers had been checked on one machine
claimed to have been checked on another — and cited a script, a device tree and
an ADT capture that were never written. Duplication is a maintenance cost.
Duplication plus substitution is a correctness one: it makes an unverified
address look verified, which is the one thing firmware provenance exists to
prevent.

Two more things fell out of the same copying. `DISP.asl` and
`Media/{MCA,AOPA,ISP}.asl` were copied beside every machine and referenced by no
build file — those devices are emitted from C in `AcpiPlatformDxe`, and the ASL
is a description of what that C does. It now lives once, in
`Drivers/AcpiPlatformDxe/AcpiReference/`. And every platform carried its own
`FADT.aslc` while the chassis package emitted one too, so the table was
installed twice; the two were byte-identical, and the platform copy is gone.

### The bin

Asahi's tree describes the die, and Apple bins these parts: an M2 Pro die
carries twelve cores and ships as ten or twelve, an M4 die carries ten and ships
as eight or ten. A generated MADT is therefore die-sized, and publishing a GICC
for a core that is not fused on is not a harmless overstatement — Windows issues
PSCI `CPU_ON` for every enabled entry and answers a truthful failure with
`SYSTEM_RESET`.

`AcpiPlatformDxe` trims the MADT against the live ADT before installing it,
matching on identity rather than count because the absent cores are not the last
ones: on a 10-core J414s they are slots 7 and 11, the last of each P cluster.
The arithmetic is in `Include/Drivers/NtasiMadtTrim.h` and is tested without
hardware by `Tests/test_madt_adt_trim.c`.

That is what makes generating from the die safe. The DSDT's processor tree is
die-sized too and is not trimmed; a device with no MADT entry backs no processor
and is not started, so the overstatement is cosmetic there.

## What adding a machine costs now

| | Lines | Where it goes |
| --- | --- | --- |
| `PlatformBuild.py` | 38 | boilerplate over `PlatformBuildCommon` |
| `.dsc` / `.fdf` | ~480 | still mostly copied — the remaining duplication |
| ACPI tables | 0 | it shares a board package, or uses the generic one |
| Feature set | 1 entry | `Platform/Features.py` |
| SoC family package | 0 | one per SoC, generated, shared by every machine on it |

Adding a Mac no longer means authoring or copying any ACPI. The `.dsc`/`.fdf`
pair is what is left: 20,488 committed lines across forty-three platforms, of
which two maximally different machines differ in 30 and 10 lines respectively.
