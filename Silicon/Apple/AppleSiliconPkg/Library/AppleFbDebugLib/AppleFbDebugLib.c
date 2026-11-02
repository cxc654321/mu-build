/** @file
  AppleFbDebugLib: a DebugLib that draws on the framebuffer.

  Everything here runs in contexts where nothing else is guaranteed: DxeCore
  itself, dispatcher passes at TPL_APPLICATION, a driver mid-initialisation.
  So it only ever touches memory it can compute from a fixed PCD -- no
  protocols, no boot services, no allocation, no locks.

  Output is white-on-black, one screen region, and scrolls. Only DEBUG() text
  is drawn; the color pattern underneath is lost on purpose so that the log is
  readable.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugPrintErrorLevelLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

#include "AppleFbDebugLib.h"

//
/// boot_args as m1n1 passes it. Only the leading fields are modelled; the
/// struct is read through PcdBootArgsPointer, a fixed PCD, so it is available
/// before any protocol or allocation exists.
///
typedef struct {
  UINT16    Revision;
  UINT16    Version;
  UINT32    Pad0;
  UINT64    VirtBase;
  UINT64    PhysBase;
  UINT64    MemSize;
  UINT64    TopOfKernelData;
  UINT64    VideoBase;
  UINT64    VideoDisplay;
  UINT64    VideoStride;
  UINT64    VideoWidth;
  UINT64    VideoHeight;
  UINT64    VideoDepth;
} APPLE_BOOT_ARGS;

#define APPLE_FB_GLYPH_W   8
#define APPLE_FB_GLYPH_H   19
#define APPLE_FB_DEFAULT_SCALE  2

//
/// Some panels hand the framebuffer back with the scanout running the other
/// way: the log comes out mirror-imaged. A PCD rather than a guess, because the
/// only way to tell is to read the screen.
///
#define FB_MIRROR_X  (FixedPcdGetBool (PcdAppleFbDebugMirrorX))

STATIC struct {
  volatile UINT32    *Base;
  UINTN              Width;
  UINTN              Height;
  UINTN              StridePx;
  UINTN              Scale;
  UINTN              Cols;
  UINTN              Rows;
  UINTN              CurX;
  UINTN              CurY;
  BOOLEAN            Ready;
  BOOLEAN            Probed;
  BOOLEAN            Mirror;
} mFb = { 0 };

#define FB_CELL_W  (APPLE_FB_GLYPH_W * mFb.Scale)
#define FB_CELL_H  (APPLE_FB_GLYPH_H * mFb.Scale)

STATIC
VOID
FbPuts (
  IN CONST CHAR8  *Text
  );

STATIC
VOID
FbPrintDec (
  IN UINTN  Value
  );

STATIC
VOID
FbPrintHex (
  IN UINTN  Value
  );

STATIC
VOID
FbProbeLine (
  IN BOOLEAN      Mirror,
  IN CONST CHAR8  *Text
  );

STATIC
VOID
FbGoto (
  IN UINTN  Column
  );

STATIC
VOID
FbFlush (
  IN UINTN  Offset,
  IN UINTN  Length
  )
{
  WriteBackDataCacheRange ((VOID *)((UINTN)mFb.Base + Offset), Length);
}

STATIC
VOID
FbPixel (
  IN UINTN   X,
  IN UINTN   Y,
  IN UINT32  Color
  )
{
  if (mFb.Mirror) {
    X = mFb.Width - 1 - X;
  }

  mFb.Base[Y * mFb.StridePx + X] = Color;
}

/**
  Work out where the framebuffer is and how big a character cell is.

  Called from DebugPrint, so it must not recurse into anything that prints.
**/
STATIC
VOID
FbProbe (
  VOID
  )
{
  APPLE_BOOT_ARGS  *BootArgs;
  UINTN            CellH;

  if (mFb.Probed) {
    return;
  }

  //
  // PrePi (SEC) is kept on BaseDebugLibNull because it runs on m1n1's page
  // tables, where the video carveout may not be mapped. Everything from DxeCore
  // up shares the address space SimpleFbDxe later proves writable, so the first
  // call here is already safe.
  //
  mFb.Probed = TRUE;

  BootArgs = (APPLE_BOOT_ARGS *)(UINTN)FixedPcdGet64 (PcdBootArgsPointer);
  if (BootArgs == NULL) {
    return;
  }

  if ((BootArgs->VideoBase == 0) || (BootArgs->VideoWidth < 640) ||
      (BootArgs->VideoHeight < 480) || (BootArgs->VideoStride == 0) ||
      ((BootArgs->VideoDepth & 0xFF) != 30 && (BootArgs->VideoDepth & 0xFF) != 32))
  {
    return;
  }

  mFb.Base     = (volatile UINT32 *)(UINTN)BootArgs->VideoBase;
  mFb.Width    = (UINTN)BootArgs->VideoWidth;
  mFb.Height   = (UINTN)BootArgs->VideoHeight;
  mFb.StridePx = (UINTN)(BootArgs->VideoStride / 4);
  mFb.Scale    = (mFb.Width >= 1280) ? APPLE_FB_DEFAULT_SCALE : 1;
  mFb.Cols     = mFb.Width / (APPLE_FB_GLYPH_W * mFb.Scale);
  mFb.Rows     = mFb.Height / (APPLE_FB_GLYPH_H * mFb.Scale);
  mFb.CurX     = 0;
  mFb.CurY     = 0;
  mFb.Mirror   = FB_MIRROR_X;
  mFb.Ready    = TRUE;

  //
  // Clear the text area and say what we found, so that a blank screen means
  // "this library never ran" rather than "the log said nothing".
  //
  CellH = FB_CELL_H;
  SetMem ((VOID *)mFb.Base, mFb.StridePx * 4 * CellH * mFb.Rows, 0);
  FbFlush (0, mFb.StridePx * 4 * CellH * mFb.Rows);

  FbPuts ("#### APPLE FB DEBUG CONSOLE ####\n");
  FbPuts ("FB BASE 0x");
  FbPrintHex ((UINTN)mFb.Base);
  FbPuts (" ");
  FbPrintDec (mFb.Width);
  FbPuts ("X");
  FbPrintDec (mFb.Height);
  FbPuts (" STRIDE ");
  FbPrintDec (mFb.StridePx);
  FbPuts (" SCALE ");
  FbPrintDec (mFb.Scale);
  FbPuts (" MIRROR ");
  FbPrintDec (mFb.Mirror ? 1 : 0);
  FbPuts ("\n");

  //
  // Which way this panel scans out is a fact about the hardware that can only
  // be read off the screen -- and reading the screen through a camera adds its
  // own mirror if one is pointing the wrong way. So draw the same string twice,
  // once each way, and let whoever is looking say which one is legible. That
  // settles it in one boot instead of one boot per guess.
  //
  FbProbeLine (FALSE, "PROBE-A PLAIN    ABCDEFGHIJK 0123456789");
  FbProbeLine (TRUE,  "PROBE-B MIRRORED ABCDEFGHIJK 0123456789");
  FbPuts ("READABLE LINE WINS. RUNTIME MIRROR ");
  FbPrintDec (mFb.Mirror ? 1 : 0);
  FbPuts ("\n");
  //
  // Orientation test that stays inside the text path. Two words on one row,
  // one at each margin: if the row starts with Z-RIGHT the scanout runs the
  // other way. Drawn with the same code that already renders the banner, so a
  // crash here cannot be blamed on a separate pixel path.
  //
  FbGoto ((mFb.Cols > 24) ? (mFb.Cols - 8) : 0);
  FbPuts ("Z-RIGHT");
  FbPutChar ('\n');
  FbGoto (0);
  FbPuts ("A-LEFT\n");
}

STATIC
VOID
FbScroll (
  VOID
  )
{
  UINTN  LineBytes;
  UINTN  CellH;

  CellH     = FB_CELL_H;
  LineBytes = mFb.StridePx * 4;

  CopyMem (
    (VOID *)mFb.Base,
    (VOID *)((UINTN)mFb.Base + CellH * LineBytes),
    (mFb.Rows - 1) * CellH * LineBytes
    );
  SetMem (
    (VOID *)((UINTN)mFb.Base + (mFb.Rows - 1) * CellH * LineBytes),
    CellH * LineBytes,
    0
    );
  FbFlush (0, mFb.Rows * CellH * LineBytes);
  mFb.CurY = mFb.Rows - 1;
}

/**
  Flush one character row: the whole band the glyph sits in.

  Flushing per glyph would write back a full-band span per character; on a
  2560 x 1600 panel that is hundreds of kilobytes for sixteen pixels of text,
  and the dispatch log is thousands of characters long. Once per text row is
  enough because each row is drawn left to right before the next begins.
**/
STATIC
VOID
FbFlushRow (
  IN UINTN  Row
  )
{
  if (!mFb.Ready) {
    return;
  }

  FbFlush (Row * FB_CELL_H * mFb.StridePx * 4, FB_CELL_H * mFb.StridePx * 4);
}

STATIC
VOID
FbDrawGlyph (
  IN CHAR8  Ch
  )
{
  CONST APPLE_FB_GLYPH  *Glyph;
  UINTN                 OriginX;
  UINTN                 OriginY;
  UINTN                 Col;
  UINTN                 Row;
  UINTN                 Py;
  UINTN                 Px;
  UINT32                Bits;

  if ((UINT8)Ch < 0x20) {
    Ch = ' ';
  }

  if ((UINT8)Ch > 0x7E) {
    Ch = '?';
  }

  Glyph   = &mAppleFbGlyphs[(UINT8)Ch - 0x20];
  OriginX = mFb.CurX * FB_CELL_W;
  OriginY = mFb.CurY * FB_CELL_H;

  for (Col = 0; Col < APPLE_FB_GLYPH_W; Col++) {
    for (Row = 0; Row < APPLE_FB_GLYPH_H; Row++) {
      Bits = Glyph->GlyphCol1[Row];
      for (Px = 0; Px < mFb.Scale; Px++) {
        for (Py = 0; Py < mFb.Scale; Py++) {
          UINTN  X = OriginX + Col * mFb.Scale + Px;
          UINTN  Y = OriginY + Row * mFb.Scale + Py;
          //
          // Bit 0 of GlyphCol1 is the leftmost pixel of the cell -- see
          // GraphicsConsoleDxe's cursor code, which tests (BIT0 << PosX).
          //
          FbPixel (X, Y, ((Bits >> Col) & 1) ? 0xFFFFFFFFu : 0x00000000u);
        }
      }
    }
  }

}

STATIC
VOID
FbPutChar (
  IN CHAR8  Ch
  )
{
  if (!mFb.Ready) {
    return;
  }

  if (Ch == '\r') {
    return;
  }

  if (Ch == '\n') {
    FbFlushRow (mFb.CurY);
    mFb.CurX = 0;
    mFb.CurY++;
    if (mFb.CurY >= mFb.Rows) {
      FbScroll ();
    }

    return;
  }

  if ((Ch >= 'a') && (Ch <= 'z')) {
    Ch = (CHAR8)(Ch - 'a' + 'A');
  }

  FbDrawGlyph (Ch);
  mFb.CurX++;
  if (mFb.CurX >= mFb.Cols) {
    FbFlushRow (mFb.CurY);
    mFb.CurX = 0;
    mFb.CurY++;
    if (mFb.CurY >= mFb.Rows) {
      FbScroll ();
    }
  }
}

/**
  Park the cursor at a column so a row can be marked at both margins.
**/
STATIC
VOID
FbGoto (
  IN UINTN  Column
  )
{
  if (!mFb.Ready) {
    return;
  }

  if (Column >= mFb.Cols) {
    Column = mFb.Cols - 1;
  }

  FbFlushRow (mFb.CurY);
  mFb.CurX = Column;
}

STATIC
VOID
FbPuts (
  IN CONST CHAR8  *Text
  )
{
  while (*Text != '\0') {
    FbPutChar (*Text++);
  }
}

STATIC
VOID
FbProbeLine (
  IN BOOLEAN      Mirror,
  IN CONST CHAR8  *Text
  )
{
  BOOLEAN  Saved;

  Saved      = mFb.Mirror;
  mFb.Mirror = Mirror;
  FbPuts (Text);
  FbPutChar ('\n');
  mFb.Mirror = Saved;
}

STATIC
VOID
FbPrintDec (
  IN UINTN  Value
  )
{
  CHAR8  Digits[24];
  UINTN  Index;

  if (Value == 0) {
    FbPutChar ('0');
    return;
  }

  Index = 0;
  while ((Value != 0) && (Index < sizeof (Digits))) {
    Digits[Index++] = (CHAR8)('0' + (Value % 10));
    Value          /= 10;
  }

  while (Index > 0) {
    FbPutChar (Digits[--Index]);
  }
}

STATIC
VOID
FbPrintHex (
  IN UINTN  Value
  )
{
  STATIC CONST CHAR8  Hex[] = "0123456789ABCDEF";
  UINTN               Shift;

  Shift = sizeof (UINTN) * 8;
  while (Shift > 4) {
    Shift -= 4;
    FbPutChar (Hex[(Value >> Shift) & 0xF]);
  }

  FbPutChar (Hex[Value & 0xF]);
}

VOID
EFIAPI
DebugPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR8    Buffer[512];

  (VOID)ErrorLevel;
  VA_START (Marker, Format);
  AsciiVSPrint (Buffer, sizeof (Buffer) - 1, Format, Marker);
  VA_END (Marker);

  FbProbe ();
  FbPuts (Buffer);
}

VOID
EFIAPI
DebugVPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  IN VA_LIST      VaList
  )
{
  CHAR8  Buffer[512];

  (VOID)ErrorLevel;
  AsciiVSPrint (Buffer, sizeof (Buffer) - 1, Format, VaList);

  FbProbe ();
  FbPuts (Buffer);
}

VOID
EFIAPI
DebugBPrint (
  IN UINTN        ErrorLevel,
  IN CONST CHAR8  *Format,
  IN BASE_LIST    BaseListMarker
  )
{
  CHAR8  Buffer[512];

  (VOID)ErrorLevel;
  AsciiBSPrint (Buffer, sizeof (Buffer) - 1, Format, BaseListMarker);

  FbProbe ();
  FbPuts (Buffer);
}

VOID
EFIAPI
DebugAssert (
  IN CONST CHAR8  *FileName,
  IN UINTN        LineNumber,
  IN CONST CHAR8  *Description
  )
{
  FbProbe ();
  FbPuts ("ASSERT ");
  FbPuts (FileName);
  FbPutChar (':');
  FbPuts (Description);
  FbPutChar ('\n');

  CpuDeadLoop ();
}

BOOLEAN
EFIAPI
DebugAssertEnabled (
  VOID
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugPrintEnabled (
  VOID
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugCodeEnabled (
  VOID
  )
{
  return TRUE;
}

BOOLEAN
EFIAPI
DebugClearMemoryEnabled (
  VOID
  )
{
  return TRUE;
}

VOID *
EFIAPI
DebugClearMemory (
  IN VOID   *Address,
  IN UINTN  Length
  )
{
  return SetMem (Address, Length, 0xAF);
}

BOOLEAN
EFIAPI
DebugPrintLevelEnabled (
  IN CONST UINTN  ErrorLevel
  )
{
  //
  // Honour PcdDebugPrintErrorLevel. Returning TRUE unconditionally put every
  // DEBUG_POOL and DEBUG_PAGE line on the panel and buried the actual failure;
  // DEBUG_ERROR still gets through, which is what a bring-up log needs.
  //
  return (BOOLEAN)((ErrorLevel & (UINTN)GetDebugPrintErrorLevel ()) != 0);
}
