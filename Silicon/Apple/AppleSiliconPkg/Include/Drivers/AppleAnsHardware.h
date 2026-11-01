/** @file
  Shared Apple ANS hardware-resource bounds.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#ifndef APPLE_ANS_HARDWARE_H_
#define APPLE_ANS_HARDWARE_H_

#include <Uefi.h>

#define APPLE_ANS_CPU_MIN_SIZE         0x8840u
#define APPLE_ANS_NVME_T8015_MIN_SIZE  0x1304u
#define APPLE_ANS_NVME_MIN_SIZE        0x28124u
/* Complete ADT reg[9] standard/secure controller aperture. */
#define APPLE_ANS_NVME_SECURE_MIN_SIZE 0x10000u
#define APPLE_ANS_SART_V0_MIN_SIZE     0x80u
#define APPLE_ANS_SART_V2_MIN_SIZE     0x80u
#define APPLE_ANS_SART_V3_MIN_SIZE     0xc0u

/**
  Return TRUE only when [Base, Base + Size) is representable by UINTN and
  contains every register required by the selected hardware build.
**/
STATIC inline
BOOLEAN
AppleAnsMmioRangeValid (
  IN UINT64  Base,
  IN UINT64  Size,
  IN UINT64  MinimumSize
  )
{
  if ((MinimumSize == 0) ||
      (Size < MinimumSize) ||
      (Base > (UINT64)MAX_UINTN) ||
      (Size > (UINT64)MAX_UINTN))
  {
    return FALSE;
  }

  return Base <= ((UINT64)MAX_UINTN - (Size - 1));
}

#endif
