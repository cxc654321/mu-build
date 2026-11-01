## @file
#  The FvMain module list every Apple silicon machine shares.
#
#  Fifty-three INF lines were identical, in identical order, in all seven
#  platform .fdf files. The rest differ by at most eleven lines each, and every
#  one of those differences is a yes/no about a driver or which package supplies
#  an ACPI table -- so they are DEFINEs here rather than seven copies.
#
#  A machine sets its DEFINEs and includes this. See Docs/PLATFORMS.md.
#
#  Copyright (c) Aurora Silicon.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
!if $(PLATFORM_ENABLE_DART) == TRUE
  INF AppleSiliconPkg/Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.inf
!endif
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
!if $(SECURE_BOOT_ENABLE) == TRUE
  INF SecurityPkg/VariableAuthenticated/SecureBootConfigDxe/SecureBootConfigDxe.inf
!endif
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
!if $(PLATFORM_ENABLE_SERIAL_DXE) == TRUE
  INF MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
!endif
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
# One interrupt controller or the other, never both.
!if $(AIC_BUILD) == FALSE
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
!else
  INF AppleSiliconPkg/Drivers/AppleAicDxe/AppleAicDxe.inf
!endif
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF AppleSiliconPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!if $(PLATFORM_ENABLE_PCI) == TRUE
  INF ArmPkg/Drivers/ArmPciCpuIo2Dxe/ArmPciCpuIo2Dxe.inf
!endif
!if $(PLATFORM_ENABLE_PCI) == TRUE
  INF MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridgeDxe.inf
!endif
!if $(PLATFORM_ENABLE_PCI) == TRUE
  INF MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf
!endif
  INF MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
!if $(PLATFORM_NAND_SLOT) == EARLY
  INF AppleSiliconPkg/Drivers/AppleNANDStorageDxe/AppleNANDStorageDxe.inf
!endif
  INF EmbeddedPkg/Drivers/VirtualKeyboardDxe/VirtualKeyboardDxe.inf
  INF MdeModulePkg/Universal/FvSimpleFileSystemDxe/FvSimpleFileSystemDxe.inf
!if $(PLATFORM_ENABLE_GPIO) == TRUE
  INF AppleSiliconPkg/Drivers/AppleEmbeddedGpioControllerDxe/AppleEmbeddedGpioControllerDxe.inf
!endif
!if $(PLATFORM_ENABLE_APPLE_PCI) == TRUE
  INF AppleSiliconPkg/Drivers/AppleSiliconPciPlatformDxe/AppleSiliconPciPlatformDxe.inf
!endif
!if $(PLATFORM_ENABLE_TYPEC) == TRUE
  INF AppleSiliconPkg/Drivers/AppleUsbTypeCBringupDxe/AppleUsbTypeCBringupDxe.inf
!endif
!if $(PLATFORM_ENABLE_MTP_HID) == TRUE
  INF AppleSiliconPkg/Drivers/AppleMtpHidDxe/AppleMtpHidDxe.inf
!endif
!if $(PLATFORM_ENABLE_MTP_HID) == BUILD_FLAG
!if $(MTP_HID_BUILD) == TRUE
  INF AppleSiliconPkg/Drivers/AppleMtpHidDxe/AppleMtpHidDxe.inf
!endif
!endif
  INF  MdeModulePkg/Bus/Pci/UhciDxe/UhciDxe.inf
  INF  MdeModulePkg/Bus/Pci/EhciDxe/EhciDxe.inf
  INF  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  INF  MsCorePkg/Universal/StatusCodeHandler/Serial/Dxe/SerialStatusCodeHandlerDxe.inf
  INF  MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf
  INF AppleSiliconPkg/Drivers/BootRamdiskHelperDxe/BootRamdiskHelperDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
  INF AppleSiliconPkg/Drivers/AcpiPlatformDxe/AcpiPlatformDxe.inf
  INF MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
!if $(PLATFORM_ENABLE_GENERIC_ACPI) == TRUE
  INF RuleOverride=ACPITABLE AppleSiliconPkg/AcpiTables/GenericAcpiTables.inf
!endif
  #
  # ACPI tables, one tier at a time: the board's, the chassis family's, the
  # SoC's.
  #
  # PLATFORM_BOARD_PKG is the board the machine's ACPI was measured on, which is
  # not always the machine itself -- eight T602X machines share J414sBoardPkg
  # because they share its board, and a machine whose board nobody has measured
  # uses GenericBoardPkg, which declares only what the SoC family package's PCDs
  # already state. It was previously PLATFORM_PKG, which forced every machine to
  # carry its own copy of tables it had not measured.
  #
  INF RuleOverride=ACPITABLE $(PLATFORM_BOARD_PKG)/AcpiTables/DeviceAcpiTables.inf
!if $(PLATFORM_FAMILY_PKG) != NONE
  INF RuleOverride=ACPITABLE $(PLATFORM_FAMILY_PKG)/AcpiTables/DeviceFamilyAcpiTables.inf
!endif
!if $(PLATFORM_SOC_PKG) != NONE
  INF RuleOverride=ACPITABLE $(PLATFORM_SOC_PKG)/AcpiTables/CPUAcpiTables.inf
!endif
  INF AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.inf
  INF MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
!if $(PLATFORM_NAND_SLOT) == LATE
#
# Firmware-owned internal storage: attach to iBoot's live ANS/RTKit state,
# expose read-only Block I/O, and preserve the warm handoff for Windows. Only
# the machines that boot from internal storage put it here, and only they gate
# it on the profile -- the four older platforms embed it unconditionally, early,
# which is why PLATFORM_NAND_SLOT has two positions rather than one.
#
!if $(NTASI_ENABLE_ANS) == TRUE
  INF AppleSiliconPkg/Drivers/AppleNANDStorageDxe/AppleNANDStorageDxe.inf
!endif
!endif
  INF ShellPkg/Application/Shell/Shell.inf
!ifdef $(INCLUDE_TFTP_COMMAND)
  INF ShellPkg/DynamicCommand/TftpDynamicCommand/TftpDynamicCommand.inf
!endif
!if $(PLATFORM_ENABLE_COLORBARS) == TRUE
  INF AppleSiliconPkg/Drivers/ColorbarsDxe/ColorbarsDxe.inf
!endif
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  INF MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  INF MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

