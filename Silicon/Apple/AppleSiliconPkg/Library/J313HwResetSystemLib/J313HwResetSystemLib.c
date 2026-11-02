/** @file
  HwResetSystemLib for Apple Silicon: refuse the reset instead of faulting.

  ArmPsciResetSystemLib implements ResetWarm() as a PSCI SYSTEM_RESET2 call
  through ArmMonitorLib (SMC). On this machine there is no PSCI monitor, so the
  SMC turns into an instruction the CPU does not implement and the machine dies
  with "EC 0x00 Unknown reason", ELR inside ResetSystemRuntimeDxe:

    BdsDxe -> VariableDxe asks for a warm reset (memory type information
    changed) -> ResetSystemRuntimeDxe -> ResetWarm() -> ArmMonitorCall -> dead

  For bring-up the useful behaviour is to say so and stop, not to execute an
  undefined instruction. Nothing later depends on the reset actually happening.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

STATIC
VOID
Refuse (
  IN CONST CHAR8  *Which
  )
{
  DEBUG ((DEBUG_ERROR, "\n##### RESET UNSUPPORTED (%a) #####\n", Which));
  DEBUG ((DEBUG_ERROR, "No PSCI monitor on this machine; halting instead of faulting.\n"));
  DEBUG ((DEBUG_ERROR, "##### HALTED #####\n"));
  CpuDeadLoop ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  Refuse ("cold");
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  Refuse ("warm");
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  Refuse ("shutdown");
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData OPTIONAL
  )
{
  (VOID)DataSize;
  (VOID)ResetData;
  Refuse ("platform-specific");
}

RETURN_STATUS
EFIAPI
J313HwResetSystemLibConstructor (
  VOID
  )
{
  return RETURN_SUCCESS;
}
