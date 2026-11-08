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
    DEBUG ((DEBUG_ERROR, "J313 stage=%u status=%lx valid=%x last=%u/%lx\n", D->Stage, D->Status, D->Valid, D->LastFailureStage, D->LastFailureStatus));
    /* At most seven lines. Missing fields use ffffffff, never apparent success. */
    DEBUG ((DEBUG_ERROR, "Resource=%x id=%x bus=%x (ffffffff=? id=BYPASS)\n",
      (D->Valid & J313_DIAG_RESOURCE_VALID) ? D->ResourceCode : MAX_UINT32,
      (D->Valid & J313_DIAG_IDENTITY_VALID) ? D->IdentityMask : MAX_UINT32,
      (D->Valid & J313_DIAG_BUS_VALID) ? D->BusCode : MAX_UINT32));
    if (D->Valid & J313_DIAG_PMGR_VALID)
      DEBUG ((DEBUG_ERROR, "PMGR e=%x s=%x gate=%x id=%x bits=%u\n", D->PmgrReason, D->PmgrSource, D->PmgrGate, D->PmgrId, D->PmgrIdWidth));
    else DEBUG ((DEBUG_ERROR, "PMGR=UNKNOWN\n"));
    if (D->Valid & J313_DIAG_HID_VALID) {
      DEBUG ((DEBUG_ERROR, "HID op=%u ph=%u dom=%u err=%d bus=%u rej=%u/%x\n", D->HidOp, D->HidPhase, D->HidDomain, D->HidDetail, D->HidBus, D->HidReject, D->HidRejectMask));
      if (D->HidRejectIdValid)
        DEBUG ((DEBUG_ERROR, "HID p/r/rx/tx/d=%x/%x/%x/%x/%x ID e/a=%02x/%02x t/i=%02x/%02x\n", D->HidPolls, D->HidReady, D->HidReads, D->HidWrites, D->HidDrops, D->HidExpectedId, D->HidActualId, D->HidRequestType, D->HidRequestIface));
      else
        DEBUG ((DEBUG_ERROR, "HID p/r/rx/tx/d=%x/%x/%x/%x/%x ID=n/a\n", D->HidPolls, D->HidReady, D->HidReads, D->HidWrites, D->HidDrops));
      DEBUG ((DEBUG_ERROR, "PIO e=%u seg=%u q/t/r=%lx/%lx/%lx f=%x ack=%08x\n", D->HidPioStatus, D->HidPioSegments, D->HidTxQueued, D->HidTxCompleted, D->HidRxCompleted, D->HidPioFlags, D->HidAck));
    } else {
      DEBUG ((DEBUG_ERROR, "HID=UNKNOWN (not started)\n"));
      if (D->Valid & J313_DIAG_PMGR_VALID)
        DEBUG ((DEBUG_ERROR, "PMGR rec=%x len=%x/%x/%x ps/reg=%x/%x\n", D->PmgrRecordIndex, D->PmgrGateLength, D->PmgrDevicesLength, D->PmgrPsLength, D->PmgrPsIndex, D->PmgrRegIndex));
    }
    if (D->Valid & J313_DIAG_CLEANUP_VALID) DEBUG ((DEBUG_ERROR, "Cleanup=%lx; menu in 30s\n", D->CleanupStatus));
    else DEBUG ((DEBUG_ERROR, "Cleanup=UNKNOWN; menu in 30s\n"));
  }
  gBS->Stall (30000000);
}
