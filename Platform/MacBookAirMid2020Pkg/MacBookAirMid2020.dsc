#MacBook Air Mid 2020 UEFI DSC file
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

[Defines]
  PLATFORM_NAME                  = MacBookAirMid2020
  #
  # Whether this SoC's family package found a PCIe root complex in
  # Asahi's tree. The board DSDT declares its root bridge only when
  # there is one to describe.
  #
  DEFINE NTASI_SOC_HAS_PCIE = 1

  PLATFORM_GUID                  = 482c40bb-f36c-4c25-bc04-2c05e75542ef
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacBookAirMid2020-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacBookAirMid2020Pkg/MacBookAirMid2020.fdf
  SECURE_BOOT_ENABLE             = FALSE
  AIC_BUILD                      = TRUE #AIC build enabled by default, change to false if you want to use a vGIC
  USES_MAC_CPU                   = TRUE # a futureproofing switch, changes SoC identifier in SMBIOS
  NETWORK_TLS_ENABLE             = TRUE

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8103
  *_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS
  # XmlSupportPkg's vendored fasterxml carries #pragma optimize("", off) to
  # stop MSVC turning a loop into a memset. Clang ignores that pragma and
  # reports it; with -Werror the whole XmlTreeLib module fails to compile.
  *_CLANGPDB_AARCH64_CC_FLAGS = -Wno-ignored-pragma-optimize

[PcdsFixedAtBuild.common]
  #
  # Do not ask for a warm reset when the memory type information changes.
  #
  # MdeModulePkg/Library/UefiBootManagerLib/BmMisc.c prints
  # "Memory Type Information settings change." and, when this is TRUE, follows it
  # with "...Warm Reset!!!" and gRT->ResetSystem (EfiResetWarm). On this machine
  # that request either kills the boot (ArmPsciResetSystemLib issued an SMC with
  # no PSCI monitor: EC 0x00 unknown instruction) or, with our halting reset
  # library, stops it -- and it can never stop being requested, because
  # PcdEmuVariableNvModeEnable is TRUE, so the memory type information variable
  # is rebuilt from scratch every boot and the comparison always differs.
  #
  # FALSE is the upstream-supported way to say "record it, do not reboot".
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  #
  # Internal storage, which BDS can then enumerate.
  #
  # AppleNANDStorageDxe is already in the FV on this platform
  # (PLATFORM_NAND_SLOT = EARLY) and its depex is TRUE, but both switches in
  # front of it defaulted to FALSE: it ran discovery, printed its PMGR/SART
  # report, and deliberately published no Block I/O, so the boot manager had
  # nothing to look at.
  #
  # The 'storage' shorthand in Platform/Features.py does not reach this
  # platform -- BLD_*_NTASI_ANS_* are only mapped by the T6020-era DSCs. These
  # are the PCDs the driver actually reads.
  #
  #
  # Back to FALSE for the crash bisect.
  #
  # With the ACPI ASSERTs removed the boot finally reaches code past
  # AcpiPlatformDxe, and takes a synchronous exception at 0x9DDD76208. ANS
  # bring-up is the most invasive thing that code does -- it starts a
  # coprocessor, retargets SART and touches the NVMe registers -- so it is the
  # first variable to take back out. The three switches are the driver's own
  # enable bits; leaving them FALSE restores "discovery and report only", which
  # is what every earlier round ran.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishBlockIo|FALSE
  #
  # Step one of two for internal storage: let the driver actually run its
  # ANS/RTKit bring-up, but still withhold Block I/O. Nothing can file-system
  # mount yet -- that needs PcdAppleAnsPublishBlockIo -- so a boot that gets
  # further here is a bring-up that did not fault, which is the thing worth
  # learning before publishing anything to BDS.
  #
  # History: this was turned off on 2026-07-30 after a Windows BUGCODE_USB3_DRIVER
  # 0x144 that only went away with ANS bring-up disabled. That was a different
  # tree, an OS-side handoff, and six boots of evidence; here it is about
  # reaching the boot manager at all.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPerformDxeBringUp|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPreserveForOs|FALSE

  #
  # FALSE. The panel does not mirror; AppleFbDebugLib was the one drawing
  # backwards.
  #
  # The evidence is the exception dump being half readable and half not: its
  # header line ("Synchronous Exception at 0x...") is written through
  # ConSplitter -> GraphicsConsoleDxe -> GOP -> SimpleFbDxe and came out
  # readable, while the register dump below it -- same handler,
  # ArmPkg/DefaultExceptionHandlerLib -- goes out through DEBUG() and came out
  # reversed. Same screen, two renderers, opposite result: the compensation in
  # this library is what put it backwards.
  #
  # So ConOut is the reference and DebugLib has to match it, not the other way
  # round. No GOP-side change is needed.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleFbDebugMirrorX|FALSE

  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Air (Mid 2020)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"MacBookAir10,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Air (MacBookAir10,1)"
  

[Components.common]

  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTables.inf

  # Reads out DXE's memory map and PrePi's resource HOBs on the panel, before
  # anything else has consumed memory. Bring-up diagnostic; depex TRUE.
  AppleSiliconPkg/Drivers/J313MemMapDxe/J313MemMapDxe.inf

!include T810XFamilyPkg/T810XFamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc