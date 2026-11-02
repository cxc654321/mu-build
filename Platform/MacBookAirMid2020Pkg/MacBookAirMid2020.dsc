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
  # Memory type information, in pages, as EFI_MEMORY_TYPE_INFORMATION entries
  # terminated by EfiMaxMemoryType. Order is by type index:
  #
  #   00 EfiReservedMemoryType          0
  #   05 EfiRuntimeServicesCode       780   (0x30C)
  #   06 EfiRuntimeServicesData       160   (0xA0)
  #   09 EfiACPIReclaimMemory           5
  #   0A EfiACPIMemoryNVS               0
  #   0E EfiMaxMemoryType               0  <- terminator
  #
  # These are the "Next" column BDS printed for this machine. The comparison
  # table read prev 0 / 0x12C / 0x96 for 09 / 06 / 05 and wanted 5 / 0xA0 /
  # 0x30C, so every boot decided the settings had changed and asked for a warm
  # reset -- which this platform cannot perform (no PSCI monitor), which is what
  # killed the boot inside ResetSystemRuntimeDxe.
  #
  # It cannot converge on its own: PcdEmuVariableNvModeEnable is TRUE here, so
  # gEfiMemoryTypeInformationGuid never survives a reboot and the comparison
  # always starts from the PCD again. Seeding the PCD with the values BDS would
  # compute breaks the loop at the first comparison instead of the second boot.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdMemoryTypeInformation|{
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,   # 00 Reserved,            0 pages
    0x05, 0x00, 0x00, 0x00,  0x0C, 0x03, 0x00, 0x00,   # 05 RuntimeServicesCode, 780 pages
    0x06, 0x00, 0x00, 0x00,  0xA0, 0x00, 0x00, 0x00,   # 06 RuntimeServicesData, 160 pages
    0x09, 0x00, 0x00, 0x00,  0x05, 0x00, 0x00, 0x00,   # 09 ACPIReclaimMemory,    5 pages
    0x0A, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,   # 0A ACPIMemoryNVS,        0 pages
    0x0E, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00    # 0E EfiMaxMemoryType, terminator
  }
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
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPerformDxeBringUp|FALSE
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