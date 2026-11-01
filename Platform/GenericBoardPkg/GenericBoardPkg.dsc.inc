#
# Board values for a machine whose board has not been measured.
#
# Every PCD here is a number that can only come from reading a live machine.
# They are set to zero, which the consumers read as "not pinned" rather than as
# an address:
#
#   * AcpiPlatformDxe resolves the ANS power domains from the ADT by name and
#     uses the PCDs only to cross-check the result. Zero means there is nothing
#     to cross-check against, so it does not warn.
#   * A published/physical interrupt pair of zero means no ANS ACPI device is
#     described. That is consistent with these machines, which have the ANS
#     features off -- the internal SSD still works, because
#     AppleNANDStorageDxe takes every address from the live ADT.
#   * Zero DWC3 controllers means no USB host controller is published.
#
# Copying another machine's values would have been worse than leaving them out.
# Before this file existed, every generated machine carried J414s' numbers: an
# M3 Pro machine's .dsc named T602X's pmgr block at 0x2902801A8 and J414s'
# measured AIC line 1832, neither of which is an address on its silicon.
#
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

[PcdsFixedAtBuild.common]
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Darts|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUsb3PipeSwitchPortMask|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUsb4RoutedPipeSwitchPortMask|0

  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishedInterrupt|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsExpectedPhysicalInterrupt|0

  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrResetBase|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStBase|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStSysBase|0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieSt1SysBase|0
