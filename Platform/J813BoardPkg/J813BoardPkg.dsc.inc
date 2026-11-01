#
# Board values measured on J813. See ../J414sBoardPkg/J414sBoardPkg.dsc.inc for
# what these are and why they live with the board.
#
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

[PcdsFixedAtBuild.common]
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|4
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Darts|8

  #
  # 1155 is /arm-io/ans interrupts[4]; 996 is the published GSIV standing in for
  # it. J414s publishes 38 for the same purpose -- that number is not portable
  # here, because 38 is a live physical line on this SoC.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishedInterrupt|996
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsExpectedPhysicalInterrupt|1155

  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrResetBase|0x380700300
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStBase|0x380700410
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStSysBase|0x380700520
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieSt1SysBase|0
