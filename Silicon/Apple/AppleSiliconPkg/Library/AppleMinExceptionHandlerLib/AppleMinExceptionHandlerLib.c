/** @file
  A DefaultExceptionHandler that cannot fail, and that names the module.

  The stock ArmPkg implementation walks the EFI debug image table, formats PDB
  paths and prints a full register dump. On this machine it took an exception
  and then tripped its own ASSERT, so whatever the original fault was got
  buried under the handler's second one.

  This one prints five numbers plus, if it can, which loaded image ELR falls
  inside. It reads the debug image table that the DXE core already maintains
  (gST->ConfigurationTable, no allocation, no PE/COFF parsing) and takes the
  file name off the end of the PDB path. Everything is bounded: an exception
  handler that faults again is worse than one that prints less.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/DebugImageInfoTable.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Library/UefiLib.h>

#include <Protocol/DebugSupport.h>

#define HANDLER_NAME_MAX  48

/**
  Print "MOD <file name> +<offset>" when ELR lands inside a loaded image.

  Deliberately not using PeCoffGetSizeOfHeaders() or the PDB loader: the
  previous handler died somewhere in that direction. The table already carries
  ImageBase and ImageSize, which is all a name lookup needs.
**/
STATIC
VOID
ReportImageFor (
  IN UINT64  Address,
  IN UINT64  FaultAddress
  )
{
  EFI_STATUS                         Status;
  EFI_DEBUG_IMAGE_INFO_TABLE_HEADER  *Header;
  EFI_DEBUG_IMAGE_INFO               *Entry;
  UINTN                              Index;
  EFI_LOADED_IMAGE_PROTOCOL          *Image;
  CHAR8                              *Pdb;
  CHAR8                              *Name;
  CHAR8                              Buffer[HANDLER_NAME_MAX + 1];
  UINTN                              Length;

  Header = NULL;
  Status = EfiGetSystemConfigurationTable (&gEfiDebugImageInfoTableGuid, (VOID **)&Header);
  if (EFI_ERROR (Status) || (Header == NULL) || (Header->EfiDebugImageInfoTable == NULL)) {
    DEBUG ((DEBUG_ERROR, "MOD  (no debug image table)\n"));
    return;
  }

  for (Index = 0; Index < Header->TableSize; Index++) {
    Entry = &Header->EfiDebugImageInfoTable[Index];
    if (Entry->NormalImage == NULL) {
      continue;
    }

    Image = Entry->NormalImage->LoadedImageProtocolInstance;
    if (Image == NULL) {
      continue;
    }

    if ((Address < (UINT64)(UINTN)Image->ImageBase)) {
      continue;
    }

    if (Address >= ((UINT64)(UINTN)Image->ImageBase + (UINT64)Image->ImageSize)) {
      continue;
    }

    //
    // The PDB path is where the module's own name lives. Guard the PE header
    // before trusting anything at ImageBase -- an image in a bad state is
    // exactly the sort of thing an exception handler gets called for.
    //
    Pdb = NULL;
    if (*(UINT16 *)(UINTN)Image->ImageBase == 0x5A4D) {
      Pdb = PeCoffLoaderGetPdbPointer (Image->ImageBase);
    }

    //
    // Prefer the PDB path (it carries the module's own name); fall back to the
    // image base description. Either way keep it bounded.
    //
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
      while ((Name[Length] != 0) && (Length < HANDLER_NAME_MAX)) {
        Buffer[Length] = Name[Length];
        Length++;
      }
    }

    Buffer[Length] = 0;

    DEBUG ((
      DEBUG_ERROR,
      "MOD  %a +0x%lx (base 0x%lx size 0x%lx)\n",
      (Length > 0) ? Buffer : "(unnamed)",
      Address - (UINT64)(UINTN)Image->ImageBase,
      (UINT64)(UINTN)Image->ImageBase,
      (UINT64)Image->ImageSize
      ));

    //
    // If FAR looks like an address in this image with its top bits missing,
    // say so outright: it distinguishes "wrote to an unmapped page" from
    // "a pointer lost its high half on the way to a store".
    //
    if ((FaultAddress != 0) && (FaultAddress < (UINT64)(UINTN)Image->ImageBase)) {
      UINT64  Rebuilt;

      Rebuilt = (UINT64)(UINTN)Image->ImageBase | (FaultAddress & 0xFFFFFFFFull);
      if ((Rebuilt >= (UINT64)(UINTN)Image->ImageBase) &&
          (Rebuilt < ((UINT64)(UINTN)Image->ImageBase + (UINT64)Image->ImageSize)))
      {
        DEBUG ((
          DEBUG_ERROR,
          "FAR  0x%lx is inside this image if the high bits were kept: 0x%lx (+0x%lx)\n",
          FaultAddress,
          Rebuilt,
          Rebuilt - (UINT64)(UINTN)Image->ImageBase
          ));
      }
    }

    return;
  }

  DEBUG ((DEBUG_ERROR, "MOD  (no image contains ELR)\n"));
}

VOID
DefaultExceptionHandler (
  IN     EFI_EXCEPTION_TYPE  ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_AARCH64  *Context;

  Context = SystemContext.SystemContextAArch64;

  DEBUG ((DEBUG_ERROR, "\n##### CPU EXCEPTION #####\n"));
  DEBUG ((DEBUG_ERROR, "TYPE %d\n", (UINT32)ExceptionType));

  if (Context == NULL) {
    DEBUG ((DEBUG_ERROR, "NO CONTEXT\n"));
    CpuDeadLoop ();
  }

  DEBUG ((DEBUG_ERROR, "ELR  0x%lx\n", Context->ELR));
  DEBUG ((DEBUG_ERROR, "ESR  0x%lx\n", Context->ESR));
  DEBUG ((DEBUG_ERROR, "FAR  0x%lx\n", Context->FAR));
  DEBUG ((DEBUG_ERROR, "SPSR 0x%lx\n", Context->SPSR));

  DEBUG ((DEBUG_ERROR, "EC   0x%lx ISS 0x%lx\n", (Context->ESR >> 26) & 0x3F, Context->ESR & 0x1FFFFFF));

  ReportImageFor (Context->ELR, Context->FAR);

  DEBUG ((DEBUG_ERROR, "##### HALTED #####\n"));
  CpuDeadLoop ();
}
