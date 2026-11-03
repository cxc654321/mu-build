/** @file
  Phase0 safety placeholder, NOT a usable keyboard or pointer driver.

  No board mapping or transport protocol has been validated. This entry only
  emits fixed diagnostics; EFI_SUCCESS means the placeholder returned normally,
  not that a transport or input device was initialized.

  The previous implementation is quarantined as non-deployable reference in
  ../artifacts/spihid-quarantine-a26de9a (relative to the workspace root).
  Its Linux GPL provenance remains subject to audit; this replacement does not
  relicense or validate that implementation.
**/

#include <Uefi.h>
#include <Library/DebugLib.h>

EFI_STATUS
EFIAPI
AppleSpiHidDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  (VOID)ImageHandle;
  (VOID)SystemTable;

  DEBUG ((DEBUG_ERROR, "J313-P0-20261104-r1 SPI-ENTRY\n"));
  DEBUG ((DEBUG_ERROR, "transport disabled pending validated board mapping and protocol\n"));

  return EFI_SUCCESS;
}
