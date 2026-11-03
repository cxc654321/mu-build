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

#include <Guid/DebugImageInfoTable.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

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

/**
  Dump every loaded image, by name, at ReadyToBoot.

  "Which module is actually in the machine" kept being answered by guesswork:
  a module can be packaged in the FV and still never be dispatched (depex,
  MODULE_TYPE, order), and per-module DEBUG lines are only as reliable as the
  patch that inserted them. The DXE debug image table is maintained by the core
  itself, so this is the ground truth: base, size and PDB file name for every
  image that was loaded, taken as late as possible before the boot manager runs.
**/
STATIC
BOOLEAN
NameHas (
  IN CONST CHAR8  *Name,
  IN CONST CHAR8  *Needle
  )
{
  UINTN  i;
  UINTN  j;

  if ((Name == NULL) || (Needle == NULL)) {
    return FALSE;
  }

  for (i = 0; Name[i] != 0; i++) {
    for (j = 0; Needle[j] != 0; j++) {
      if (Name[i + j] != Needle[j]) {
        break;
      }
    }

    if (Needle[j] == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Which of the six UI modules are loaded, as six bits.

  Walks the debug image table again rather than remembering names from the
  dump loop, so the answer cannot drift from what was printed.
**/
STATIC
UINTN
ChainPresence (
  VOID
  )
{
  EFI_STATUS                         Status;
  EFI_DEBUG_IMAGE_INFO_TABLE_HEADER  *Header;
  EFI_DEBUG_IMAGE_INFO               *Entry;
  EFI_LOADED_IMAGE_PROTOCOL          *Image;
  UINTN                              Index;
  UINTN                              Bits;
  CHAR8                              *Pdb;

  Bits   = 0;
  Header = NULL;
  Status = EfiGetSystemConfigurationTable (&gEfiDebugImageInfoTableGuid, (VOID **)&Header);
  if (EFI_ERROR (Status) || (Header == NULL) || (Header->EfiDebugImageInfoTable == NULL)) {
    return 0;
  }

  for (Index = 0; Index < Header->TableSize; Index++) {
    Entry = &Header->EfiDebugImageInfoTable[Index];
    if ((Entry->NormalImage == NULL) || (Entry->NormalImage->LoadedImageProtocolInstance == NULL)) {
      continue;
    }

    Image = Entry->NormalImage->LoadedImageProtocolInstance;
    Pdb   = NULL;
    if (*(UINT16 *)(UINTN)Image->ImageBase == 0x5A4D) {
      Pdb = PeCoffLoaderGetPdbPointer (Image->ImageBase);
    }

    if (Pdb == NULL) {
      continue;
    }

    if (NameHas (Pdb, "RenderingEngine")) {
      Bits |= BIT0;
    }

    if (NameHas (Pdb, "SimpleWindowManager")) {
      Bits |= BIT1;
    }

    if (NameHas (Pdb, "OnScreenKeyboard")) {
      Bits |= BIT2;
    }

    if (NameHas (Pdb, "MsUiTheme")) {
      Bits |= BIT3;
    }

    if (NameHas (Pdb, "DisplayEngine")) {
      Bits |= BIT4;
    }

    if (NameHas (Pdb, "FrontPage")) {
      Bits |= BIT5;
    }
  }

  return Bits;
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                         Status;
  EFI_DEBUG_IMAGE_INFO_TABLE_HEADER  *Header;
  EFI_DEBUG_IMAGE_INFO               *Entry;
  EFI_LOADED_IMAGE_PROTOCOL          *Image;
  UINTN                              Index;
  UINTN                              Loaded;
  CHAR8                              *Pdb;
  CHAR8                              *Name;
  UINTN                              Length;

  Header = NULL;
  Status = EfiGetSystemConfigurationTable (&gEfiDebugImageInfoTableGuid, (VOID **)&Header);
  if (EFI_ERROR (Status) || (Header == NULL) || (Header->EfiDebugImageInfoTable == NULL)) {
    DEBUG ((DEBUG_ERROR, "LOADED: no debug image table\n"));
    return;
  }

  Loaded = 0;
  DEBUG ((DEBUG_ERROR, "LOADED: dumping %d debug image slots\n", (UINT32)Header->TableSize));

  for (Index = 0; Index < Header->TableSize; Index++) {
    Entry = &Header->EfiDebugImageInfoTable[Index];
    if ((Entry->NormalImage == NULL) || (Entry->NormalImage->LoadedImageProtocolInstance == NULL)) {
      continue;
    }

    Image = Entry->NormalImage->LoadedImageProtocolInstance;
    Pdb   = NULL;

    if (*(UINT16 *)(UINTN)Image->ImageBase == 0x5A4D) {
      Pdb = PeCoffLoaderGetPdbPointer (Image->ImageBase);
    }

    Name = Pdb;
    if (Name != NULL) {
      CHAR8  *Walk;

      for (Walk = Name; *Walk != 0; Walk++) {
        if ((*Walk == '\\') || (*Walk == '/')) {
          Name = Walk + 1;
        }
      }
    }

    Length = 0;
    if (Name != NULL) {
      while ((Name[Length] != 0) && (Length < 40)) {
        Length++;
      }
    }

    if (Length > 0) {
      DEBUG ((
        DEBUG_ERROR,
        "LOADED: %a base 0x%lx size 0x%lx\n",
        Name,
        (UINT64)(UINTN)Image->ImageBase,
        (UINT64)Image->ImageSize
        ));
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "LOADED: (unnamed) base 0x%lx size 0x%lx\n",
        (UINT64)(UINTN)Image->ImageBase,
        (UINT64)Image->ImageSize
        ));
    }

    Loaded++;
  }

  DEBUG ((DEBUG_ERROR, "LOADED: %d images\n", (UINT32)Loaded));

  //
  // One line that answers the whole question, because the per-image lines above
  // have scrolled the screen several times over by now.
  //
  {
    UINTN  Chain;

    Chain = ChainPresence ();
    DEBUG ((DEBUG_ERROR, "UI CHAIN: SRE=%d SWM=%d OSK=%d THEME=%d DE=%d FP=%d\n",
            (Chain & BIT0) ? 1 : 0, (Chain & BIT1) ? 1 : 0, (Chain & BIT2) ? 1 : 0,
            (Chain & BIT3) ? 1 : 0, (Chain & BIT4) ? 1 : 0, (Chain & BIT5) ? 1 : 0));
  }

  //
  // BDS arms the watchdog per boot option; keep it disabled for bring-up.
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  (VOID)Event;
  (VOID)Context;
}

EFI_STATUS
EFIAPI
J313MemMapDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  PointSpEl0AtOurStack ();

  //
  // Disarm the platform watchdog for the whole boot.
  //
  // DxeCore arms the architectural watchdog from
  // PcdWatchdogTimerDefault (300 s) and BDS re-arms it per boot option. Five
  // minutes is shorter than a bring-up session and, because this platform's
  // HwResetSystemLib only reports a reset request instead of performing one,
  // the expiry leaves the machine parked mid-flow:
  //
  //   WATCHDOG TIMER RESETTING SYSTEM
  //
  // There is nothing to protect here: no OS is running and an unattended hang
  // is visible on the panel anyway. Turn it off once, and again at ReadyToBoot
  // in case BDS set it back.
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  ReportMemoryMap ();
  ReportMemoryHobs ();

  //
  // Register late, when BDS has loaded everything it is going to load.
  //
  {
    EFI_EVENT  Event;

    Event  = NULL;
    Status = gBS->CreateEventEx (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    OnReadyToBoot,
                    NULL,
                    &gEfiEventReadyToBootGuid,
                    &Event
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "LOADED: cannot register ReadyToBoot (%r)\n", Status));
    }
  }

  return EFI_SUCCESS;
}
