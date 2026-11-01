/** @file
  Rewrites architectural PMUv3 accesses in Windows ARM64 images for Apple M5.

  T8142 does not expose the architectural PMU register bank to an EL1 guest and
  also does not implement the EL2 trap controls that would normally let m1n1
  emulate those accesses.  Windows probes PMCCNTR_EL0 during boot and treats the
  resulting undefined-instruction exception as fatal once it owns VBAR_EL1.

  Patch loaded PE/COFF images before they run.  Cycle-counter reads use the
  architectural physical timer, PMCR reads report a disabled PMU, and PMU
  control writes become no-ops.  The LoadedImage notification covers UEFI
  applications such as bootmgfw.efi.  The ExitBootServices scan also finds
  winload and ntoskrnl, which the Windows loader maps without installing an EFI
  Loaded Image protocol.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <IndustryStandard/PeImage.h>

#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/LoadedImage.h>

#define WINDOWS_PMU_MEMORY_MAP_BUFFER_SIZE  (512U * 1024U)
#define WINDOWS_PMU_PAGE_SIZE               4096U
#define WINDOWS_PMU_MAX_SECTIONS            128U

#define AARCH64_REGISTER_MASK      0x0000001FU
#define AARCH64_SYSTEM_REG_MASK    0xFFFFFFE0U
#define AARCH64_MRS_PMCCNTR_EL0    0xD53B9D00U
#define AARCH64_MRS_PMCR_EL0       0xD53B9C00U
#define AARCH64_MSR_PMCNTENCLR_EL0 0xD51B9C40U
#define AARCH64_MSR_PMOVSCLR_EL0   0xD51B9C60U
#define AARCH64_MSR_PMINTENCLR_EL1 0xD5189E40U
#define AARCH64_MSR_PMCR_EL0       0xD51B9C00U
#define AARCH64_MSR_PMCCFILTR_EL0  0xD51BEFE0U
#define AARCH64_MSR_PMCNTENSET_EL0 0xD51B9C20U
#define AARCH64_MSR_PMUSERENR_EL0  0xD51B9E00U
#define AARCH64_MRS_CNTPCT_EL0     0xD53BE020U
#define AARCH64_MOV_XD_ZERO        0xD2800000U
#define AARCH64_NOP                0xD503201FU

STATIC EFI_EVENT  mLoadedImageEvent;
STATIC VOID       *mLoadedImageRegistration;
STATIC EFI_EVENT  mExitBootServicesEvent;
STATIC UINT8      mMemoryMapBuffer[WINDOWS_PMU_MEMORY_MAP_BUFFER_SIZE];

STATIC
BOOLEAN
IsScanMemoryType (
  IN EFI_MEMORY_TYPE  Type
  )
{
  return (Type == EfiLoaderCode) ||
         (Type == EfiLoaderData) ||
         (Type == EfiBootServicesCode) ||
         (Type == EfiBootServicesData);
}

STATIC
BOOLEAN
PatchPmuInstruction (
  IN OUT UINT32  *Instruction
  )
{
  UINT32  Opcode;
  UINT32  Register;

  Opcode   = *Instruction & AARCH64_SYSTEM_REG_MASK;
  Register = *Instruction & AARCH64_REGISTER_MASK;

  switch (Opcode) {
    case AARCH64_MRS_PMCCNTR_EL0:
      *Instruction = AARCH64_MRS_CNTPCT_EL0 | Register;
      return TRUE;

    case AARCH64_MRS_PMCR_EL0:
      *Instruction = AARCH64_MOV_XD_ZERO | Register;
      return TRUE;

    case AARCH64_MSR_PMCNTENCLR_EL0:
    case AARCH64_MSR_PMOVSCLR_EL0:
    case AARCH64_MSR_PMINTENCLR_EL1:
    case AARCH64_MSR_PMCR_EL0:
    case AARCH64_MSR_PMCCFILTR_EL0:
    case AARCH64_MSR_PMCNTENSET_EL0:
    case AARCH64_MSR_PMUSERENR_EL0:
      *Instruction = AARCH64_NOP;
      return TRUE;

    default:
      return FALSE;
  }
}

STATIC
UINTN
PatchPeImage (
  IN VOID        *ImageBase,
  IN UINTN       AvailableSize,
  IN CONST CHAR8 *Source
  )
{
  EFI_IMAGE_DOS_HEADER       *DosHeader;
  EFI_IMAGE_NT_HEADERS64     *NtHeader;
  EFI_IMAGE_SECTION_HEADER   *Section;
  UINT8                      *Base;
  UINTN                      NtOffset;
  UINTN                      SectionOffset;
  UINTN                      SectionTableSize;
  UINTN                      ImageSize;
  UINTN                      SectionSize;
  UINTN                      Index;
  UINTN                      Offset;
  UINTN                      Patched;

  if ((ImageBase == NULL) || (AvailableSize < sizeof (EFI_IMAGE_DOS_HEADER))) {
    return 0;
  }

  Base      = ImageBase;
  DosHeader = ImageBase;
  if ((DosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE) ||
      (DosHeader->e_lfanew <= 0)) {
    return 0;
  }

  NtOffset = (UINTN)DosHeader->e_lfanew;
  if ((NtOffset > AvailableSize) ||
      ((AvailableSize - NtOffset) < sizeof (EFI_IMAGE_NT_HEADERS64))) {
    return 0;
  }

  NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(Base + NtOffset);
  if ((NtHeader->Signature != EFI_IMAGE_NT_SIGNATURE) ||
      (NtHeader->FileHeader.Machine != EFI_IMAGE_MACHINE_AARCH64) ||
      (NtHeader->OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) ||
      (NtHeader->FileHeader.NumberOfSections == 0) ||
      (NtHeader->FileHeader.NumberOfSections > WINDOWS_PMU_MAX_SECTIONS) ||
      (NtHeader->FileHeader.SizeOfOptionalHeader < sizeof (EFI_IMAGE_OPTIONAL_HEADER64))) {
    return 0;
  }

  ImageSize = NtHeader->OptionalHeader.SizeOfImage;
  if ((ImageSize == 0) || (ImageSize > AvailableSize)) {
    return 0;
  }

  SectionOffset = NtOffset + sizeof (UINT32) + sizeof (EFI_IMAGE_FILE_HEADER) +
                  NtHeader->FileHeader.SizeOfOptionalHeader;
  SectionTableSize = (UINTN)NtHeader->FileHeader.NumberOfSections *
                     sizeof (EFI_IMAGE_SECTION_HEADER);
  if ((SectionOffset > ImageSize) ||
      (SectionTableSize > (ImageSize - SectionOffset))) {
    return 0;
  }

  Section = (EFI_IMAGE_SECTION_HEADER *)(Base + SectionOffset);
  Patched = 0;

  for (Index = 0; Index < NtHeader->FileHeader.NumberOfSections; Index++) {
    if ((Section[Index].Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0) {
      continue;
    }

    SectionSize = Section[Index].Misc.VirtualSize;
    if (SectionSize == 0) {
      SectionSize = Section[Index].SizeOfRawData;
    }

    if ((SectionSize < sizeof (UINT32)) ||
        ((UINTN)Section[Index].VirtualAddress > ImageSize) ||
        (SectionSize > (ImageSize - (UINTN)Section[Index].VirtualAddress))) {
      continue;
    }

    for (Offset = 0; (Offset + sizeof (UINT32)) <= SectionSize; Offset += sizeof (UINT32)) {
      if (PatchPmuInstruction ((UINT32 *)(Base + Section[Index].VirtualAddress + Offset))) {
        Patched++;
      }
    }

    if (Patched != 0) {
      WriteBackInvalidateDataCacheRange (
        Base + Section[Index].VirtualAddress,
        SectionSize
        );
      InvalidateInstructionCacheRange (
        Base + Section[Index].VirtualAddress,
        SectionSize
        );
    }
  }

  if (Patched != 0) {
    DEBUG ((
      DEBUG_WARN,
      "WindowsPmuCompat: patched %u PMU instructions in %a PE image @ 0x%p (size 0x%lx)\n",
      Patched,
      Source,
      ImageBase,
      (UINT64)ImageSize
      ));
  }

  return Patched;
}

STATIC
VOID
EFIAPI
OnLoadedImageNotification (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE                 Handle;
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  EFI_STATUS                 Status;
  UINTN                      BufferSize;

  (VOID)Event;
  (VOID)Context;

  for (;;) {
    BufferSize = sizeof (Handle);
    Status = gBS->LocateHandle (
                    ByRegisterNotify,
                    NULL,
                    mLoadedImageRegistration,
                    &BufferSize,
                    &Handle
                    );
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = gBS->HandleProtocol (
                    Handle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    if (EFI_ERROR (Status) || (LoadedImage->ImageBase == NULL)) {
      continue;
    }

    PatchPeImage (
      LoadedImage->ImageBase,
      (UINTN)LoadedImage->ImageSize,
      "LoadedImage"
      );
  }
}

STATIC
UINTN
ScanMemoryDescriptor (
  IN CONST EFI_MEMORY_DESCRIPTOR  *Descriptor
  )
{
  UINT8   *Base;
  UINTN   Size;
  UINTN   Offset;
  UINTN   ImageSize;
  UINTN   Patched;

  if (!IsScanMemoryType ((EFI_MEMORY_TYPE)Descriptor->Type) ||
      (Descriptor->NumberOfPages == 0) ||
      (Descriptor->NumberOfPages > (MAX_UINTN / EFI_PAGE_SIZE))) {
    return 0;
  }

  Base = (UINT8 *)(UINTN)Descriptor->PhysicalStart;
  Size = (UINTN)Descriptor->NumberOfPages * EFI_PAGE_SIZE;
  Patched = 0;

  for (Offset = 0;
       (Offset + sizeof (EFI_IMAGE_DOS_HEADER)) <= Size;
       Offset += WINDOWS_PMU_PAGE_SIZE) {
    if (*(volatile UINT16 *)(Base + Offset) != EFI_IMAGE_DOS_SIGNATURE) {
      continue;
    }

    Patched += PatchPeImage (Base + Offset, Size - Offset, "memory-map");

    if ((Size - Offset) >= sizeof (EFI_IMAGE_DOS_HEADER)) {
      EFI_IMAGE_DOS_HEADER    *DosHeader;
      EFI_IMAGE_NT_HEADERS64  *NtHeader;
      UINTN                   NtOffset;

      DosHeader = (EFI_IMAGE_DOS_HEADER *)(Base + Offset);
      NtOffset  = (DosHeader->e_lfanew > 0) ? (UINTN)DosHeader->e_lfanew : 0;
      if ((NtOffset != 0) &&
          (NtOffset <= (Size - Offset)) &&
          (((Size - Offset) - NtOffset) >= sizeof (EFI_IMAGE_NT_HEADERS64))) {
        NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(Base + Offset + NtOffset);
        ImageSize = NtHeader->OptionalHeader.SizeOfImage;
        if ((NtHeader->Signature == EFI_IMAGE_NT_SIGNATURE) &&
            (ImageSize >= WINDOWS_PMU_PAGE_SIZE) &&
            (ImageSize <= (Size - Offset))) {
          Offset += (ImageSize - WINDOWS_PMU_PAGE_SIZE) &
                    ~(WINDOWS_PMU_PAGE_SIZE - 1U);
        }
      }
    }
  }

  return Patched;
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_MEMORY_DESCRIPTOR  *Descriptor;
  EFI_STATUS             Status;
  UINTN                  MemoryMapSize;
  UINTN                  MapKey;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  UINTN                  Offset;
  UINTN                  Patched;

  (VOID)Event;
  (VOID)Context;

  MemoryMapSize = sizeof (mMemoryMapBuffer);
  Status = gBS->GetMemoryMap (
                  &MemoryMapSize,
                  (EFI_MEMORY_DESCRIPTOR *)mMemoryMapBuffer,
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );
  if (EFI_ERROR (Status) || (DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR))) {
    DEBUG ((
      DEBUG_ERROR,
      "WindowsPmuCompat: GetMemoryMap failed at ExitBootServices: %r (buffer 0x%x)\n",
      Status,
      sizeof (mMemoryMapBuffer)
      ));
    return;
  }

  Patched = 0;
  for (Offset = 0; (Offset + DescriptorSize) <= MemoryMapSize; Offset += DescriptorSize) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)(mMemoryMapBuffer + Offset);
    Patched += ScanMemoryDescriptor (Descriptor);
  }

  DEBUG ((
    DEBUG_WARN,
    "WindowsPmuCompat: ExitBootServices scan complete, %u PMU instructions patched\n",
    Patched
    ));
}

EFI_STATUS
EFIAPI
WindowsPmuCompatDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  mLoadedImageEvent = EfiCreateProtocolNotifyEvent (
                        &gEfiLoadedImageProtocolGuid,
                        TPL_CALLBACK,
                        OnLoadedImageNotification,
                        NULL,
                        &mLoadedImageRegistration
                        );
  if (mLoadedImageEvent == NULL) {
    DEBUG ((DEBUG_ERROR, "WindowsPmuCompat: failed to register LoadedImage notification\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "WindowsPmuCompat: failed to register ExitBootServices callback: %r\n", Status));
    gBS->CloseEvent (mLoadedImageEvent);
    return Status;
  }

  DEBUG ((DEBUG_WARN, "WindowsPmuCompat: ARM64 PMU image patching armed\n"));
  return EFI_SUCCESS;
}
