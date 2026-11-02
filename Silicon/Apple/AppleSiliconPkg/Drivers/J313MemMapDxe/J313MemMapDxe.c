/** @file
  J313MemMapDxe: say, on the panel, what memory DXE actually got.

  The boot stops after the color bars with pool allocations failing, and the
  two candidate explanations point at different places:

    - PrePi published memory HOBs that do not describe the machine (m1n1's map
      read wrongly, a carveout swallowing DRAM, the video carveout missing), so
      DxeCore has almost nothing to hand out, or
    - the map is fine and something downstream asks for an unreasonable size.

  Both are answered by printing the memory map and the resource-descriptor HOBs
  once, at driver entry, before anything has consumed memory.

  Read-only, no allocation (the buffers are static), depex TRUE.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Pi/PiHob.h>

#define J313_MAX_DESCRIPTORS  1024
#define J313_REPORT_MIN_PAGES 0x100      // report regions of at least 1 MiB

STATIC EFI_MEMORY_DESCRIPTOR  mDescriptors[J313_MAX_DESCRIPTORS];

STATIC CONST CHAR8 *
MemTypeName (
  IN EFI_MEMORY_TYPE  Type
  )
{
  switch (Type) {
    case EfiReservedMemoryType:      return "RSVD";
    case EfiLoaderCode:              return "LDCD";
    case EfiLoaderData:              return "LDDT";
    case EfiBootServicesCode:        return "BSCD";
    case EfiBootServicesData:        return "BSDT";
    case EfiRuntimeServicesCode:     return "RTCD";
    case EfiRuntimeServicesData:     return "RTDT";
    case EfiConventionalMemory:      return "FREE";
    case EfiUnusableMemory:          return "BAD ";
    case EfiACPIReclaimMemory:       return "ACPI";
    case EfiACPIMemoryNVS:           return "NVS ";
    case EfiMemoryMappedIO:          return "MMIO";
    case EfiMemoryMappedIOPortSpace: return "MMIO";
    case EfiPalCode:                 return "PAL ";
    case EfiPersistentMemory:        return "PERS";
    default:                         return "??? ";
  }
}

STATIC
CONST CHAR8 *
ResourceTypeName (
  IN EFI_RESOURCE_TYPE  Type
  )
{
  switch (Type) {
    case EFI_RESOURCE_SYSTEM_MEMORY:          return "SYS ";
    case EFI_RESOURCE_MEMORY_MAPPED_IO:       return "MMIO";
    case EFI_RESOURCE_IO:                     return "IO  ";
    case EFI_RESOURCE_FIRMWARE_DEVICE:        return "FW  ";
    case EFI_RESOURCE_MEMORY_MAPPED_IO_PORT:  return "MMP ";
    case EFI_RESOURCE_MEMORY_RESERVED:        return "RSVD";
    case EFI_RESOURCE_IO_RESERVED:            return "IOR ";
    default:                                  return "??? ";
  }
}

STATIC
VOID
ReportMemoryMap (
  VOID
  )
{
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  EFI_STATUS             Status;
  UINTN                  Index;
  UINTN                  Count;
  EFI_MEMORY_DESCRIPTOR  *Descriptor;
  UINT64                 FreePages;
  UINT64                 LargestFree;
  UINT64                 TotalPages;

  Size              = sizeof (mDescriptors);
  DescriptorSize    = 0;
  DescriptorVersion = 0;
  Key               = 0;

  Status = gBS->GetMemoryMap (&Size, mDescriptors, &Key, &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR (Status) || (DescriptorSize == 0)) {
    DEBUG ((DEBUG_ERROR, "MEMMAP: GetMemoryMap failed (%r)\n", Status));
    return;
  }

  Count      = Size / DescriptorSize;
  FreePages  = 0;
  LargestFree = 0;
  TotalPages = 0;

  DEBUG ((DEBUG_ERROR, "MEMMAP: %d descriptors, %d bytes each\n", Count, DescriptorSize));

  for (Index = 0; Index < Count; Index++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mDescriptors + Index * DescriptorSize);
    TotalPages += Descriptor->NumberOfPages;

    if (Descriptor->Type == EfiConventionalMemory) {
      FreePages += Descriptor->NumberOfPages;
      if (Descriptor->NumberOfPages > LargestFree) {
        LargestFree = Descriptor->NumberOfPages;
      }
    }

    if (Descriptor->NumberOfPages >= J313_REPORT_MIN_PAGES) {
      DEBUG ((
        DEBUG_ERROR,
        "  MMAP %a 0x%lx +0x%lx (%ld MB)\n",
        MemTypeName (Descriptor->Type),
        (UINT64)Descriptor->PhysicalStart,
        (UINT64)(Descriptor->NumberOfPages * EFI_PAGE_SIZE),
        (UINT64)Descriptor->NumberOfPages / 256
        ));
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "MEMMAP: free %ld MB, largest free run %ld MB, %ld MB described\n",
    FreePages / 256,
    LargestFree / 256,
    TotalPages / 256
    ));
}

STATIC
VOID
ReportMemoryHobs (
  VOID
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE  *Handoff;
  EFI_HOB_GENERIC_HEADER      *Hob;
  UINTN                       Count;

  Handoff = (EFI_HOB_HANDOFF_INFO_TABLE *)GetHobList ();
  if (Handoff == NULL) {
    DEBUG ((DEBUG_ERROR, "HOB: no handoff table\n"));
    return;
  }

  DEBUG ((
    DEBUG_ERROR,
    "HOB: handoff mem 0x%lx +0x%lx free 0x%lx\n",
    (UINT64)Handoff->EfiMemoryTop,
    (UINT64)(Handoff->EfiMemoryTop - Handoff->EfiMemoryBottom),
    (UINT64)(Handoff->EfiFreeMemoryTop - Handoff->EfiFreeMemoryBottom)
    ));

  Count = 0;
  //
  // Plain pointer arithmetic: GET_NEXT_HOB() takes the address of its argument,
  // so it needs an lvalue -- &Handoff->Header is not one.
  //
  Hob = (EFI_HOB_GENERIC_HEADER *)((UINT8 *)Handoff + Handoff->Header.HobLength);
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      EFI_HOB_RESOURCE_DESCRIPTOR  *Resource;

      Resource = (EFI_HOB_RESOURCE_DESCRIPTOR *)Hob;
      if (Resource->ResourceLength >= (UINT64)J313_REPORT_MIN_PAGES * EFI_PAGE_SIZE) {
        DEBUG ((
          DEBUG_ERROR,
          "  HOB  %a 0x%lx +0x%lx\n",
          ResourceTypeName (Resource->ResourceType),
          (UINT64)Resource->PhysicalStart,
          (UINT64)Resource->ResourceLength
          ));
      }

      Count++;
    }

    Hob = GET_NEXT_HOB (Hob);
  }

  DEBUG ((DEBUG_ERROR, "HOB: %d resource descriptors\n", Count));
}

//
/// The exception vector code that handles "Current EL with SP0" switches to
/// SP_EL0 and stores the register frame there. Nothing in ArmCpuDxe writes
/// SP_EL0 (its disassembly contains no such instruction), so on this machine
/// the vector inherits whatever SP_EL0 held when the firmware started -- and
/// the first synchronous exception dies storing to it:
///
///   ELR = ArmCpuDxe +0x6A08   (stp x0, x1, [sp, #-256]!)
///   FAR = 0x0DDD82E50         (SP_EL0 - 256, high bits missing)
///
/// Give it a real buffer, from our own image, and report what it was before.
///
STATIC UINT8  mEl0Stack[16 * 1024] __attribute__ ((aligned (16)));

STATIC
VOID
PointSpEl0AtOurStack (
  VOID
  )
{
  UINT64  Before;
  UINT64  After;
  UINT64  Top;

  __asm__ volatile ("mrs %0, sp_el0" : "=r" (Before));

  Top = (UINT64)(UINTN)mEl0Stack + sizeof (mEl0Stack);
  Top &= ~(UINT64)0xF;
  __asm__ volatile ("msr sp_el0, %0" : : "r" (Top));

  __asm__ volatile ("mrs %0, sp_el0" : "=r" (After));

  DEBUG ((
    DEBUG_ERROR,
    "EL0STACK: sp_el0 was 0x%lx, now 0x%lx (buffer 0x%lx)\n",
    Before,
    After,
    (UINT64)(UINTN)mEl0Stack
    ));
}

EFI_STATUS
EFIAPI
J313MemMapDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  PointSpEl0AtOurStack ();
  ReportMemoryMap ();
  ReportMemoryHobs ();
  return EFI_SUCCESS;
}
