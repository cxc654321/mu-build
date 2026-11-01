# Copyright (c) 2026 Aurora Silicon

[PcdsFixedAtBuild.common]
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemFamily|"MacBook Air"
  #will be changed later on, default values
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferWidth|2560
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
  gAppleSiliconPkgTokenSpaceGuid.PcdFrameBufferPixelBpp|30


[PcdsPatchableInModule.common]



[PcdsDynamicDefault.common]
  #
  # Screen settings. Zero means "use what the panel actually reports" and is
  # the only correct value for a family spanning several panels: SimpleFbDxe
  # publishes exactly one GOP mode, taken from the boot_args framebuffer, so a
  # hardcoded resolution is right for at most one machine. J813 is 2560x1664,
  # the Mid2020 Air is 2560x1600 -- the old 2560x1600 here only ever reached
  # the right mode on J813 by way of GraphicsConsoleDxe's error path.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|0
  #
  # Likewise zero rows/columns, which makes GraphicsConsoleDxe select the
  # largest text mode it built -- the full-screen one, 320x87 on J813.
  #
  # 150x150 was unreachable: rows are glyph cells, not pixels, so 150 rows
  # needs 150*EFI_GLYPH_HEIGHT = 2850 pixels of height on a 1664-pixel panel.
  # No mode matched, PreferMode stayed -1, and the console collapsed to the
  # 80x25 fallback -- 640x475 pixels in the top-left corner of the display,
  # which is the "tiny UEFI window" symptom.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0

[Components.common]
  MacBookAirFamilyPkg/AcpiTables/DeviceFamilyAcpiTables.inf