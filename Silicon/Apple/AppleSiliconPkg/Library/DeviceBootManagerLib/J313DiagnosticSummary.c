/** Read-only boot snapshot photo summary; no driver pointers or key input. */
#include <Uefi.h>
#include <Guid/J313BootDiagnostic.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

VOID J313DiagnosticSummary (VOID)
{
  EFI_GUID Guid = J313_BOOT_DIAGNOSTIC_GUID;
  J313_BOOT_DIAGNOSTIC *D = NULL;
  J313_BOOT_DIAGNOSTIC Copy;
  UINTN I;
  EFI_TPL OldTpl;
  if (EfiGetCurrentTpl () != TPL_APPLICATION) return;
  /* Poll runs at NOTIFY. Copy atomically, then restore before console/Stall. */
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (I = 0; I < gST->NumberOfTableEntries; ++I) {
    if (CompareGuid (&gST->ConfigurationTable[I].VendorGuid, &Guid)) {
      D = gST->ConfigurationTable[I].VendorTable;
      break;
    }
  }
  if (D != NULL && D->Version == J313_DIAG_VERSION && D->Size == sizeof (Copy)) {
    Copy = *D;
    D = &Copy;
  } else D = NULL;
  gBS->RestoreTPL (OldTpl);
  if (D == NULL) {
    DEBUG ((DEBUG_ERROR, "J313 snapshot UNKNOWN (missing/version/size); menu in 30s\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "J313 stage=%u status=%lx valid=%x\n", D->Stage, D->Status, D->Valid));
    if (D->Valid & J313_DIAG_RESOURCE_VALID) DEBUG ((DEBUG_ERROR, "Resource code=%u (1 struct 2 id 3 reg 4 pmgr 5 alloc)\n", D->ResourceCode));
    else DEBUG ((DEBUG_ERROR, "Resource code=UNKNOWN\n"));
    if (D->Valid & J313_DIAG_IDENTITY_VALID) DEBUG ((DEBUG_ERROR, "Identity mask=%x (BYPASS; not verified)\n", D->IdentityMask));
    else DEBUG ((DEBUG_ERROR, "Identity mask=UNKNOWN\n"));
    if (D->Valid & J313_DIAG_BUS_VALID) DEBUG ((DEBUG_ERROR, "SPI init buscode=%u\n", D->BusCode));
    else DEBUG ((DEBUG_ERROR, "SPI init buscode=UNKNOWN\n"));
    DEBUG ((DEBUG_ERROR, "Last failure stage=%u status=%lx\n", D->LastFailureStage, D->LastFailureStatus));
    if (D->Valid & J313_DIAG_CLEANUP_VALID) DEBUG ((DEBUG_ERROR, "Cleanup=%lx; menu in 30s\n", D->CleanupStatus));
    else DEBUG ((DEBUG_ERROR, "Cleanup=UNKNOWN; menu in 30s\n"));
  }
  gBS->Stall (30000000);
}
