# T8142-specific platform support UEFI package

## About

This package provides SoC-specific code for machines using the T8142 SoC (Apple M5).
Mainly SoC-specific ACPI tables, the virtual memory map, and memory init.

Structured after `T810XFamilyPkg`, which is the only complete SoC package in this
tree. Note that `T8122FamilyPkg` (M3) is a Readme-only stub, and there is no M4
package, so there was no closer starting point.

## Status

Builds clean. **Never run on hardware.** Everything here is derived from static
analysis: the J704 Apple Device Tree read out of macOS, cross-checked against a
real M4 (T8132) capture. Treat every value as unverified until something boots.

## Key facts

| | |
|---|---|
| SoC | T8142, chip-id `0x8142` |
| Board | J704 (MacBook Pro 14", `Mac17,2`) |
| Cores | **10: 6 E-cores + 4 P-cores** (asymmetric) |
| DRAM | base **`0x10000000000`**, 24 GiB — *not* T810X's `0x800000000` |
| AIC | v3 @ `0x381000000`, size `0x1cc000` |
| UART0 | `0x3a5200000` |
| PMGR | `0x380700000` |

## Things that differ from the other SoC packages

**The core topology is asymmetric.** Every other package in this tree assumes a
uniform `CLUSTER_CORE_COUNT` and declares a single `PPTT_CLUSTER` type. 6+4 cannot
be expressed that way, so `AcpiTables/PPTT.aslc` declares a distinct type per
cluster (`PPTT_ECORE_CLUSTER`, `PPTT_PCORE_CLUSTER`). Do not refactor it back into
the shared shape.

**The memory map was derived, not inherited.** `Include/Library/T8142FamilyVirtualMemoryMapDefines.h`
comes from the harvested ADT rather than a previous SoC — 12 core MMIO ranges plus
9 PCIe ranges (2 ECAM + 7 BAR windows), each annotated with what actually lives
there.

**DRAM is at the 1 TB mark**, not `0x800000000`. `T810XFamilyPkg` — the package this
one is modelled on — is the outlier here; `T600XFamilyPkg` and `T602XFamilyPkg`
already use `0x10000000000` and are the right reference for anything DRAM-relative.
An early version of this package inherited T810X's `PcdSecPhaseStackBase`
(`0x900000000`), which is not inside T8142's DRAM at all and would have faulted as
soon as SEC touched its stack.

**M5 derives from M4, not M3.** AIC, PMGR, watchdog and all four GPIO blocks sit at
addresses identical to T8132, and both are 6E+4P with DRAM at `0x10000000000`
(confirmed against a real M4 capture). The `everest`/`sawtooth` core naming is not
an M5 quirk — M4's ADT uses it too.

Several blocks *did* move, by differing amounts: the SIO block (UART, I²C, SPI) by
`-0x8000000`, but `jpeg0/1`, `scaler0` and `sep` much further. M5's `uart0`
(`0x3a5200000`) lands where M4 kept `jpeg0` (`0x3a5000000`) — which is why every
address here was derived from J704's own ADT rather than shifted from M4's.

## Verified so far

The generated ACPI tables were decompiled and checked against the hardware data:

- **MADT** — 10 GICC entries; MPIDRs `0x0`–`0x5` (E-cores) and `0x10100`–`0x10103`
  (P-cores), matching the T8132 device tree.
- **PPTT** — 1 package → 2 clusters → 6 + 4 cores, parent offsets resolving
  correctly, ACPI processor UIDs matching the MADT.

## Cross-component constraint

The vGIC base addresses must stay in sync with m1n1's `hv_vgic.c`, which emulates
the GIC these values describe:

| | This package | m1n1 `hv_vgic.c` |
|---|---|---|
| Distributor | `PcdGicDistributorBase` = `0xf00000000` | `DIST_BASE_36_BIT` |
| Redistributors | `PcdGicRedistributorsBase` = `0xf10000000` | `REDIST_BASE_36_BIT` |
| ITS | MADT ITS base = `0xF20000000` | `ITS_BASE_36_BIT` |

If one side moves and the other does not, the firmware programs a GIC at addresses
nothing is emulating and interrupts die silently.

## Known incomplete

- **PMU interrupt numbers are `0`** in the MADT. T8132 expresses them as AIC
  `fiq-index` affinities rather than plain numbers and we have not read them off
  J704. Zero means "no PMU interrupt", which is honest; the values from
  `T810XFamilyPkg` are for different silicon and contain apparent typos, so they
  were deliberately not copied.
- **`PcdBootArgsPointer` / `PcdAdtPointer` are unverified.** They are now inside
  T8142's DRAM (using T602X's offsets), which is necessary but not sufficient —
  they must match where m1n1 actually deposits those structures. T602X carries the
  same TODO.
- **No CSRT.** `CSRT.aslc` is a documented placeholder, not in the build. The AIC
  half of the requirement is moot (m1n1 presents a GICv3, so Windows never sees
  the AIC), but the DART/IOMMU side is genuinely unaddressed and will matter for
  anything doing DMA.
- PCIe ranges use `ARM_MEMORY_REGION_ATTRIBUTE_DEVICE` (nGnRnE) where they should
  be nGnRE. That limitation is inherited — EDK2 here has no attribute for it, and
  the other packages have the same TODO.

## Resolved

- **PCIe PCDs** are no longer T810X placeholders. ECAM is `0x1cb0000000`
  (`apcie` reg[0], 256MB = 256 buses); MMIO windows decoded from the controller's
  `ranges`: MMIO32 bus `0x80000000` -> CPU `0xb80000000` (translation
  `0xb00000000`), MMIO64 `0xbc0000000` 1:1.
- **All 58GB of PCIe BAR space is now mapped** (ranges 3-9). These come from the
  bridges' `ranges` properties, which no `reg`-based check can see -- they were
  entirely absent from the memory map before, and enumerating PCIe would have
  faulted. The resulting layout matches `T602XFamilyPkg`'s ranges 3-6 exactly,
  which is a useful independent check on the decode.
- **`GTDT.aslc` verified**, not merely compiled: fully PCD-driven, and its timer
  GSIVs match what m1n1 injects (`CNTP_CTL_EL02` -> vINTID 17 =
  `PcdArmArchTimerIntrNum`, `CNTV_CTL_EL02` -> vINTID 18 =
  `PcdArmArchTimerVirtIntrNum`). A mismatch would have had Windows arm a timer
  and never receive a tick.
- **`BERT.aslc` deleted.** Never in `[Sources]`, referenced nowhere, and would not
  have compiled (`BOOT_ERROR_REGION_BASE` had its `#define` commented out).
  Project Mu supplies its own BERT via `MsWheaPkg` (`HwErrorBert.efi`), which
  *is* in the build.
