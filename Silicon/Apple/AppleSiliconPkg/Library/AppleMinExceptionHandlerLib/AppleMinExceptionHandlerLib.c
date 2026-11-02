/** @file
  A DefaultExceptionHandler that cannot fail.

  The stock ArmPkg implementation walks the EFI debug image table, formats PDB
  paths and prints a full register dump. On this machine it took an exception
  and then tripped its own ASSERT, so whatever the original fault was got
  buried under the handler's second one.

  All this needs to do is put four numbers on the panel. ELR is the one that
  matters: it falls between two "Loading driver at 0x..." lines in the
  dispatcher log, which names the module. ESR says what kind of abort, FAR says
  which address.

  No image lookup, no device paths, no allocations, no Unicode conversion --
  DEBUG() straight into the framebuffer and stop.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#include <Protocol/DebugSupport.h>

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

  if (Context != NULL) {
    DEBUG ((DEBUG_ERROR, "ELR  0x%lx\n", Context->ELR));
    DEBUG ((DEBUG_ERROR, "ESR  0x%lx\n", Context->ESR));
    DEBUG ((DEBUG_ERROR, "FAR  0x%lx\n", Context->FAR));
    DEBUG ((DEBUG_ERROR, "SPSR 0x%lx\n", Context->SPSR));
  } else {
    DEBUG ((DEBUG_ERROR, "NO CONTEXT\n"));
  }

  DEBUG ((DEBUG_ERROR, "##### HALTED #####\n"));
  CpuDeadLoop ();
}
