/*
 * Copyright (c) 2015, Linaro Ltd. All rights reserved.
 *
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/DebugLib.h>
#include <Library/AppleDTLib.h>

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))

// Captured before CEntryPoint so PrePi can report progress directly on the
// already-initialized Apple framebuffer even when both serial paths are quiet.
volatile UINT64 gMuFrameBufferBase;
volatile UINT32 gMuFrameBufferStride;
volatile UINT32 gMuFrameBufferWidth;
volatile UINT32 gMuFrameBufferHeight;

BOOLEAN
EarlySetup (
  IN VOID     *BootArgsAddr,
  OUT UINT64  *SystemMemoryBase,
  OUT UINT64  *SystemMemorySize,
  IN    VOID  *PcdBootArgsDest,
  IN    VOID  *PcdAdtDest
  )
{

  struct boot_args *BootArgs = (struct boot_args *)BootArgsAddr;
  VOID             *AdtSource;

  AdtSource = (VOID *)((UINTN)BootArgs->devtree -
                       (UINTN)BootArgs->virt_base +
                       (UINTN)BootArgs->phys_base);

  DEBUG ((
    DEBUG_ERROR,
    "J813 EARLY: boot_args=%p boot_dest=%p adt_src=%p adt_dest=%p adt_size=0x%x\n",
    BootArgsAddr,
    PcdBootArgsDest,
    AdtSource,
    PcdAdtDest,
    BootArgs->devtree_size
    ));

  CopyMem(PcdBootArgsDest, BootArgsAddr, sizeof(struct boot_args));
  DEBUG ((DEBUG_ERROR, "J813 EARLY: boot args copied\n"));
  CopyMem(PcdAdtDest, AdtSource, BootArgs->devtree_size);
  DEBUG ((DEBUG_ERROR, "J813 EARLY: ADT copied\n"));

  if(dt_check(PcdAdtDest, BootArgs->devtree_size, NULL) != 0) {
    DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "no ADT supplied, exiting\n"));
    return FALSE;
  }
  DEBUG ((DEBUG_ERROR, "J813 EARLY: ADT validated\n"));

  *SystemMemoryBase = BootArgs->phys_base;
  *SystemMemorySize = BootArgs->phys_base + BootArgs->mem_size - BootArgs->phys_base;


  PatchPcdSet64(PcdFrameBufferAddress, BootArgs->video.base);
  PatchPcdSet64(PcdFrameBufferSize, ALIGN_UP(BootArgs->video.stride * BootArgs->video.height, 0x4000)); // for notched/internal displays, the FB size has to be aligned up!

  gMuFrameBufferBase   = BootArgs->video.base;
  gMuFrameBufferStride = BootArgs->video.stride;
  gMuFrameBufferWidth  = BootArgs->video.width;
  gMuFrameBufferHeight = BootArgs->video.height;

  DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "Framebuffer address loaded to PCDs: 0x%llx (size 0x%llx)\n", PcdGet64(PcdFrameBufferAddress), PcdGet64(PcdFrameBufferSize)));

  return TRUE;
}
