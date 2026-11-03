/** @file J313 boot-time GOP filter transaction.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/RenderingEngine.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC EFI_GUID mRenderingEngineFile = {
  0x02aa389e, 0xca29, 0x43bf, { 0xbe, 0x05, 0xa4, 0xca, 0x03, 0x88, 0x12, 0xf2 }
};

STATIC
EFI_STATUS
FindRenderingEngine (OUT EFI_DRIVER_BINDING_PROTOCOL **Binding)
{
  EFI_HANDLE                   *Handles = NULL;
  UINTN                        Count;
  UINTN                        Index;
  EFI_STATUS                   Status;
  EFI_DRIVER_BINDING_PROTOCOL  *Candidate;
  EFI_LOADED_IMAGE_PROTOCOL    *Image;
  EFI_DEVICE_PATH_PROTOCOL     *Node;
  EFI_GUID                     *File;

  *Binding = NULL;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiDriverBindingProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDriverBindingProtocolGuid,
                                  (VOID **)&Candidate);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = gBS->HandleProtocol (Candidate->ImageHandle, &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Image);
    if (EFI_ERROR (Status) || (Image->FilePath == NULL)) {
      continue;
    }
    for (Node = Image->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
      File = EfiGetNameGuidFromFwVolDevicePathNode ((MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Node);
      if ((File != NULL) && CompareGuid (File, &mRenderingEngineFile)) {
        if (*Binding != NULL) {
          FreePool (Handles);
          return EFI_UNSUPPORTED;
        }
        *Binding = Candidate;
        break;
      }
    }
  }
  FreePool (Handles);
  return (*Binding == NULL) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
J313SreHandoffEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS                    Status;
  EFI_STATUS                    Cleanup;
  EFI_STATUS                    Restore;
  EFI_HANDLE                    *Handles = NULL;
  EFI_HANDLE                    Controller;
  UINTN                         Count;
  EFI_DRIVER_BINDING_PROTOCOL   *Binding;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Original;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Replacement;
  VOID                          *Sre;
  EFI_GUID                      *Override;
  BOOLEAN                       OverrideInstalled = FALSE;
  BOOLEAN                       StartAttempted = FALSE;

  // Resolve every prerequisite before touching the early, usable SimpleFb GOP.
  Status = FindRenderingEngine (&Binding);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  if (Count != 1) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }
  Controller = Handles[0];
  Status = gBS->HandleProtocol (Controller, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Original);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  Override = PcdGetPtr (PcdMsGopOverrideProtocolGuid);
  if (!EFI_ERROR (gBS->HandleProtocol (Controller, Override, &Sre)) ||
      !EFI_ERROR (gBS->HandleProtocol (Controller, &gMsSREProtocolGuid, &Sre))) {
    Status = EFI_ALREADY_STARTED;
    goto Exit;
  }

  // APRIORI places this transaction before console drivers. No notify callback.
  Status = gBS->UninstallProtocolInterface (Controller, &gEfiGraphicsOutputProtocolGuid, Original);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  Status = gBS->InstallProtocolInterface (&Controller, Override, EFI_NATIVE_INTERFACE, Original);
  if (EFI_ERROR (Status)) {
    goto Rollback;
  }
  OverrideInstalled = TRUE;
  // Call exactly the audited SRE binding; ConnectController may try other drivers.
  Status = Binding->Supported (Binding, Controller, NULL);
  if (EFI_ERROR (Status)) {
    goto Rollback;
  }
  StartAttempted = TRUE;
  Status = Binding->Start (Binding, Controller, NULL);
  if (EFI_ERROR (Status)) {
    goto Rollback;
  }
  Status = gBS->HandleProtocol (Controller, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Replacement);
  if (!EFI_ERROR (Status) && (Replacement != Original)) {
    Status = gBS->HandleProtocol (Controller, &gMsSREProtocolGuid, &Sre);
    if (!EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "J313 SRE: committed GOP+SRE on %p\n", Controller));
      goto Exit;
    }
  }
  Status = EFI_DEVICE_ERROR;

Rollback:
  DEBUG ((DEBUG_ERROR, "J313 SRE: handoff failed %r; restoring SimpleFb\n", Status));
  if (StartAttempted) {
    // Start can fail *after* publishing both protocols (CreateEvent/SetTimer).
    if (!EFI_ERROR (gBS->HandleProtocol (Controller, &gMsSREProtocolGuid, &Sre)) ||
        !EFI_ERROR (gBS->HandleProtocol (Controller, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Replacement))) {
      Cleanup = Binding->Stop (Binding, Controller, 0, NULL);
      if (EFI_ERROR (Cleanup)) {
        // Never detach a parent still referenced by the published wrapper.
        DEBUG ((DEBUG_ERROR, "J313 SRE: Stop failed %r; retaining live wrapper\n", Cleanup));
        Status = Cleanup;
        goto Exit;
      }
    } else {
      // Failed Start before publication leaves the upstream BY_DRIVER open.
      Cleanup = gBS->CloseProtocol (Controller, Override, Binding->DriverBindingHandle, Controller);
      DEBUG ((DEBUG_INFO, "J313 SRE: failed-start CloseProtocol %r\n", Cleanup));
    }
  }
  if (OverrideInstalled) {
    Cleanup = gBS->UninstallProtocolInterface (Controller, Override, Original);
    if (EFI_ERROR (Cleanup)) {
      DEBUG ((DEBUG_ERROR, "J313 SRE: override rollback failed %r\n", Cleanup));
    }
  }
  Restore = gBS->InstallProtocolInterface (&Controller, &gEfiGraphicsOutputProtocolGuid,
                                          EFI_NATIVE_INTERFACE, Original);
  DEBUG ((DEBUG_ERROR, "J313 SRE: original GOP restore %r\n", Restore));
  if (EFI_ERROR (Restore)) {
    Status = Restore;
  }
Exit:
  if (Handles != NULL) {
    FreePool (Handles);
  }
  DEBUG ((DEBUG_INFO, "J313 SRE: transaction exit %r\n", Status));
  return Status;
}
