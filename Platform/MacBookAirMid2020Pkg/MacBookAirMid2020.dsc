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
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishBlockIo|TRUE
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPerformDxeBringUp|TRUE
  # Left FALSE on purpose: warm handoff is only for a build that boots Windows
  # from internal storage. BDS enumeration does not need it.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPreserveForOs|FALSE

  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Air (Mid 2020)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"MacBookAir10,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Air (MacBookAir10,1)"
  

[Components.common]

  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTables.inf

!include T810XFamilyPkg/T810XFamilyPkg.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc