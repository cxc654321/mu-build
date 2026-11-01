/** @file
  Authenticated AGX native-16K backing-pool handoff, ABI v1.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/
#ifndef NTASI_GPU_BACKING_POOL_H_
#define NTASI_GPU_BACKING_POOL_H_

#include <Uefi.h>

#define NTASI_GPU_BACKING_POOL_V1_SIGNATURE  SIGNATURE_32 ('A', 'G', 'B', 'P')
#define NTASI_GPU_BACKING_POOL_V1_VERSION    1U
#define NTASI_GPU_BACKING_POOL_V1_HEADER_SIZE  SIZE_16KB
#define NTASI_GPU_BACKING_POOL_V1_DATA_SIZE    0x40000000ULL
#define NTASI_GPU_BACKING_POOL_V1_RESERVATION_SIZE \
  (NTASI_GPU_BACKING_POOL_V1_HEADER_SIZE + NTASI_GPU_BACKING_POOL_V1_DATA_SIZE)
#define NTASI_GPU_BACKING_POOL_V1_PAGE_SHIFT  14U
#define NTASI_GPU_BACKING_POOL_V1_PAGE_COUNT  65536U

#define NTASI_GPU_BACKING_POOL_FLAG_RESERVED         BIT0
#define NTASI_GPU_BACKING_POOL_FLAG_INSTALLED        BIT1
#define NTASI_GPU_BACKING_POOL_FLAG_HEADER_SEALED    BIT2
#define NTASI_GPU_BACKING_POOL_FLAG_ZERO_ON_LEASE    BIT3
#define NTASI_GPU_BACKING_POOL_FLAG_TAIL_SHRUNK      BIT4
#define NTASI_GPU_BACKING_POOL_FLAG_GPU_INITDATA     BIT5
#define NTASI_GPU_BACKING_POOL_FLAG_WIRELESS         BIT6
#define NTASI_GPU_BACKING_POOL_KNOWN_FLAGS            (BIT7 - 1)
#define NTASI_GPU_BACKING_POOL_REQUIRED_FLAGS \
  (BIT0 | BIT1 | BIT2 | BIT3 | BIT4)

#define NTASI_GPU_BACKING_POOL_HOB_GUID \
  { 0x1cebc8ed, 0x5643, 0x4c23, { 0xb8, 0xbd, 0x6f, 0x67, 0xd7, 0xc4, 0xf9, 0x11 } }

#pragma pack(push, 1)
typedef struct {
  UINT32    Signature;
  UINT16    Version;
  UINT16    StructureSize;
  UINT32    Flags;
  UINT32    HeaderCrc32;
  UINT64    ReservationBase;
  UINT64    ReservationSize;
  UINT64    DataBase;
  UINT64    DataSize;
  UINT64    GuestMemoryBase;
  UINT64    OriginalGuestTop;
  UINT64    PhysicalMemoryTop;
  UINT32    PageShift;
  UINT32    PageCount;
  UINT32    ChipId;
  UINT16    GpuGeneration;
  UINT8     GpuVariant;
  UINT8     Reserved0;
  UINT64    GpuInitdataBase;
  UINT64    GpuInitdataSize;
  UINT64    WirelessBase;
  UINT64    WirelessSize;
  UINT64    Reserved1;
} NTASI_GPU_BACKING_POOL_V1;
#pragma pack(pop)

STATIC_ASSERT (sizeof (NTASI_GPU_BACKING_POOL_V1) == 128, "AGBP v1 ABI size");

STATIC
inline
UINT32
NtasiGpuBackingPoolCrc32 (
  IN CONST VOID  *Data,
  IN UINT32      Length
  )
{
  CONST UINT8  *Bytes;
  UINT32       Crc;
  UINT32       Index;
  UINT32       Bit;

  Bytes = Data;
  Crc = MAX_UINT32;
  for (Index = 0; Index < Length; Index++) {
    Crc ^= Bytes[Index];
    for (Bit = 0; Bit < 8; Bit++) {
      Crc = (Crc >> 1) ^ (0xedb88320U & (0U - (Crc & 1U)));
    }
  }
  return ~Crc;
}

STATIC
inline
BOOLEAN
NtasiValidateGpuBackingPoolV1 (
  IN CONST NTASI_GPU_BACKING_POOL_V1  *Header,
  IN UINT64                           ExpectedBase,
  IN UINT64                           GuestMemoryBase,
  IN UINT64                           PhysicalMemoryTop
  )
{
  NTASI_GPU_BACKING_POOL_V1  Copy;
  UINT32                     HeaderCrc;

  if ((Header == NULL) ||
      (Header->Signature != NTASI_GPU_BACKING_POOL_V1_SIGNATURE) ||
      (Header->Version != NTASI_GPU_BACKING_POOL_V1_VERSION) ||
      (Header->StructureSize != sizeof (*Header)) ||
      ((Header->Flags & NTASI_GPU_BACKING_POOL_REQUIRED_FLAGS) !=
       NTASI_GPU_BACKING_POOL_REQUIRED_FLAGS) ||
      (Header->ReservationBase != ExpectedBase) ||
      (Header->ReservationSize != NTASI_GPU_BACKING_POOL_V1_RESERVATION_SIZE) ||
      (Header->DataBase != ExpectedBase + NTASI_GPU_BACKING_POOL_V1_HEADER_SIZE) ||
      (Header->DataSize != NTASI_GPU_BACKING_POOL_V1_DATA_SIZE) ||
      (Header->GuestMemoryBase != GuestMemoryBase) ||
      (Header->OriginalGuestTop != ExpectedBase + NTASI_GPU_BACKING_POOL_V1_RESERVATION_SIZE) ||
      (Header->PhysicalMemoryTop != PhysicalMemoryTop) ||
      (Header->PageShift != NTASI_GPU_BACKING_POOL_V1_PAGE_SHIFT) ||
      (Header->PageCount != NTASI_GPU_BACKING_POOL_V1_PAGE_COUNT) ||
      (Header->ChipId != 0x6020U) || (Header->GpuGeneration != 14U) ||
      (Header->GpuVariant != 'S') || (Header->Reserved0 != 0) ||
      (Header->Reserved1 != 0) ||
      ((Header->Flags & ~NTASI_GPU_BACKING_POOL_KNOWN_FLAGS) != 0) ||
      ((BOOLEAN)(Header->GpuInitdataSize != 0) !=
       (BOOLEAN)((Header->Flags & NTASI_GPU_BACKING_POOL_FLAG_GPU_INITDATA) != 0)) ||
      ((BOOLEAN)(Header->WirelessSize != 0) !=
       (BOOLEAN)((Header->Flags & NTASI_GPU_BACKING_POOL_FLAG_WIRELESS) != 0)) ||
      ((Header->GpuInitdataSize != 0) &&
       ((Header->GpuInitdataBase > MAX_UINT64 - Header->GpuInitdataSize) ||
        (Header->GpuInitdataBase < Header->OriginalGuestTop) ||
        (Header->GpuInitdataBase + Header->GpuInitdataSize > PhysicalMemoryTop))) ||
      ((Header->WirelessSize != 0) &&
       ((Header->WirelessBase > MAX_UINT64 - Header->WirelessSize) ||
        (Header->WirelessBase < Header->OriginalGuestTop) ||
        (Header->WirelessBase + Header->WirelessSize > PhysicalMemoryTop))))
  {
    return FALSE;
  }

  Copy = *Header;
  HeaderCrc = Copy.HeaderCrc32;
  Copy.HeaderCrc32 = 0;
  return (BOOLEAN)((HeaderCrc != 0) &&
                   (NtasiGpuBackingPoolCrc32 (&Copy, sizeof (Copy)) == HeaderCrc));
}

#endif
