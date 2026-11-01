/** @file
  Runtime location of the firmware image, published by PrePi for later phases.

  PcdFdBaseAddress and PcdFdSize are declared in [PcdsPatchableInModule], so each
  module links its own copy of the value and only PrePi patches its copy to the
  address m1n1 actually loaded the firmware at. Any other module -- notably a DXE
  driver -- reads the FDF's build-time defaults instead:

      BaseAddress = 0x830000000 | gArmTokenSpaceGuid.PcdFdBaseAddress
      Size        = 0x00001E00000 | gArmTokenSpaceGuid.PcdFdSize

  which on this platform is inside the MMIO window, not DRAM. Dereferencing an
  address derived from them takes an SError and kills the boot.

  PrePi therefore publishes the real values in this HOB. Consumers must use the
  HOB, never the PCDs.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef APPLE_FD_INFO_HOB_H_
#define APPLE_FD_INFO_HOB_H_

extern EFI_GUID  gAppleSiliconPkgFdInfoHobGuid;

typedef struct {
  ///
  /// Address m1n1 actually loaded the firmware image at, i.e. the value PrePi
  /// prints as "FD Base Address".
  ///
  UINT64    FdBase;
  ///
  /// Declared size of the firmware image region (PcdFdSize as patched in PrePi).
  /// Anything appended to the payload by m1n1 begins at FdBase + FdSize, provided
  /// the .fd was padded to this size first -- see tools/make_appended_payload.sh.
  ///
  UINT64    FdSize;
} APPLE_FD_INFO_HOB;

#endif // APPLE_FD_INFO_HOB_H_
