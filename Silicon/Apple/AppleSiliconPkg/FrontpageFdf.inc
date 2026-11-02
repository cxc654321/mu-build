# Continuation of [FV.FvMain]
  #INF MsGraphicsPkg/GopOverrideDxe/GopOverrideDxe.inf
  INF DfciPkg/AuthManagerNull/AuthManagerNull.inf #change when non-volatile storage is ready
  INF DfciPkg/SettingsManager/SettingsManagerDxe.inf
  INF MsGraphicsPkg/MsUiTheme/Dxe/MsUiThemeProtocol.inf
  # Provides gMsSREProtocolGuid (and its own GOP) for SimpleWindowManagerDxe.
  INF MsGraphicsPkg/RenderingEngineDxe/RenderingEngineDxe.inf
  INF MsGraphicsPkg/DisplayEngineDxe/DisplayEngineDxe.inf
  #
  # The window manager has to be dispatched before the UI that looks for it.
  # FV order is dispatch order for depex-TRUE drivers, and both FrontPage and
  # BootMenu locate gMsSWMProtocol exactly once, in their entry point:
  #
  #   ERROR [FP]: Failed to find the Window Manager Protocol (Unsupported)
  #   ERROR: Image at 009DC989000 Start Failed: Unsupported
  #   DeviceBootManagerUnableToBoot: Returned from Boot Manager Menu
  #
  # SimpleWindowManagerDxe carries no [Depex], so nothing orders it ahead of
  # FrontPage except this list. The on-screen keyboard moves up with it: it is
  # the input device FrontPage asks for next (no built-in HID on this machine).
  #
  INF MsGraphicsPkg/SimpleWindowManagerDxe/SimpleWindowManagerDxe.inf
  INF MsGraphicsPkg/OnScreenKeyboardDxe/OnScreenKeyboardDxe.inf
  INF OemPkg/BootMenu/BootMenu.inf
  INF OemPkg/FrontPage/FrontPage.inf
  INF PcBdsPkg/MsBootPolicy/MsBootPolicy.inf
  INF MdeModulePkg/Universal/BootManagerPolicyDxe/BootManagerPolicyDxe.inf
  INF MdeModulePkg/Universal/RegularExpressionDxe/RegularExpressionDxe.inf
  INF DfciPkg/DfciManager/DfciManager.inf
  
  INF MsWheaPkg/HwErrBert/HwErrBert.inf
  INF MsWheaPkg/MsWheaReport/Dxe/MsWheaReportDxe.inf

  # Hardware Health (Menu) application
  INF MsWheaPkg/HwhMenu/HwhMenu.inf

  INF MsCorePkg/AcpiRGRT/AcpiRgrt.inf

  # Change AARCH64 to the appropriate architecture for your platform.
  FILE APPLICATION=PCD(gPcBdsPkgTokenSpaceGuid.PcdShellFile) {
    SECTION PE32=$(OUTPUT_DIRECTORY)/$(TARGET)_$(TOOL_CHAIN_TAG)/AARCH64/Shell.efi
    SECTION UI= "EdkShell"
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdLogoFile) {
    SECTION RAW = AppleSiliconPkg/Include/Resources/BootLogo.bmp
    SECTION UI = "Logo"
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdFrontPageLogoFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/FrontpageLogo.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdLowBatteryFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/LBAT.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdThermalFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/THOT.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdVolumeUpIndicatorFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/VolumeUp.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdFirmwareSettingsIndicatorFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/FirmwareSettings.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdBootFailIndicatorFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/NoBoot.bmp
  }