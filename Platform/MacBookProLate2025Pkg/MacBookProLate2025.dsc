#Mac Mini 2020 UEFI DSC file
#some parts borrowed from WOA-Project/SurfaceDuoPkg
#Disclaimer: probably not the best UEFI dev out there
#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#
#  This program and the accompanying materials
#  are licensed and made available under the terms and conditions of the BSD License
#  which accompanies this distribution.  The full text of the license may be found at
#  http://opensource.org/licenses/bsd-license.php
#
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
#
#

#Copyright (c) 2026 Aurora Silicon
#SPDX-License-Identifier: BSD 2-Clause

#Basic Defines

[Defines]
  PLATFORM_NAME                  = MacBookProLate2025
  PLATFORM_GUID                  = 28e8d1bb-24c7-41ca-95ca-756a4a1e26a8
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacBookProLate2025-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacBookProLate2025Pkg/MacBookProLate2025.fdf
  SECURE_BOOT_ENABLE             = FALSE #disable secure boot for now
  DEFINE T8142_SYSTEM_MEMORY_SIZE = 0x600000000
  #
  # FALSE = use m1n1's emulated GICv3 instead of Apple's AIC.
  #
  # This is mandatory for Windows: Windows on ARM64 has no AIC support at all, and
  # the MADT this platform builds already describes a GICv3/GICR rather than an
  # AIC (see T8142FamilyPkg/AcpiTables/MADT_Static.aslc, whose header says outright
  # that the GIC it describes is the one m1n1 emulates). Leaving AIC_BUILD TRUE
  # would have UEFI drive the AIC while the ACPI tables promise the OS a GIC.
  #
  # Addresses check out: T8142FamilyPkg.dsc.inc sets PcdGicDistributorBase
  # 0xf00000000 / PcdGicRedistributorsBase 0xf10000000, and m1n1's `case T8142:` in
  # src/hv_vgic.c assigns DIST_BASE_36_BIT 0xF00000000 / REDIST_BASE_36_BIT
  # 0xF10000000. (The "Initializing for 42-bit PA range" line in the boot log is
  # the CPU's PA width, not where the vGIC is placed -- do not "fix" these to the
  # 42-bit constants.) Neither address falls in any range m1n1 maps as MMIO.
  #
  # REVERTED TO TRUE after FW-14 tested on hardware. Do not flip this again until
  # the m1n1 side is finished -- FW-14 hung in ArmGicDxe:
  #
  #     Loading driver at ... ArmGicDxe.efi
  #     UpdateRegionMappingRecursive: F00000000 - F00001000    <- GICD, correct
  #     UpdateRegionMappingRecursive: 0 - 2000                 <- GICC at base 0
  #     <dead>
  #
  # 0x2000 at base 0 is a GICv2 CPU interface with PcdGicInterruptInterfaceBase
  # unset. ArmGicDxe took the v2 path because GicV3Supported() (ArmGicDxe.c:22)
  # gates on ArmHasGicSystemRegisters(), i.e. ID_AA64PFR0_EL1.GIC, and got 0.
  #
  # m1n1 already intends to advertise a GIC -- hv_exc.c:550 ORs (1 << 24) into
  # ID_AA64PFR0_EL1 -- but three things block it:
  #
  #   1. HCR_EL2.TID3 is never set (hv.c:76), so guest ID-register reads do not
  #      trap and the spoof never runs.
  #   2. The ICC_* CPU-interface handlers (IAR1/EOIR1/BPR1/IGRPEN1) sit inside a
  #      block comment at hv_exc.c:454, "only needed if ICH_HCR_EL2.TALL1 is set".
  #   3. ICC_SRE_EL1 is not emulated at all, and GicV3Supported() requires it.
  #
  # This is hypervisor work, not a firmware flag. See docs/WINDOWS-ROADMAP.md
  # Phase 3. UEFI runs fine on AIC meanwhile; the GIC is needed by Windows, not
  # by UEFI, and the same m1n1 work is required either way.
  #
  # RE-ENABLED for FW-15, paired with m1n1 M5-DEBUG-20, which addresses all three:
  #   1. HCR_EL2.TID3 is now set (hv.c), plus passthrough for the entire
  #      CRn=0 CRm=1..7 trap space it opens up (hv_exc.c).
  #   2. The ICC_* CPU-interface handlers are uncommented.
  #   3. ICC_SRE_EL1 is emulated and reports SRE=1, as GicV3Supported() requires;
  #      ICC_PMR/CTLR/IGRPEN0 added alongside it.
  #
  # THIS BUILD ONLY WORKS ON M5-DEBUG-20 OR LATER. On M5-DEBUG-19 it reproduces the
  # FW-14 hang, because the guest still reads ID_AA64PFR0_EL1.GIC = 0.
  #
  AIC_BUILD                      = FALSE
  NETWORK_TLS_ENABLE             = TRUE


[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8142
  #*_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS


[PcdsFixedAtBuild.common]
  #
  # Do not reset when the MemoryTypeInformation variable changes.
  #
  # DXE Core measures actual per-type memory usage, writes the
  # MemoryTypeInformation UEFI variable, and resets so the next boot can pre-size
  # its memory bins. That is correct on a platform with persistent variables.
  #
  # We have none -- there is no NVRAM or backing storage behind the variable
  # store yet -- so every boot rediscovers the same "change" and resets again:
  #
  #   Memory Type Information settings change.
  #   ...Warm Reset!!!
  #   DXE ResetSystem2: ResetType Warm, Call Depth = 1.
  #   Warm reboot not supported by platform, issuing cold reboot
  #
  # which m1n1 sees as PSCI SYSTEM_RESET (0x84000009). That is the reset loop
  # that looked like a crash. Revisit once variables are actually persistent.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE

  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Pro (Late 2020)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"MacBookPro17,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Pro (MacBookPro17,1)"
  # J704 (M5) exposes usb-drd0, usb-drd1 and usb-drd3 -- no usb-drd2. m1n1 brings
  # up all three (USB0/USB1/USB3). These PCDs are used as a loop *bound*, so the
  # non-contiguous indexing cannot be expressed exactly; 4 covers 0..3, and the
  # absent usb-drd2 is skipped harmlessly by the node-presence check in
  # AppleDartIoMmuDxe / AppleUsbTypeCBringupDxe. At the previous value of 2,
  # usb-drd3 was unreachable, leaving only one candidate port for boot media.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|4
  # Unread by any code -- declared in AppleSiliconPkg.dec and set here and in the
  # other platform DSCs, but nothing consumes it. Kept consistent for clarity.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Darts|8
  # J704's only keyboard is internal (MTP over dockchannel), and there is no UEFI
  # driver for it yet, so the USB class keyboard path in ConIn can never resolve.
  # Leaving it there makes the console depend on the USB stack, which stalls in
  # UsbRootHubInit at ReadyToBoot before BDS attempts any boot option -- see
  # docs/NEXT-STEPS.md attempt 30. XhciDxe stays registered either way.
  # Set back to TRUE once USB enumeration completes, or once an external USB
  # keyboard is wanted for the menu.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectUsbKeyboardConsole|FALSE
  # THE actual cause of the UsbRootHubInit stall (attempt 31). PostReadyToBoot()
  # calls EfiBootManagerConnectAll() because the boot target is the internal
  # Shell; that binds XhciDxe and hangs before the Shell image ever starts.
  # FALSE gets a Shell prompt over the VUART, from which `connect -r` reproduces
  # the stall interactively. Set back to TRUE once USB enumeration completes.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleConnectAllForInternalShell|FALSE
  # Turn on BootRamdiskHelperDxe. It no-ops unless a RAW section with
  # gAppleSiliconPkgEmbeddedRamdiskGuid is actually in the FV, so this is safe to
  # leave on -- but the FDF currently embeds a 16 MiB FAT16 *test* image, not
  # WinPE. See docs/NEXT-STEPS.md attempt 39.
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  #will be changed later on, default values
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|2560
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|30

[PcdsDynamicDefault.common]
  # #borrowed from SurfaceDuoPkg
  # gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1920
  # gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1080
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|1920
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|1080
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|300
  # gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|50
  # gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|300
  # gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|50

[Components.common]

MacBookProLate2025Pkg/AcpiTables/DeviceAcpiTables.inf

!include MacBookProFamilyPkg/MacBookProFamilyPkg.dsc.inc
!include T8142FamilyPkg/T8142FamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc

#
# Built only so its .efi can be copied into the RAM disk image as
# EFI\BOOT\BOOTAA64.EFI -- see tools/make_ramdisk_img.sh. It is deliberately NOT
# added to the FDF, so it never ends up in the firmware volume.
#
# Purpose: roadmap Phase 1.3, "load a PE from FS1". Mounting the RAM disk and
# reading RAMDISK.TXT (done, FW-10) only proves DiskIo/Partition/Fat work.
# Executing an image off it additionally proves the Shell can load and relocate a
# PE from that volume, which is exactly what bootaa64.efi will need.
#
# HelloWorld rather than Shell.efi on purpose: launching a nested Shell would
# re-run FS0:\startup.nsh and recurse. HelloWorld prints one line and exits.
#
MdeModulePkg/Application/HelloWorld/HelloWorld.inf
