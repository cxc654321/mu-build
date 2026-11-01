# Copyright (c) 2026 Aurora Silicon

[PcdsFixedAtBuild.common]
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemFamily|"Mac Pro"
  #will be changed later on, default values
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|1920
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|1080
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|30


[PcdsPatchableInModule.common]



[PcdsDynamicDefault.common]
  #screen resolution settings
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1920
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|1080
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|1902
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|1080
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|150
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|150
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|150
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|150

[Components.common]
  MacProFamilyPkg/AcpiTables/DeviceFamilyAcpiTables.inf