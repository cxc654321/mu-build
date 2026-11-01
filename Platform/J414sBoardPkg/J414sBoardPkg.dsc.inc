#
# Board values measured on J414s.
#
# These are not identity and they are not silicon. They are numbers read off a
# live machine's ADT and correlated with the alias table the AIC HAL extension
# uses, which is exactly what makes them board facts: they belong beside the
# board's ACPI, not copied into each machine's .dsc where the eight machines
# sharing this board each kept their own copy of them.
#
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

[PcdsFixedAtBuild.common]
  #
  # USB. Three DWC3 controllers behind six DARTs. Read off J414s; the other
  # machines on this board have the same complement.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|3
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Darts|6
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUsb3PipeSwitchPortMask|$(NTASI_USB3_PIPE_SWITCH_PORT_MASK)
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleUsb4RoutedPipeSwitchPortMask|$(NTASI_USB4_ROUTED_PIPE_SWITCH_PORT_MASK)

  #
  # Internal NVMe. 1832 is /arm-io/ans interrupts[4]; 38 is the low GSIV
  # published in its place, because 1832 is above the GIC carrier's limit and
  # Windows' PnP arbiter refuses it. The CSRT's ALI2 tail translates between
  # them, so the pair has to agree with the CSRT and with m1n1's alias table.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPublishedInterrupt|38
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsExpectedPhysicalInterrupt|1832

  #
  # ANS power domains. AcpiPlatformDxe resolves these from the ADT by name and
  # uses these only to cross-check the result, warning on disagreement -- the
  # 2026-07-30 finding was that hardcoding them resolved to DCS_09/DCS_10, two
  # DRAM controller domains, because they had been computed against the wrong
  # pmgr register block.
  #
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrResetBase|0x2902801A8
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStBase|0x2902801A0
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieStSysBase|0x290280408
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleAnsPmgrApcieSt1SysBase|0x290280410
