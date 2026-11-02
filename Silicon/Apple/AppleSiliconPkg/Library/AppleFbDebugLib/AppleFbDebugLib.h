/** @file
  AppleFbDebugLib: DEBUG() output rendered straight onto the framebuffer.

  J313 has no accessible UART, so BaseDebugLibSerialPort writes the DXE
  dispatch log, BDS progress and every driver's own DEBUG() into nothing. The
  framebuffer is the only output device this machine gives us, and SimpleFbDxe
  already establishes that it works (ColorbarsDxe paints bars on it). This
  library puts the log there instead, so dispatch order and the point of a hang
  are visible on the panel rather than inferred.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef APPLE_FB_DEBUG_LIB_H_
#define APPLE_FB_DEBUG_LIB_H_

#include <Uefi.h>

///
/// EFI_NARROW_GLYPH layout from MdePkg's UefiInternalFormRepresentation.h,
/// repeated here so this library does not have to pull in the whole HII
/// description machinery it never uses.
///
typedef struct {
  CHAR16    UnicodeWeight;
  UINT8     Attributes;
  UINT8     GlyphCol1[19];
} APPLE_FB_GLYPH;

extern APPLE_FB_GLYPH  mAppleFbGlyphs[];

#endif
