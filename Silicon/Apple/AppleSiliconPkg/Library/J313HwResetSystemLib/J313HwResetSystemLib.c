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

//
// Say out loud that this library is in the image: if even this line does not
// show up, the DSC mapping never took effect and nothing here is reachable.
//
STATIC UINT32  mResetRequests = 0;

STATIC
VOID
Refuse (
  IN CONST CHAR8  *Which
  )
{
  mResetRequests++;
  //
  // Refuse the reset and hand control back to the caller.
  //
  // Halting here stopped the crash (the PSCI SMC was an undefined instruction)
  // but also stopped the boot: BdsDxe asks for a warm reset whenever the memory
  // type information it computes differs from what it started with, and with
  // emulated variables (PcdEmuVariableNvModeEnable = TRUE) that record never
  // survives a reboot, so the request repeats forever. The values cannot
  // converge, and this tree has no PcdMemoryTypeInformation to seed either --
  // it is not declared in any DEC here.
  //
  // Returning lets BDS carry on in the same boot. A reset that does not happen
  // costs one reboot's worth of state at worst; the alternative is never
  // reaching the boot manager at all.
  //
  DEBUG ((DEBUG_ERROR, "\nHWRS: %a requested (#%d) -- IGNORED, CONTINUING\n", Which, (UINT32)mResetRequests));
  DEBUG ((DEBUG_ERROR, "No PSCI monitor here; see J313HwResetSystemLib.\n"));
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
  DEBUG ((DEBUG_ERROR, "HWRS: J313HwResetSystemLib constructor ran\n"));
  return RETURN_SUCCESS;
}
