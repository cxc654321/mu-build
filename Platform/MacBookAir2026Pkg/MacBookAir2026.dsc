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
  PLATFORM_NAME                  = MacBookAir2026
  #
  # Whether this SoC's family package found a PCIe root complex in
  # Asahi's tree. The board DSDT declares its root bridge only when
  # there is one to describe.
  #
  DEFINE NTASI_SOC_HAS_PCIE = 1

  PLATFORM_GUID                  = 35B1C834-FE1C-4656-BBDB-E5C6B6B3D48B
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacBookAir2026-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacBookAir2026Pkg/MacBookAir2026.fdf
  SECURE_BOOT_ENABLE             = FALSE #disable secure boot for now
  DEFINE T8142_SYSTEM_MEMORY_SIZE = 0x400000000
  #
  # Internal Apple ANS/NVMe is opt-in.  A build without the storage
  # features stays free of storage bring-up; enabling them sets these through
  # BLD_* variables in Tools/build-j813-windows-native.sh.
  #
  DEFINE NTASI_ENABLE_ANS = FALSE
  DEFINE NTASI_ANS_DXE_BRINGUP = FALSE
  DEFINE NTASI_ANS_PUBLISH_BLOCK_IO = FALSE
  DEFINE NTASI_ANS_PRESERVE_FOR_OS = FALSE
  # Enabling the storage features disables the interactive MTP survey. Its
  # eight-second delay loop depends on the guest counter path and can dominate
  # or stall supervised T8142 boots before ANS is dispatched.
  DEFINE MTP_HID_BUILD = TRUE
  # J813 is currently operated without a UEFI keyboard, and the guest timer
  # path can stall inside BDS's cosmetic countdown. Boot the selected
  # diagnostic target immediately.
  DEFINE NTASI_PLATFORM_BOOT_TIMEOUT = 0
  # Keep boot diagnostics while suppressing DEBUG_BLKIO.  WinPE performs many
  # small DiskIo reads and logging each subtask over UART makes RAM booting
  # needlessly slow on J813.
  DEFINE NTASI_DEBUG_PRINT_ERROR_LEVEL = 0xFFFFEF4F
  # J813 currently boots Mu with Apple's native AIC and has no Windows AIC2
  # CSRT alias table. Keep NTAS2003 withheld until the 1155 -> legal-GSIV
  # carrier contract exists; UEFI Block I/O does not need the ACPI device.
  DEFINE NTASI_ANS_PUBLISH_ACPI = FALSE
  #
  # Start J813 bring-up on Apple's native AIC: this is the path already proven
  # to reach the internal UEFI shell. A later Windows configuration will switch this
  # to FALSE once m1n1's emulated GICv3 path is stable on T8142.
  #
  AIC_BUILD                      = TRUE
  NETWORK_TLS_ENABLE             = TRUE


[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8142 -DNTASI_J813_EL2_SYSREG_ASSIST=1
  GCC:*_*_AARCH64_PP_FLAGS = -DNTASI_J813_EL2_SYSREG_ASSIST=1
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

  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Air (M5, 2026)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"Mac17,3"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Air (Mac17,3)"
  # J813 (M5) exposes usb-drd0, usb-drd1 and usb-drd3 -- no usb-drd2. m1n1 brings
  # up all three (USB0/USB1/USB3). These PCDs are used as a loop *bound*, so the
  # non-contiguous indexing cannot be expressed exactly; 4 covers 0..3, and the
  # absent usb-drd2 is skipped harmlessly by the node-presence check in
  # AppleDartIoMmuDxe / AppleUsbTypeCBringupDxe. At the previous value of 2,
  # usb-drd3 was unreachable, leaving only one candidate port for boot media.
  # Unread by any code -- declared in AppleSiliconPkg.dec and set here and in the
  # other platform DSCs, but nothing consumes it. Kept consistent for clarity.
  # J813 / T8142 values resolved from the live DeviceTree.j813ap ADT in the
  # 26.6.1 (25G76) IPSW. AppleNANDStorageDxe resolves the domain by exact name
  # again at runtime and treats these values only as a write-safety cross-check.
  # T8142 calls the controller domain "ANS" (not "ANS2") and has no
  # APCIE_ST1_SYS domain, so that unused expectation is deliberately zero.
  # The physical ANS NVMe line is ADT interrupt[4] == 1155. It cannot be
  # published directly as a Windows GSIV: 1155 lies in GIC's reserved
  # 1024..4095 range, and a devnode that names it comes up problem=12
  # (CM_PROB_NO_VALID_LOG_CONFIG) with its driver never loaded.
  #
  # J813 has the alias contract this used to wait for, but it is not ALI2:
  # there is no built CSRT here and HalExtAppleInterruptController never loads,
  # so the guest stays on m1n1's emulated GICv3 carrier for its whole life and
  # m1n1 is the only thing translating between AIC lines and guest INTIDs.
  # The table is hv_aic_aliases_t8142 in m1n1's src/hv_aic_alias.c, and these
  # two values must stay equal to the {published, physical} pair it holds for
  # ans -- Mu publishes the GSIV, m1n1 performs the renumbering, and neither
  # can discover the other's choice at runtime.
  #
  # 996 was measured free, not assumed: 443 distinct lines appear in ADT
  # `interrupts` properties on this machine and none of them is 996. Publishing
  # a number that is also a live line would deliver another device's interrupts
  # under ANS0's INTID, because injection is the identity on an alias miss.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishAcpiDevice|$(NTASI_ANS_PUBLISH_ACPI)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPerformDxeBringUp|$(NTASI_ANS_DXE_BRINGUP)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishBlockIo|$(NTASI_ANS_PUBLISH_BLOCK_IO)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPreserveForOs|$(NTASI_ANS_PRESERVE_FOR_OS)
  # J813's only keyboard is internal (MTP over dockchannel), and there is no UEFI
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
  # The installed resident m1n1 remains the recovery-safe proxy, but the J813
  # launch path RAM-chainloads the current hypervisor before entering Mu.  That
  # hypervisor reserves uart0 and maps it to the secondary USB CDC endpoint, so
  # DebugLib and SerialDxe can use the normal Apple UART aperture safely.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUartMmioEnabled|TRUE
  # Turn on BootRamdiskHelperDxe. It no-ops unless a RAW section with
  # gAppleSiliconPkgEmbeddedRamdiskGuid is actually in the FV, so this is safe to
  # leave on -- but the FDF currently embeds a 16 MiB FAT16 *test* image, not
  # WinPE. See docs/NEXT-STEPS.md attempt 39.
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  #will be changed later on, default values
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|2560
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
  # gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|30

[Components.common]

J813BoardPkg/AcpiTables/DeviceAcpiTables.inf

!include J813BoardPkg/J813BoardPkg.dsc.inc
!include MacBookAirFamilyPkg/MacBookAirFamily.dsc.inc
!include T8142FamilyPkg/T8142FamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc

#
# Deliberately after the includes: the last assignment to a PCD wins, and
# MacBookAirFamily.dsc.inc sets these to 0. A block placed above !include is
# silently overridden by the family default, which is exactly how the first
# attempt at this failed.
#
# J813's panel is 2560 x 1664. The family default of 0 means "use the highest
# mode the GOP offers", which lands on that native mode, where an EFI_GLYPH is
# 8 x 19 physical pixels -- legible on a 1080p monitor, not on this one.
#
# SimpleFbDxe publishes integer-downscaled modes that replicate each logical
# pixel into an N x N block, so selecting the 1/2 mode here doubles every glyph
# to 16 x 38 physical pixels while still covering the whole display (160
# columns x 43 rows). Panel geometry is per-machine, which is why this override
# lives in the J813 platform and not in the shared family include.
#
# NOTE: this is the mode the OS inherits at ExitBootServices. If a Windows
# display regression is ever bisected to console scaling, set these four back
# to 0 to hand Windows the native scanout again.
#
# T8142 architectural PMUv3 is trapped and virtualized by m1n1 at EL2.  Keep
# the J813 firmware and Microsoft PE images free of runtime instruction patches.

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
AppleSiliconPkg/Application/StorageProbe/StorageProbe.inf

#
# Deliberately after the !includes: the last assignment wins, and the family
# include sets its own value. A block placed above them is silently overridden.
#
# J813's panel is 2560 x 1664 and the UEFI console draws glyphs at a fixed
# 8 x 19 physical pixels, which is unreadable here. Halving the published
# geometry gives a 1280 x 832 GOP whose pixels SimpleFbDxe replicates 2x2 into
# the real scanout, so every glyph becomes 16 x 38 physical pixels and the
# console still covers the whole display (160 columns x 43 rows).
#
# The four PcdVideo*Resolution values stay 0 ("use the largest mode the GOP
# offers") from MacBookAirFamily.dsc.inc: with the divisor applied, the largest
# mode already IS 1280 x 832, so nothing needs to name a resolution.
#
# NOTE: this is the geometry the OS inherits at ExitBootServices, and
# BasicDisplay has no other source of it on this platform. Set the divisor back
# to 1 to hand Windows the full 2560 x 1664 scanout again.
#
# DISABLED (set to 1) until scaling is safe for direct-framebuffer consumers.
#
# A divisor of 2 does give a readable console -- measured 1280x832, Mode 4,
# glyphs at 16x38 physical pixels covering the panel. But it publishes
# PixelsPerScanLine=1280 against a scanout that is still 2560 pixels wide with a
# 2560-pixel stride, and that is only coherent for callers that go through
# GOP Blt(). bootmgfw.efi and BasicDisplay do not: they take FrameBufferBase and
# PixelsPerScanLine and write the linear framebuffer directly, so they render
# with the wrong stride. Observed as a blank screen after bootmgfw.efi loaded.
#
# Fixing this properly means keeping the raw panel as the largest GOP mode (so
# GCM_NATIVE_RES hands the OS a coherent framebuffer) while getting the console
# onto a scaled mode some other way than being the maximum -- the mode-ordering
# route in SimpleFbDxe cannot satisfy both at once.
#
[PcdsFixedAtBuild.common]
  gAppleSiliconPkgTokenSpaceGuid.PcdConsoleScaleDivisor|1
