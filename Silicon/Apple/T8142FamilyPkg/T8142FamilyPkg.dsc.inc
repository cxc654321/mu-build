#
# T8142 (Apple M5) SoC configuration.
#
# Derived from T810XFamilyPkg. Values that differ from that reference are those
# read off J704 hardware -- see docs/T8142-PLATFORM.md and docs/T8132-VS-T8142.md
# in the project root.
#
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

[PcdsPatchableInModule.common]
  #
  # These will get overriden by the FDT settings.
  #
  # DRAM on T8142 starts at the 1TB mark, NOT at 0x800000000 like T810X (M1).
  # Read from the J704 ADT /chosen: dram-base = 0x10000000000,
  # J704 reports 0x600000000 (24GB); J813 reports 0x400000000 (16GB). Each
  # platform defines T8142_SYSTEM_MEMORY_SIZE before including this file.
  #
  # This matches T600XFamilyPkg and T602XFamilyPkg, which already use the same
  # 1TB base. T810X is the outlier -- do not copy DRAM values from it.
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x10000000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|$(T8142_SYSTEM_MEMORY_SIZE)
  gArmTokenSpaceGuid.PcdFdBaseAddress|0x0
  gArmTokenSpaceGuid.PcdFvBaseAddress|0x0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferAddress|0x0
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferSize|0x0

[PcdsFixedAtBuild.common]
  # Setting timer frequency to 0 means "read from CNTFRQ_EL0"
  #gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|0
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|19 # placeholder
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|17 # phys timer IRQ number (this is arbitrary because per-cpu fiq)
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|20 #this disables the HV timer
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|18 #virt timer
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSartVersion|2

  #
  # Boot args / ADT pointers. These are PcdsFixedAtBuild -- unlike
  # PcdSystemMemoryBase above they are NOT patched at runtime, so a value outside
  # DRAM is fatal rather than merely wrong.
  #
  # The T810X values (0x840000000 / 0x840004000) are relative to ITS 0x800000000
  # DRAM base and land nowhere near T8142's DRAM. Using T602X's offsets instead,
  # which are DRAM + 0x220000000 (8.5GB in) and therefore inside our 24GB span.
  #
  # TODO: unverified. These must match where m1n1 actually deposits the boot args
  # and the ADT on T8142 -- an address that is merely inside DRAM is necessary but
  # not sufficient. T602X carries its own "verify this hack still works" TODO for
  # the same reason. Confirm against m1n1 before trusting a boot attempt.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdBootArgsPointer|0x10220000000
  gAppleSiliconPkgTokenSpaceGuid.PcdAdtPointer|0x10220004000
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuIdentifier|"T8142"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosCpuModel|"Apple M5"

  #
  # Serial port base address. This is uart0 from the J704 ADT.
  #
  # NOTE: this is NOT the same as T8132/M4's 0x3ad200000 -- the SIO block moved
  # down by 0x8000000 between M4 and M5. It is the one address in this file that
  # could not have been inherited from the previous generation.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartBase|0x3a5200000

  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x8142
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE

  #
  # SEC phase stack. T810X's 0x900000000 is outside T8142's DRAM entirely, which
  # would have faulted the moment SEC touched its stack. DRAM + 12GB, matching
  # T600X/T602X, and comfortably inside our 24GB.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdSecPhaseStackBase|0x10300000000

  #
  # 10 cores: 6 E (cluster 0) + 4 P (cluster 1). Asymmetric -- the ACPI tables in
  # this package handle the two cluster sizes explicitly rather than assuming a
  # uniform cores-per-cluster count the way the other SoC packages do.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdCoreCount|10|UINT32|0x00004406
  gAppleSiliconPkgTokenSpaceGuid.PcdClusterCount|2|UINT32|0x00004407

#AIC/GIC configuration settings
  #
  # If GIC is used, it is expected to be virtualised (by m1n1's vGIC, in this
  # project's case) at the following addresses, as Apple silicon has no onboard
  # GIC.
  #
  # Set for every build, including AIC builds.  These were previously inside
  # `!if $(AIC_BUILD) == FALSE`, but the MADT in this package describes a GICv3
  # unconditionally -- an AIC build still emits GICC, GICD and GICR structures.
  # Leaving the addresses at their UINT64 default of 0 therefore did not omit
  # the GIC from the tables, it published one located at physical address 0.
  # Windows believes the MADT: it mapped page 0 as the redistributor and took a
  # stage-2 fault on the first GICR_ICENABLER0 write, which ends the boot.
  #
  # m1n1 backs both addresses in AIC builds through its startup carrier: hv.c
  # calls hv_vgicv3_init() on the native-AIC path, which installs the
  # distributor and redistributor MMIO hooks at exactly these two values.  The
  # carrier answers the kernel's controller classification only -- HCR.IMO stays
  # clear and no list registers are enabled -- and it deliberately does not hook
  # an ITS, which is why the ITS entry is gone from MADT_Static.aslc.
  #
  # So these addresses describe what the guest is actually given, and gating
  # them on the AIC flag described the hypervisor incorrectly rather than
  # describing less of it.
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0xf00000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0xf10000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00008142

!if $(SECURE_BOOT_ENABLE) == TRUE
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|600
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|400
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|1500
!else
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|300
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|150
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|1000
!endif
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|800
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|10
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderData|0
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|40

  #PCIe settings
  gEfiMdeModulePkgTokenSpaceGuid.PcdSrIovSupport|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdAriSupport|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdMrIovSupport|FALSE
  #placeholder settings
  gArmTokenSpaceGuid.PcdPciBusMin|0x0
  gArmTokenSpaceGuid.PcdPciBusMax|0xFF
  #
  # Apple chips don't seem to use "x86-style" I/O accesses, so use placeholder values here, and don't reference them in the host bridge driver.
  #
  gArmTokenSpaceGuid.PcdPciIoBase|0x0
  gArmTokenSpaceGuid.PcdPciIoSize|0x00010000
  #
  # PCIe windows for the INTERNAL root complex (`apcie` -- NVMe, Wi-Fi/BT, SD
  # reader). All values decoded from the `ranges` property of the apcie node in
  # the J704 ADT; see T8142FamilyVirtualMemoryMapDefines.h for the full layout
  # including the Thunderbolt controllers.
  #
  # EDK2 convention: *Base is the bus/child address and *Translation is what gets
  # added to reach the CPU-physical address.
  #
  # The 32 bit PCIe MMIO is not prefetchable.
  #   ADT: child 0x80000000 -> cpu 0xb80000000, size 0x40000000 (1GB)
  #
  gArmTokenSpaceGuid.PcdPciMmio32Base|0x80000000
  gArmTokenSpaceGuid.PcdPciMmio32Size|0x40000000
  gEfiMdePkgTokenSpaceGuid.PcdPciMmio32Translation|0xb00000000
  gEfiMdePkgTokenSpaceGuid.PcdPciMmio64Translation|0x0
  #
  # 64 bit PCIe MMIO *is* prefetchable.
  #   ADT: child 0xbc0000000 -> cpu 0xbc0000000, size 0x20000000 (512MB), 1:1
  #
  gArmTokenSpaceGuid.PcdPciMmio64Base|0xbc0000000
  gArmTokenSpaceGuid.PcdPciMmio64Size|0x20000000

  #
  # PCIe ECAM: the internal root complex's config space, apcie reg[0].
  # 0x10000000 = 256MB = 256 buses x 1MB, consistent with PcdPciBusMax = 0xFF.
  #
  # (Previously carried over from T810X as 0x690000000, which is not a PCIe
  # address on this SoC at all.)
  #
  gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress|0x1cb0000000

  #
  # Inclusive limits and the bus count, derived from the base and size
  # above. GenericBoardPkg's root bridge _CRS needs them stated rather
  # than computed: iasl substitutes a PCD fine but will not fold
  # arithmetic in a resource descriptor.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdPciBusCount|0x100
  gAppleSiliconPkgTokenSpaceGuid.PcdPciMmio32Limit|0xBFFFFFFF
  gAppleSiliconPkgTokenSpaceGuid.PcdPciMmio64Limit|0xBDFFFFFFF
  gAppleSiliconPkgTokenSpaceGuid.PcdPciExpressBarSize|0x10000000
  gAppleSiliconPkgTokenSpaceGuid.PcdPciExpressBarLimit|0x1cbfffffff

  gEmbeddedTokenSpaceGuid.PcdDmaDeviceOffset|0xFFFFFFFB00000000
  #
  # This shouldn't get used.
  #
  gEfiMdePkgTokenSpaceGuid.PcdPciIoTranslation|0x0

[LibraryClasses.common]
  MemoryInitPeiLib|T8142FamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.inf
  MsPlatformDevicesLib  |T8142FamilyPkg/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf

[Components.common]

  T8142FamilyPkg/AcpiTables/CPUAcpiTables.inf
