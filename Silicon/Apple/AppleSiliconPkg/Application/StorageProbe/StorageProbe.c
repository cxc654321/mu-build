/*
 * Copyright (c) 2026 Aurora Silicon
 */

/** @file
  Read-only Block I/O and GPT inspection application.

  This application deliberately never calls WriteBlocks.  It is intended for
  early Apple ANS bring-up where the first useful question is whether firmware
  can read the protective MBR, GPT header, and partition-entry array.
**/

#include <Uefi.h>

#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiFont.h>
#include <Protocol/SimpleFileSystem.h>
#include <Uefi/UefiGpt.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define GPT_HEADER_SIGNATURE  SIGNATURE_64 ('E', 'F', 'I', ' ', 'P', 'A', 'R', 'T')
#define MAX_GPT_ENTRY_BYTES   (1024U * 1024U)

//
// The default GraphicsConsole font is only 8x19 pixels.  At the J813 panel's
// native 2560x1664 resolution that leaves diagnostics in a tiny box in the
// middle of the screen.  Mu's SimpleWindowManager registers the 48-point
// Selawik package before BDS; its 61-pixel cell and 66-pixel line height make a
// useful full-screen bring-up console without depending on the experimental
// VUART aperture.
//
#define PROBE_PRINT_BUFFER_CHARS  1024U
#define PROBE_FONT_HEIGHT         61U
#define PROBE_LINE_HEIGHT         66U
#define PROBE_SCREEN_MARGIN_X     64U
#define PROBE_SCREEN_MARGIN_Y     48U
#define PROBE_PARTITION_BAR_WIDTH 36U

STATIC CONST EFI_GUID  mAppleApfsPartitionGuid = {
  0x7C3457EF, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }
};
STATIC CONST EFI_GUID  mAppleHfsPartitionGuid = {
  0x48465300, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }
};
STATIC CONST EFI_GUID  mAppleBootPartitionGuid = {
  0x426F6F74, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }
};
STATIC CONST EFI_GUID  mAppleRecoveryPartitionGuid = {
  0x52637672, 0x7900, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC }
};
STATIC CONST EFI_GUID  mEfiSystemPartitionGuid = {
  0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }
};
STATIC CONST EFI_GUID  mMicrosoftBasicDataGuid = {
  0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 }
};
STATIC CONST EFI_GUID  mMicrosoftReservedGuid = {
  0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE }
};
STATIC CONST EFI_GUID  mWindowsRecoveryGuid = {
  0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC }
};
STATIC CONST EFI_GUID  mLinuxFileSystemGuid = {
  0x0FC63DAF, 0x8483, 0x4772, { 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 }
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mProbeGop;
STATIC EFI_HII_FONT_PROTOCOL         *mProbeHiiFont;
STATIC UINTN                         mProbeCursorY;

STATIC
VOID
ProbeDisplayClear (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Background;

  if (mProbeGop == NULL) {
    return;
  }

  ZeroMem (&Background, sizeof (Background));
  (VOID)mProbeGop->Blt (
                    mProbeGop,
                    &Background,
                    EfiBltVideoFill,
                    0,
                    0,
                    0,
                    0,
                    mProbeGop->Mode->Info->HorizontalResolution,
                    mProbeGop->Mode->Info->VerticalResolution,
                    0
                    );
  mProbeCursorY = PROBE_SCREEN_MARGIN_Y;
}

STATIC
VOID
ProbeDisplayInitialize (
  VOID
  )
{
  EFI_STATUS  Status;

  mProbeGop     = NULL;
  mProbeHiiFont = NULL;
  mProbeCursorY = PROBE_SCREEN_MARGIN_Y;

  if ((gST == NULL) || (gST->ConsoleOutHandle == NULL)) {
    return;
  }

  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&mProbeGop
                  );
  if (EFI_ERROR (Status)) {
    mProbeGop = NULL;
    return;
  }

  Status = gBS->LocateProtocol (
                  &gEfiHiiFontProtocolGuid,
                  NULL,
                  (VOID **)&mProbeHiiFont
                  );
  if (EFI_ERROR (Status)) {
    mProbeHiiFont = NULL;
    return;
  }

  ProbeDisplayClear ();
}

STATIC
UINTN
EFIAPI
ProbePrint (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST                         Marker;
  CHAR16                          Buffer[PROBE_PRINT_BUFFER_CHARS];
  UINTN                           CharacterCount;
  EFI_FONT_DISPLAY_INFO           FontInfo;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   Background;
  EFI_IMAGE_OUTPUT                Image;
  EFI_IMAGE_OUTPUT                *ImagePointer;
  EFI_HII_ROW_INFO                *Rows;
  UINTN                           RowCount;
  UINTN                           RowIndex;
  UINTN                           RenderedHeight;
  EFI_STATUS                      Status;

  VA_START (Marker, Format);
  CharacterCount = UnicodeVSPrint (
                     Buffer,
                     sizeof (Buffer),
                     Format,
                     Marker
                     );
  VA_END (Marker);

  if ((mProbeGop == NULL) || (mProbeHiiFont == NULL)) {
    if ((gST != NULL) && (gST->ConOut != NULL)) {
      (VOID)gST->ConOut->OutputString (gST->ConOut, Buffer);
    }

    return CharacterCount;
  }

  if ((mProbeCursorY + PROBE_LINE_HEIGHT + PROBE_SCREEN_MARGIN_Y) >
      mProbeGop->Mode->Info->VerticalResolution)
  {
    ProbeDisplayClear ();
  }

  ZeroMem (&Foreground, sizeof (Foreground));
  Foreground.Red   = 0xF2;
  Foreground.Green = 0xF2;
  Foreground.Blue  = 0xF2;
  ZeroMem (&Background, sizeof (Background));

  ZeroMem (&FontInfo, sizeof (FontInfo));
  FontInfo.ForegroundColor       = Foreground;
  FontInfo.BackgroundColor       = Background;
  FontInfo.FontInfoMask          = EFI_FONT_INFO_ANY_FONT;
  FontInfo.FontInfo.FontStyle    = EFI_HII_FONT_STYLE_NORMAL;
  FontInfo.FontInfo.FontSize     = PROBE_FONT_HEIGHT;

  ZeroMem (&Image, sizeof (Image));
  Image.Width        = (UINT16)(mProbeGop->Mode->Info->HorizontalResolution -
                                PROBE_SCREEN_MARGIN_X);
  Image.Height       = (UINT16)mProbeGop->Mode->Info->VerticalResolution;
  Image.Image.Screen = mProbeGop;
  ImagePointer       = &Image;
  Rows               = NULL;
  RowCount           = 0;

  Status = mProbeHiiFont->StringToImage (
                            mProbeHiiFont,
                            EFI_HII_OUT_FLAG_CLIP |
                            EFI_HII_OUT_FLAG_WRAP |
                            EFI_HII_OUT_FLAG_CLIP_CLEAN_Y |
                            EFI_HII_IGNORE_IF_NO_GLYPH |
                            EFI_HII_DIRECT_TO_SCREEN,
                            Buffer,
                            &FontInfo,
                            &ImagePointer,
                            PROBE_SCREEN_MARGIN_X,
                            mProbeCursorY,
                            &Rows,
                            &RowCount,
                            NULL
                            );
  if (EFI_ERROR (Status)) {
    if ((gST != NULL) && (gST->ConOut != NULL)) {
      (VOID)gST->ConOut->OutputString (gST->ConOut, Buffer);
    }

    return CharacterCount;
  }

  RenderedHeight = 0;
  for (RowIndex = 0; RowIndex < RowCount; RowIndex++) {
    RenderedHeight += Rows[RowIndex].LineHeight;
  }

  if (Rows != NULL) {
    FreePool (Rows);
  }

  mProbeCursorY += (RenderedHeight != 0) ? RenderedHeight : PROBE_LINE_HEIGHT;
  return CharacterCount;
}

// Keep the existing diagnostic call sites concise while routing them through
// the large-screen renderer above.
#define Print  ProbePrint

STATIC
BOOLEAN
AuroraIsZeroGuid (
  IN CONST EFI_GUID  *Guid
  )
{
  CONST UINT8  *Bytes;
  UINTN        Index;

  Bytes = (CONST UINT8 *)Guid;
  for (Index = 0; Index < sizeof (*Guid); Index++) {
    if (Bytes[Index] != 0) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
VOID
PrintGuid (
  IN CONST EFI_GUID  *Guid
  )
{
  Print (
    L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    Guid->Data1,
    Guid->Data2,
    Guid->Data3,
    Guid->Data4[0],
    Guid->Data4[1],
    Guid->Data4[2],
    Guid->Data4[3],
    Guid->Data4[4],
    Guid->Data4[5],
    Guid->Data4[6],
    Guid->Data4[7]
    );
}

STATIC
VOID
BuildCapacityBar (
  OUT CHAR16  *Bar,
  IN  UINTN   BarCharacters,
  IN  UINT64  BlockCount,
  IN  UINT64  TotalBlocks,
  IN  CHAR16  FillCharacter
  )
{
  UINTN  Fill;
  UINTN  Index;

  if (BarCharacters < 3) {
    return;
  }

  Fill = 0;
  if ((BlockCount != 0) && (TotalBlocks != 0)) {
    Fill = (UINTN)((BlockCount * (BarCharacters - 2)) / TotalBlocks);
    if (Fill == 0) {
      Fill = 1;
    }

    if (Fill > (BarCharacters - 2)) {
      Fill = BarCharacters - 2;
    }
  }

  Bar[0] = L'[';
  for (Index = 0; Index < (BarCharacters - 2); Index++) {
    Bar[Index + 1] = (Index < Fill) ? FillCharacter : L'.';
  }

  Bar[BarCharacters - 1] = L']';
  Bar[BarCharacters]     = L'\0';
}

STATIC
VOID
PrintCapacity (
  IN UINT64  SizeMiB
  )
{
  UINT64  SizeGiB;
  UINTN   Tenths;

  if (SizeMiB >= 1024) {
    SizeGiB = SizeMiB / 1024;
    Tenths  = (UINTN)(((SizeMiB % 1024) * 10) / 1024);
    Print (L"%lu.%u GiB", SizeGiB, Tenths);
  } else {
    Print (L"%lu MiB", SizeMiB);
  }
}

STATIC
VOID
PrintProbeBanner (
  IN UINTN   HandleIndex,
  IN UINT64  TotalMiB,
  IN UINT32  BlockSize
  )
{
  Print (L"+------------------------------------------------------------+\r\n");
  Print (L"|  A U R O R A   S I L I C O N   //   INTERNAL STORAGE      |\r\n");
  Print (L"+------------------------------------------------------------+\r\n");
  Print (L"|  DISK %u  |  READ ONLY  |  ", HandleIndex);
  PrintCapacity (TotalMiB);
  Print (L"  |  BLOCK %u  |\r\n", BlockSize);
  Print (L"+------------------------------------------------------------+\r\n");
}

STATIC
CONST CHAR16 *
PartitionTypeName (
  IN CONST EFI_GUID  *Guid
  )
{
  if (CompareGuid (Guid, &mAppleApfsPartitionGuid)) {
    return L"Apple APFS";
  }

  if (CompareGuid (Guid, &mAppleHfsPartitionGuid)) {
    return L"Apple HFS+";
  }

  if (CompareGuid (Guid, &mAppleBootPartitionGuid)) {
    return L"Apple Boot";
  }

  if (CompareGuid (Guid, &mAppleRecoveryPartitionGuid)) {
    return L"Apple Recovery";
  }

  if (CompareGuid (Guid, &mEfiSystemPartitionGuid)) {
    return L"EFI System";
  }

  if (CompareGuid (Guid, &mMicrosoftBasicDataGuid)) {
    return L"Microsoft Basic";
  }

  if (CompareGuid (Guid, &mMicrosoftReservedGuid)) {
    return L"Microsoft Reserved";
  }

  if (CompareGuid (Guid, &mWindowsRecoveryGuid)) {
    return L"Windows Recovery";
  }

  if (CompareGuid (Guid, &mLinuxFileSystemGuid)) {
    return L"Linux Filesystem";
  }

  return L"Unknown";
}

STATIC
CONST CHAR16 *
ProbeFileSystem (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_LBA                StartingLba
  )
{
  EFI_STATUS  Status;
  UINT8       *Block;
  UINTN       Index;
  BOOLEAN     AllZero;

  Block = AllocateZeroPool (BlockIo->Media->BlockSize);
  if (Block == NULL) {
    return L"no-memory";
  }

  Status = BlockIo->ReadBlocks (
                      BlockIo,
                      BlockIo->Media->MediaId,
                      StartingLba,
                      BlockIo->Media->BlockSize,
                      Block
                      );
  if (EFI_ERROR (Status)) {
    FreePool (Block);
    return L"unreadable";
  }

  if ((BlockIo->Media->BlockSize >= 36) &&
      (CompareMem (Block + 32, "NXSB", 4) == 0))
  {
    FreePool (Block);
    return L"APFS";
  }

  if ((BlockIo->Media->BlockSize >= 11) &&
      (CompareMem (Block + 3, "NTFS    ", 8) == 0))
  {
    FreePool (Block);
    return L"NTFS";
  }

  if ((BlockIo->Media->BlockSize >= 11) &&
      (CompareMem (Block + 3, "EXFAT   ", 8) == 0))
  {
    FreePool (Block);
    return L"exFAT";
  }

  if ((BlockIo->Media->BlockSize >= 90) &&
      (CompareMem (Block + 82, "FAT32   ", 8) == 0))
  {
    FreePool (Block);
    return L"FAT32";
  }

  if ((BlockIo->Media->BlockSize >= 62) &&
      ((CompareMem (Block + 54, "FAT16   ", 8) == 0) ||
       (CompareMem (Block + 54, "FAT12   ", 8) == 0)))
  {
    FreePool (Block);
    return L"FAT12/16";
  }

  if ((BlockIo->Media->BlockSize >= 1082) &&
      (Block[1080] == 0x53) && (Block[1081] == 0xEF))
  {
    FreePool (Block);
    return L"ext2/3/4";
  }

  if ((BlockIo->Media->BlockSize >= 1026) &&
      (Block[1024] == 'H') && ((Block[1025] == '+') || (Block[1025] == 'X')))
  {
    FreePool (Block);
    return L"HFS+";
  }

  AllZero = TRUE;
  for (Index = 0; Index < BlockIo->Media->BlockSize; Index++) {
    if (Block[Index] != 0) {
      AllZero = FALSE;
      break;
    }
  }

  FreePool (Block);
  return AllZero ? L"EMPTY/ZEROED" : L"unknown";
}

STATIC
VOID
PrintFreeRange (
  IN UINT64  StartingLba,
  IN UINT64  EndingLba,
  IN UINT32  BlockSize,
  IN UINT64  TotalBlocks
  )
{
  UINT64  BlockCount;
  UINT64  SizeMiB;
  CHAR16  Bar[PROBE_PARTITION_BAR_WIDTH + 1];
  UINTN   Permille;

  if (EndingLba < StartingLba) {
    return;
  }

  BlockCount = EndingLba - StartingLba + 1;
  SizeMiB    = (BlockCount * BlockSize) >> 20;
  Permille   = (TotalBlocks == 0) ? 0 : (UINTN)((BlockCount * 1000) / TotalBlocks);
  BuildCapacityBar (Bar, PROBE_PARTITION_BAR_WIDTH, BlockCount, TotalBlocks, L'-');
  Print (L"  FREE  %s  %u.%u%%  ", Bar, Permille / 10, Permille % 10);
  PrintCapacity (SizeMiB);
  Print (L"\r\n");
  Print (L"        unallocated  |  LBA %lu .. %lu\r\n", StartingLba, EndingLba);
}

STATIC
EFI_STATUS
ProbeGpt (
  IN UINTN                  HandleIndex,
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo
  )
{
  EFI_STATUS            Status;
  EFI_BLOCK_IO_MEDIA    *Media;
  UINT8                 *Block;
  EFI_PARTITION_TABLE_HEADER  *Header;
  UINT32                StoredHeaderCrc;
  UINT32                CalculatedCrc;
  BOOLEAN               HeaderCrcValid;
  BOOLEAN               EntriesCrcValid;
  UINTN                 EntryBytes;
  UINTN                 ReadBytes;
  VOID                  *Entries;
  UINTN                 EntryIndex;
  UINTN                 UsedEntries;
  UINT64                PreviousEnd;
  UINT64                TotalBlocks;
  UINT64                TotalMiB;

  Media = BlockIo->Media;
  if ((Media == NULL) || (Media->BlockSize < 512)) {
    Print (L"SPROBE[%u]: skip invalid media/block size\r\n", HandleIndex);
    return EFI_UNSUPPORTED;
  }

  Block = AllocateZeroPool (Media->BlockSize);
  if (Block == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Print (L"SPROBE[%u]: READ lba=0 bytes=%u BEGIN\r\n", HandleIndex, Media->BlockSize);
  Status = BlockIo->ReadBlocks (
                      BlockIo,
                      Media->MediaId,
                      0,
                      Media->BlockSize,
                      Block
                      );
  Print (L"SPROBE[%u]: READ lba=0 END status=%r\r\n", HandleIndex, Status);
  if (EFI_ERROR (Status)) {
    FreePool (Block);
    return Status;
  }

  Print (
    L"SPROBE[%u]: MBR signature=%02x%02x protective-type=%02x\r\n",
    HandleIndex,
    Block[511],
    Block[510],
    Block[450]
    );

  ZeroMem (Block, Media->BlockSize);
  Print (L"SPROBE[%u]: READ lba=1 bytes=%u BEGIN\r\n", HandleIndex, Media->BlockSize);
  Status = BlockIo->ReadBlocks (
                      BlockIo,
                      Media->MediaId,
                      1,
                      Media->BlockSize,
                      Block
                      );
  Print (L"SPROBE[%u]: READ lba=1 END status=%r\r\n", HandleIndex, Status);
  if (EFI_ERROR (Status)) {
    FreePool (Block);
    return Status;
  }

  Header = (EFI_PARTITION_TABLE_HEADER *)Block;
  Print (L"SPROBE[%u]: GPT signature=0x%016lx\r\n", HandleIndex, Header->Header.Signature);
  if (Header->Header.Signature != GPT_HEADER_SIGNATURE) {
    FreePool (Block);
    return EFI_NOT_FOUND;
  }

  if ((Header->Header.HeaderSize < sizeof (EFI_TABLE_HEADER)) ||
      (Header->Header.HeaderSize > Media->BlockSize) ||
      (Header->SizeOfPartitionEntry < sizeof (EFI_PARTITION_ENTRY)) ||
      (Header->NumberOfPartitionEntries == 0) ||
      (Header->NumberOfPartitionEntries > (MAX_GPT_ENTRY_BYTES / Header->SizeOfPartitionEntry)))
  {
    Print (L"SPROBE[%u]: GPT header bounds invalid\r\n", HandleIndex);
    FreePool (Block);
    return EFI_COMPROMISED_DATA;
  }

  StoredHeaderCrc       = Header->Header.CRC32;
  Header->Header.CRC32  = 0;
  CalculatedCrc         = 0;
  Status = gBS->CalculateCrc32 (Header, Header->Header.HeaderSize, &CalculatedCrc);
  Header->Header.CRC32 = StoredHeaderCrc;
  if (EFI_ERROR (Status)) {
    FreePool (Block);
    return Status;
  }

  HeaderCrcValid = (StoredHeaderCrc == CalculatedCrc);
  Print (
    L"SPROBE[%u]: GPT header-crc stored=%08x calculated=%08x %s\r\n",
    HandleIndex,
    StoredHeaderCrc,
    CalculatedCrc,
    HeaderCrcValid ? L"PASS" : L"FAIL"
    );
  EntriesCrcValid = FALSE;
  Print (
    L"SPROBE[%u]: GPT usable=%lu..%lu entries-lba=%lu count=%u size=%u\r\n",
    HandleIndex,
    Header->FirstUsableLBA,
    Header->LastUsableLBA,
    Header->PartitionEntryLBA,
    Header->NumberOfPartitionEntries,
    Header->SizeOfPartitionEntry
    );

  EntryBytes = (UINTN)Header->NumberOfPartitionEntries * Header->SizeOfPartitionEntry;
  if (EntryBytes > (MAX_UINTN - (Media->BlockSize - 1))) {
    FreePool (Block);
    return EFI_BAD_BUFFER_SIZE;
  }

  ReadBytes = (EntryBytes + Media->BlockSize - 1) / Media->BlockSize * Media->BlockSize;
  Entries   = AllocateZeroPool (ReadBytes);
  if (Entries == NULL) {
    FreePool (Block);
    return EFI_OUT_OF_RESOURCES;
  }

  Print (
    L"SPROBE[%u]: READ entries lba=%lu bytes=%u BEGIN\r\n",
    HandleIndex,
    Header->PartitionEntryLBA,
    ReadBytes
    );
  Status = BlockIo->ReadBlocks (
                      BlockIo,
                      Media->MediaId,
                      Header->PartitionEntryLBA,
                      ReadBytes,
                      Entries
                      );
  Print (L"SPROBE[%u]: READ entries END status=%r\r\n", HandleIndex, Status);
  if (EFI_ERROR (Status)) {
    FreePool (Entries);
    FreePool (Block);
    return Status;
  }

  CalculatedCrc = 0;
  Status = gBS->CalculateCrc32 (Entries, EntryBytes, &CalculatedCrc);
  if (EFI_ERROR (Status)) {
    FreePool (Entries);
    FreePool (Block);
    return Status;
  }

  EntriesCrcValid = (Header->PartitionEntryArrayCRC32 == CalculatedCrc);

  Print (
    L"SPROBE[%u]: GPT entries-crc stored=%08x calculated=%08x %s\r\n",
    HandleIndex,
    Header->PartitionEntryArrayCRC32,
    CalculatedCrc,
    EntriesCrcValid ? L"PASS" : L"FAIL"
    );

  TotalBlocks = Media->LastBlock + 1;
  TotalMiB    = (TotalBlocks * Media->BlockSize) >> 20;
  //
  // The reads above deliberately leave detailed breadcrumbs while bring-up is
  // in progress.  Once both GPT payloads are resident, replace that prelude
  // with the operator-facing disk map so every partition fits on one panel.
  //
  ProbeDisplayClear ();
  PrintProbeBanner (HandleIndex, TotalMiB, Media->BlockSize);
  Print (
    L"|  GPT HEADER  [%s]   ENTRY ARRAY  [%s]                   |\r\n",
    HeaderCrcValid ? L"OK" : L"!!",
    EntriesCrcValid ? L"OK" : L"!!"
    );
  Print (L"+----------------------- DISK MAP ---------------------------+\r\n");
  UsedEntries = 0;
  PreviousEnd = (Header->FirstUsableLBA == 0) ? 0 : Header->FirstUsableLBA - 1;

  for (EntryIndex = 0; EntryIndex < Header->NumberOfPartitionEntries; EntryIndex++) {
    EFI_PARTITION_ENTRY  *Entry;
    CHAR16               Name[ARRAY_SIZE (((EFI_PARTITION_ENTRY *)0)->PartitionName) + 1];
    CONST CHAR16         *TypeName;
    CONST CHAR16         *FileSystem;
    UINT64               BlockCount;
    UINT64               SizeMiB;
    UINTN                Permille;
    CHAR16               Bar[PROBE_PARTITION_BAR_WIDTH + 1];

    Entry = (EFI_PARTITION_ENTRY *)((UINT8 *)Entries +
                                    (EntryIndex * Header->SizeOfPartitionEntry));
    if (AuroraIsZeroGuid (&Entry->PartitionTypeGUID)) {
      continue;
    }

    UsedEntries++;
    if ((Entry->StartingLBA > Header->FirstUsableLBA) &&
        (Entry->StartingLBA > PreviousEnd + 1))
    {
      PrintFreeRange (
        PreviousEnd + 1,
        Entry->StartingLBA - 1,
        Media->BlockSize,
        TotalBlocks
        );
    }

    CopyMem (Name, Entry->PartitionName, sizeof (Entry->PartitionName));
    Name[ARRAY_SIZE (Name) - 1] = L'\0';
    TypeName   = PartitionTypeName (&Entry->PartitionTypeGUID);
    FileSystem = ProbeFileSystem (BlockIo, Entry->StartingLBA);
    BlockCount = (Entry->EndingLBA >= Entry->StartingLBA) ?
                 Entry->EndingLBA - Entry->StartingLBA + 1 : 0;
    SizeMiB  = (BlockCount * Media->BlockSize) >> 20;
    Permille = (TotalBlocks == 0) ? 0 : (UINTN)((BlockCount * 1000) / TotalBlocks);
    BuildCapacityBar (Bar, PROBE_PARTITION_BAR_WIDTH, BlockCount, TotalBlocks, L'#');
    Print (L"  P%02u   %s  %u.%u%%  ", EntryIndex + 1, Bar, Permille / 10, Permille % 10);
    PrintCapacity (SizeMiB);
    Print (L"\r\n");
    Print (
      L"        %s  |  %s  |  %s\r\n",
      TypeName,
      FileSystem,
      (Name[0] == L'\0') ? L"(unnamed)" : Name
      );
    if (StrCmp (TypeName, L"Unknown") == 0) {
      Print (L"SPROBE[%u]:     type-guid=", HandleIndex);
      PrintGuid (&Entry->PartitionTypeGUID);
      Print (L"\r\n");
    }

    if (Entry->EndingLBA > PreviousEnd) {
      PreviousEnd = Entry->EndingLBA;
    }
  }

  if (PreviousEnd < Header->LastUsableLBA) {
    PrintFreeRange (
      PreviousEnd + 1,
      Header->LastUsableLBA,
      Media->BlockSize,
      TotalBlocks
      );
  }

  Print (L"+------------------------ SUMMARY ---------------------------+\r\n");
  Print (
    L"|  %u partitions  |  %u empty GPT slots  |  CRC %s             |\r\n",
    UsedEntries,
    Header->NumberOfPartitionEntries - UsedEntries,
    (HeaderCrcValid && EntriesCrcValid) ? L"VERIFIED" : L"FAILED"
    );
  Print (L"+------------------------------------------------------------+\r\n");

  FreePool (Entries);
  FreePool (Block);
  return (HeaderCrcValid && EntriesCrcValid) ? EFI_SUCCESS : EFI_CRC_ERROR;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles;
  UINTN                  HandleCount;
  UINTN                  HandleIndex;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  EFI_BLOCK_IO_PROTOCOL  *DiagnosticBlockIo;
  EFI_BLOCK_IO_PROTOCOL  *StandardInternalBlockIo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *SimpleFileSystem;
  BOOLEAN                PublishStandardBlockIo;
  UINTN                  LogicalPartitionCount;
  UINTN                  FileSystemCount;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  ProbeDisplayInitialize ();
  PublishStandardBlockIo = PcdGetBool (PcdAppleAnsPublishBlockIo);
  Print (L"==================== AURORA STORAGE PROBE BEGIN ====================\r\n");
  Print (L"SPROBE: policy=READ-ONLY operations=LBA0,LBA1,GPT-ENTRIES\r\n");

  //
  // Probe AppleANS first through its private protocol.  The internal namespace
  // is intentionally not published under the standard Block I/O GUID in this
  // diagnostic build, preventing PartitionDxe from racing us to the first read.
  //
  DiagnosticBlockIo = NULL;
  Status = gBS->LocateProtocol (
                  &gAppleAnsDiagnosticBlockIoProtocolGuid,
                  NULL,
                  (VOID **)&DiagnosticBlockIo
                  );
  Print (L"SPROBE: Locate AppleANS diagnostic status=%r\r\n", Status);
  if (!PublishStandardBlockIo && !EFI_ERROR (Status) &&
      (DiagnosticBlockIo != NULL) &&
      (DiagnosticBlockIo->Media != NULL))
  {
    Print (
      L"SPROBE[0]: source=AppleANS-diagnostic media-id=%u present=%u logical=%u readonly=%u block=%u last=%lu align=%u\r\n",
      DiagnosticBlockIo->Media->MediaId,
      DiagnosticBlockIo->Media->MediaPresent,
      DiagnosticBlockIo->Media->LogicalPartition,
      DiagnosticBlockIo->Media->ReadOnly,
      DiagnosticBlockIo->Media->BlockSize,
      DiagnosticBlockIo->Media->LastBlock,
      DiagnosticBlockIo->Media->IoAlign
      );
    Status = ProbeGpt (0, DiagnosticBlockIo);
    if (!EFI_ERROR (Status)) {
      Print (L"\r\n+--------------------------------------------------------------+\r\n");
      Print (L"| READ ONLY | INTERNAL APPLE ANS | PARTITION MAP COMPLETE     |\r\n");
      Print (L"| Reboot the target to leave this dashboard.                  |\r\n");
      Print (L"+--------------------------------------------------------------+\r\n");

      //
      // A successful internal-ANS probe is the result this diagnostic was
      // built to display.  Keep the verified map visible instead of falling
      // through to the temporary RAM-disk Block I/O handles or returning to
      // BDS, where the application would be launched again.
      //
      CpuDeadLoop ();
    }

    Print (L"SPROBE[0]: RESULT status=%r\r\n", Status);
  }

  Handles     = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  Print (L"SPROBE: LocateHandleBuffer status=%r handles=%u\r\n", Status, HandleCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // In the storage features, prove that the same Apple ANS namespace
  // reached the standard Block I/O database, and inventory the child handles
  // PartitionDxe/FatDxe created from it.  Only after that proof do we read the
  // GPT through the standard protocol pointer and render the final dashboard.
  //
  if (PublishStandardBlockIo) {
    StandardInternalBlockIo = NULL;
    LogicalPartitionCount   = 0;
    FileSystemCount         = 0;

    for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
      BlockIo = NULL;
      Status = gBS->HandleProtocol (
                      Handles[HandleIndex],
                      &gEfiBlockIoProtocolGuid,
                      (VOID **)&BlockIo
                      );
      if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
        continue;
      }

      if (!BlockIo->Media->LogicalPartition &&
          (BlockIo == DiagnosticBlockIo))
      {
        StandardInternalBlockIo = BlockIo;
      }

      if (BlockIo->Media->LogicalPartition) {
        LogicalPartitionCount++;
        SimpleFileSystem = NULL;
        Status = gBS->HandleProtocol (
                        Handles[HandleIndex],
                        &gEfiSimpleFileSystemProtocolGuid,
                        (VOID **)&SimpleFileSystem
                        );
        if (!EFI_ERROR (Status) && (SimpleFileSystem != NULL)) {
          FileSystemCount++;
        }
      }
    }

    Print (
      L"SPROBE: standard-path handles=%u logical-partitions=%u filesystems=%u internal=%s\r\n",
      HandleCount,
      LogicalPartitionCount,
      FileSystemCount,
      (StandardInternalBlockIo != NULL) ? L"FOUND" : L"MISSING"
      );

    if (StandardInternalBlockIo != NULL) {
      Status = ProbeGpt (0, StandardInternalBlockIo);
      if (!EFI_ERROR (Status)) {
        Print (L"\r\n+--------------------------------------------------------------+\r\n");
        Print (L"| MU BLOCK I/O + PARTITIONDXE READY                         |\r\n");
        Print (
          L"| %u logical partitions | %u EFI filesystems | READ ONLY       |\r\n",
          LogicalPartitionCount,
          FileSystemCount
          );
        Print (L"| Reboot the target to leave this dashboard.                  |\r\n");
        Print (L"+--------------------------------------------------------------+\r\n");
        CpuDeadLoop ();
      }

      Print (L"SPROBE[0]: standard-path RESULT status=%r\r\n", Status);
    }
  }

  for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
    BlockIo = NULL;
    Status = gBS->HandleProtocol (
                    Handles[HandleIndex],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo
                    );
    if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
      Print (L"SPROBE[%u]: HandleProtocol status=%r\r\n", HandleIndex, Status);
      continue;
    }

    Print (
      L"SPROBE[%u]: media-id=%u present=%u logical=%u readonly=%u block=%u last=%lu align=%u\r\n",
      HandleIndex + 1,
      BlockIo->Media->MediaId,
      BlockIo->Media->MediaPresent,
      BlockIo->Media->LogicalPartition,
      BlockIo->Media->ReadOnly,
      BlockIo->Media->BlockSize,
      BlockIo->Media->LastBlock,
      BlockIo->Media->IoAlign
      );

    if (!BlockIo->Media->MediaPresent || BlockIo->Media->LogicalPartition) {
      Print (L"SPROBE[%u]: skip absent/logical media\r\n", HandleIndex + 1);
      continue;
    }

    Status = ProbeGpt (HandleIndex + 1, BlockIo);
    Print (L"SPROBE[%u]: RESULT status=%r\r\n", HandleIndex + 1, Status);
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  Print (L"==================== AURORA STORAGE PROBE END ======================\r\n");
  return EFI_SUCCESS;
}
