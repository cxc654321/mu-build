## @file
#  MacBook Pro Early 2023 DSC file, borrowing aspects from SurfaceDuo2.dsc from WOA-Project/SurfaceDuoPkg
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#
#  Copyright (c) 2026 Aurora Silicon
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################

[Defines]
  PLATFORM_NAME                  = MacStudioUltra2023
  DEFINE NTASI_ENABLE_WIRELESS_DART_HANDOFF = 0
  DEFINE NTASI_ENABLE_XHC2 = 1
  # The PIPE switch is a firmware capability, selected per build.
  # With xhc2 on the switch finishes port 2; with it off, phase 1 finishes only
  # the left boot-volume port 1. PlatformBuild.py supplies the value.
  DEFINE NTASI_USB3_PIPE_SWITCH_PORT_MASK = 0x4
  DEFINE NTASI_GPU_RESOURCE_PROFILE = 0
  # Publish NTAS0023 (AppleAgxGpu) from AcpiPlatformDxe. Strictly narrower than
  # NTASI_GPU_RESOURCE_PROFILE, which only reserves the ADT-derived
  # carveouts in the GCD: this one additionally emits the device, its eight
  # _CRS resources and its GSIV-46 interrupt. Kept separate so "reserve the
  # carveouts" and "publish the ACPI device" stay independently selectable --
  # the same split ans/ans_acpi already has, and the reason the ans-noacpi
  # control could isolate the 0x144. Setting this without the gpu feature
  # does nothing: the whole block is nested inside it.
  DEFINE NTASI_ENABLE_GPU_ACPI_PUBLICATION = 0
  # Closed selector for the driver-facing GPU ACPI identity. 23 binds the
  # Vulkan KMD (NTAS0023); 24 binds the WDDM KMD (NTAS0024). PlatformBuild.py
  # supplies the value and AcpiPlatform.c rejects anything else
  # at compile time.
  DEFINE NTASI_GPU_ACPI_HID = 23
  DEFINE NTASI_ANS_DXE_BRINGUP = FALSE
  DEFINE NTASI_ANS_PUBLISH_BLOCK_IO = FALSE
  DEFINE NTASI_ANS_PRESERVE_FOR_OS = FALSE
  DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE
  # Media features: publish MCA0 (NTAS0080), AOPA (NTAS0081) and ISP0
  # (NTAS0090) from AcpiPlatformDxe. OFF by default, and off means
  # preprocessor-excluded, so a build without it produces byte-identical
  # firmware rather than merely equivalent firmware. Adds no FFS module and no
  # ACPI table to the FV (the SSDTs are generated at DXE runtime, like ANS0 and
  # DRT0), so expected_ffs_count and the 94-image count are unchanged. Publishes
  # ZERO interrupt resources and touches no CSRT byte -- see
  # NtasiInstallMediaTables() in AcpiPlatform.c.
  # THREE INDEPENDENT FLAGS, one per device -- there is deliberately no umbrella
  # flag, because one flag meant a machine that wanted working speakers also got
  # a camera devnode and its eight memory windows, which is exactly the coupling
  # that makes an experiment unattributable.
  #
  #   MCA0 (NTAS0080)  speakers + headset jack.  The ONLY one that changes a
  #                    CSRT byte: its five AIC lines (1211-1231) are above the
  #                    GIC carrier's 1019 limit, so CSRT.aslc must carry the
  #                    "m2-pro-media" ALI2 table when this is 1.
  #   AOPA (NTAS0081)  AOP coprocessor; its driver enumerates the internal PDM
  #                    microphone array as a PnP child.  AIC 631, below 1019,
  #                    identity mapped, no CSRT entry.  Publishes NO pmgr_east
  #                    window, so unlike MCA0/ISP0 it cannot collide with KBL0.
  #   ISP0 (NTAS0090)  FaceTime camera ISP.  AIC 569, likewise no CSRT entry.
  DEFINE NTASI_ENABLE_MCA_PUBLICATION = 0
  DEFINE NTASI_ENABLE_AOP_PUBLICATION = 0
  DEFINE NTASI_ENABLE_ISP_PUBLICATION = 0
  # Battery: publish BAT0 (NTAS0053) from AcpiPlatformDxe, the devnode the
  # AppleSmcBattery battc miniport binds to so Windows shows a real battery.
  # Unconditional: every build publishes BAT0. The generator is no longer
  # #if-gated in AcpiPlatform.c, so this define is informational only (it no
  # longer controls compilation); it stays 1 so build manifests report battery
  # publication as enabled in every build, matching the firmware. Adds no FFS
  # module and no ACPI table to the FV (the SSDT is generated at DXE runtime,
  # like ANS0, DRT0 and the media tables), so expected_ffs_count and the
  # 94-image count are unchanged. Publishes ZERO interrupts and ZERO memory
  # windows -- its _CRS is an empty resource template -- so it allocates no
  # GSIV, touches no CSRT byte, and cannot collide with SMCG's exclusive claim
  # on the SMC ASC and SRAM windows. See NtasiInstallBatteryTable() in
  # AcpiPlatform.c.
  DEFINE NTASI_ENABLE_BATTERY_PUBLICATION = 1
  # Display interrupts: add the five AIC lines the DCP path needs -- the ASC
  # mailbox quad 932-935 and the shared DART fault line 911 -- to the dynamic
  # NTAS0070 _CRS. OFF by default, and off means preprocessor-excluded, so a build
  # without it produces byte-identical firmware. The device itself (NTAS0070,
  # its six MMIO windows and its _DSD) is published unconditionally and is
  # unaffected by this switch.
  #
  # OFF is not timidity. Publishing a resource is a promise PnP must keep: if
  # any one of the five cannot be routed, NTAS0070 does not start at all, and
  # the AppleDisplay driver's entire design rule is that every failure leaves
  # BasicDisplay owning the panel. Without interrupts the driver selects
  # bounded polling and still works; with an unroutable line it does not start.
  # Turn this on only for a boot whose purpose is to test interrupt delivery,
  # and expect to lose the display if the answer is no.
  DEFINE NTASI_ENABLE_DISPLAY_INTERRUPTS = 0
  # Publish NTAS0070 from AcpiPlatformDxe so its _CRS can include the live,
  # boot-specific framebuffer interval. A static ASL table cannot express it.
  DEFINE NTASI_ENABLE_DISPLAY_ACPI_PUBLICATION = 1
  # WinPE deploy-verdict echo (BootRamdiskHelperDxe). OFF by default: it adds a
  # participant to the ReadyToBoot event group and opens every attached FAT
  # volume microseconds before the OS loader starts, on a machine whose boot disk
  # is USB and whose DEBUG build deadloops on any ASSERT. Turn it on for a
  # deploy-verification boot with
  #   NTASI_DEPLOY_EVIDENCE_ECHO=1 ./Tools/build-windows-native.sh j475d <build>
  # and leave it off for every boot whose result is meant to be attributable.
  DEFINE NTASI_DEPLOY_EVIDENCE_ECHO = 0
  #
  # Whether this SoC's family package found a PCIe root complex in
  # Asahi's tree. The board DSDT declares its root bridge only when
  # there is one to describe.
  #
  DEFINE NTASI_SOC_HAS_PCIE = 1

  PLATFORM_GUID                  = d70b31ca-2cbc-433b-885f-b8bbda409959
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacStudioUltra2023-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacStudioUltra2023Pkg/MacStudioUltra2023.fdf
  SECURE_BOOT_ENABLE             = FALSE #disable secure boot for now
  AIC_BUILD                      = TRUE  # Mu uses native AIC; m1n1 supplies only the later Windows startup carrier
  NETWORK_TLS_ENABLE             = TRUE
  # Experimental only. Build with
  #   BLD_*_NTASI_T6020_J475D_HOMOGENEOUS_EFFICIENCY=1
  # to publish PEC 0 for all ten processors without changing MPIDRs, CPU UIDs,
  # or PPTT topology. PlatformBuild.py supplies the default value of 0.

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=6020
  *_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS -DNTASI_T6020_J475D_HOMOGENEOUS_EFFICIENCY=$(NTASI_T6020_J475D_HOMOGENEOUS_EFFICIENCY) -DNTASI_ENABLE_WIRELESS_DART_HANDOFF=$(NTASI_ENABLE_WIRELESS_DART_HANDOFF) -DNTASI_GPU_RESOURCE_PROFILE=$(NTASI_GPU_RESOURCE_PROFILE) -DNTASI_ENABLE_GPU_ACPI_PUBLICATION=$(NTASI_ENABLE_GPU_ACPI_PUBLICATION) -DNTASI_GPU_ACPI_HID=$(NTASI_GPU_ACPI_HID) -DNTASI_ENABLE_MCA_PUBLICATION=$(NTASI_ENABLE_MCA_PUBLICATION) -DNTASI_ENABLE_AOP_PUBLICATION=$(NTASI_ENABLE_AOP_PUBLICATION) -DNTASI_ENABLE_ISP_PUBLICATION=$(NTASI_ENABLE_ISP_PUBLICATION) -DNTASI_ENABLE_BATTERY_PUBLICATION=$(NTASI_ENABLE_BATTERY_PUBLICATION) -DNTASI_ENABLE_DISPLAY_ACPI_PUBLICATION=$(NTASI_ENABLE_DISPLAY_ACPI_PUBLICATION) -DNTASI_ENABLE_DISPLAY_INTERRUPTS=$(NTASI_ENABLE_DISPLAY_INTERRUPTS) -DNTASI_DEPLOY_EVIDENCE_ECHO=$(NTASI_DEPLOY_EVIDENCE_ECHO)
  *_*_*_ASLPP_FLAGS = -DNTASI_SOC_HAS_PCIE=$(NTASI_SOC_HAS_PCIE) -DNTASI_ENABLE_XHC2=$(NTASI_ENABLE_XHC2) -DNTASI_ENABLE_DISPLAY_INTERRUPTS=$(NTASI_ENABLE_DISPLAY_INTERRUPTS)



[PcdsFixedAtBuild.common]
  # This machine's SoC, from its device tree compatible. The family
  # package pins the base part of the family; a Max or Ultra is not it.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleSocIdentifier|0x6022
  # This firmware is RAM-loaded by m1n1 and has no persistent UEFI variable
  # store. The normal first-boot memory-type update reset would therefore
  # repeat on every launch instead of stabilizing after one reboot.
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  # Boot Windows deterministically. The J475d SSD dual-boots Fedora, whose
  # installer placed its own ESP at a lower partition number than the Windows
  # ESP; MsBootPolicy's device sort then reaches Fedora's ESP first, and the
  # stock removable-media expansion boots its \EFI\BOOT\BOOTAA64.EFI (shim)
  # instead of Windows (measured: serial-console.log 2026-08-06 20:00 boot,
  # HD(4,GPT,F923AC81...) shim selected while HD(8,GPT,139FDE03...) still
  # carried a readable \EFI\Microsoft\Boot\BCD). With this TRUE, the HDD boot
  # element boots \EFI\Microsoft\Boot\bootmgfw.efi exclusively and skips every
  # filesystem that lacks it. Set FALSE to restore stock selection.
  gPcBdsPkgTokenSpaceGuid.PcdForceWindowsBootManager|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Mac Studio (M2 Ultra, 2023)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"J475d"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"Mac Studio (M2 Ultra, 2023) (J475d)"
  #
  # Finish exactly the deferred USB3 PIPE selected by the sealed build:
  # 0x2 = left boot-volume port for phase-1 no-XHC2 isolation;
  # 0x4 = right port for right-enabled builds. Never select both implicitly.
  # Windows consumes GSIV 38; the AIC2 CSRT translates it to T6020 line 1832.
  # ANS publication is the only storage-firmware experiment.  The unified
  # baseline leaves this FALSE; the ans build build overrides it to TRUE.
  # DECOUPLED 2026-07-30 from NTASI_ENABLE_ANS (which gates the driver FFS) so
  # "the ANS driver is in the FV" and "NTAS2003 is published to Windows" can be
  # varied independently. The `ans-noacpi` build is exactly that experiment:
  # identical FFS set to `ans`, no NTAS2003.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishAcpiDevice|$(NTASI_ANS_PUBLISH_ACPI)
  # Mu-side ANS bring-up. FALSE leaves the coprocessor exactly as iBoot left it
  # (running), which is the state every booting build has. Flip
  # NTASI_ANS_DXE_BRINGUP to TRUE and rebuild to restore the full bring-up.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPerformDxeBringUp|$(NTASI_ANS_DXE_BRINGUP)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishBlockIo|$(NTASI_ANS_PUBLISH_BLOCK_IO)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPreserveForOs|$(NTASI_ANS_PRESERVE_FOR_OS)
  # CORRECTED 2026-07-30 (hardware-confirmed): these four were resolved
  # against the wrong PMGR register block. /arm-io/pmgr's "ps-regs" table
  # has multiple blocks (reg tuples); ANS2/APCIE_ST/APCIE_ST_SYS/
  # APCIE_ST1_SYS live in the "pmgr_east" block (base 0x290280000), not the
  # main "pmgr" block (base 0x28E080000) these values used to point into.
  # The old 0x28E080-prefixed addresses land on DCS_09/DCS_10 -- DRAM
  # controller power domains -- at the exact same low offsets, which is why
  # they passed every alignment/spacing sanity check while being
  # catastrophically wrong. Live ADT walk of /arm-io/pmgr "devices",
  # matching by name, confirms these four; TODO(Delivery 2): resolve them
  # from the ADT at runtime instead of trusting this constant again.

[Components.common]

  J414sBoardPkg/AcpiTables/DeviceAcpiTables.inf

!include J414sBoardPkg/J414sBoardPkg.dsc.inc
!include MacStudioFamilyPkg/MacStudioFamilyPkg.dsc.inc
!include T602XFamilyPkg/T602XFamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc
