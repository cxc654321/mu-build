/** @file
  J313-only SPI HID keyboard DXE. GPL-2.0-only.
  Framing/initialization derived from Asahi Linux contributors (see Core).
  No hardware-validation claim. Descriptor comes only from correlated bus replies.
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootManagerLib.h>
#include "J313SpiTransport.h"
#include "J313Keyboard.h"
#include "J313BoardResources.h"
#include "Core/j313_session.h"

#define POLL_100NS 50000ULL
#define BOOT_TICKS 1200U
/* User-requested photo diagnostic build only; set to 0/remove to revoke.
   Bounded ten seconds, no key input, new event, or permanent halt. */
#define J313_PHOTO_DELAY_US 10000000

STATIC VOID PhotoDelay (CONST CHAR8 *Stage)
{
  EFI_TPL OldTpl;
  /* Probe only: restore before logging/stalling so timer polling can continue.
     HIGH_LEVEL also permits safe detection when the caller is already elevated. */
  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (OldTpl);
  if (OldTpl != TPL_APPLICATION) {
    DEBUG ((DEBUG_ERROR, "J313-PHOTO %a SKIP tpl=%u (not APPLICATION)\n", Stage, (UINT32)OldTpl));
    return;
  }
  DEBUG ((DEBUG_ERROR, "J313-PHOTO %a WAIT us=%u (diagnostic only)\n", Stage, (UINT32)J313_PHOTO_DELAY_US));
  if (J313_PHOTO_DELAY_US != 0) (VOID)gBS->Stall (J313_PHOTO_DELAY_US);
  DEBUG ((DEBUG_ERROR, "J313-PHOTO %a CONTINUE\n", Stage));
}

typedef struct {
  J313_SPI_TRANSPORT Transport;
  struct j313_session Session;
  J313_KEYBOARD *Keyboard;
  EFI_EVENT Timer;
  EFI_EVENT ExitEvent;
  BOOLEAN Stopped;
  BOOLEAN Exited;
  BOOLEAN Published;
  BOOLEAN QueueWarning;
  BOOLEAN KeySeen;
  BOOLEAN DropWarning;
  UINT8 Packet[SH_PACKET_SIZE];
  struct sh_init_action Action;
  hk_result Keys;
} J313_CONTEXT;
STATIC J313_CONTEXT *mContext;
STATIC J313_BOOT_DIAGNOSTIC *mDiagnostic;
STATIC BOOLEAN mDiagnosticAttempted;

STATIC VOID DiagnosticCreate (VOID)
{
  EFI_GUID Guid = J313_BOOT_DIAGNOSTIC_GUID;
  EFI_TPL OldTpl;
  J313_BOOT_DIAGNOSTIC *D;
  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (OldTpl);
  if (OldTpl != TPL_APPLICATION || mDiagnosticAttempted) return;
  mDiagnosticAttempted = TRUE;
  if (EFI_ERROR (gBS->AllocatePool (EfiBootServicesData, sizeof (*D), (VOID **)&D))) return;
  *D = (J313_BOOT_DIAGNOSTIC){0};
  D->Version = J313_DIAG_VERSION; D->Size = sizeof (*D);
  D->Stage = J313StageEntry;
  if (EFI_ERROR (gBS->InstallConfigurationTable (&Guid, D))) { FreePool (D); return; }
  /* Intentionally retained boot-lifetime allocation; OS reclaims after EBS.
     A reloaded image may replace the table, retaining the old small allocation. */
  mDiagnostic = D;
}

STATIC EFI_STATUS DiagnosticStatus (UINT32 Stage, EFI_STATUS Status)
{
  if (mDiagnostic != NULL) {
    mDiagnostic->Stage = Stage; mDiagnostic->Status = (UINT64)Status;
    if (EFI_ERROR (Status)) {
      mDiagnostic->LastFailureStage = Stage;
      mDiagnostic->LastFailureStatus = (UINT64)Status;
    }
  }
  return Status;
}

STATIC UINT64 NowMs (VOID)
{
  return GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000000ULL;
}

/* Capture before Stop/free; first fatal is immutable even during cleanup.
 * Counters are successful packet calls, PIO counts are actual last transaction. */
STATIC VOID DiagnosticHid (J313_CONTEXT *C, UINT32 Op, UINT32 Bus, UINT32 Domain)
{
  J313_BOOT_DIAGNOSTIC *D = mDiagnostic;
  if (D == NULL || D->HidFrozen) return;
  D->Valid |= J313_DIAG_HID_VALID;
  D->HidOp = Op; D->HidBus = Bus;
  D->HidPhase = C->Session.failure_domain ? C->Session.failure_phase : C->Session.init.phase;
  D->HidDomain = Domain ? Domain : C->Session.failure_domain;
  D->HidDetail = C->Session.failure_domain ? C->Session.failure_detail : C->Session.detail;
  D->HidReject = C->Session.init.reject_detail;
  D->HidRejectMask = C->Session.init.reject_mask;
  D->HidPioStatus = C->Transport.LastTransfer.status;
  D->HidPioSegments = (UINT32)C->Transport.LastTransfer.segments_completed;
  D->HidPioFlags = C->Transport.LastTransfer.fifo_error_flags;
  D->HidTxQueued = C->Transport.LastTransfer.tx_queued;
  D->HidTxCompleted = C->Transport.LastTransfer.tx_completed;
  D->HidRxCompleted = C->Transport.LastTransfer.rx_completed;
  D->HidAck = C->Transport.LastAck;
  if (D->HidDomain) D->HidFrozen = 1;
}

/* Timer and ExitBootServices share TPL_NOTIFY: no mid-transaction teardown. */
STATIC VOID Stop (J313_CONTEXT *C)
{
  if (C->Stopped) return;
  C->Stopped = TRUE;
  if (C->Timer != NULL) (VOID)gBS->SetTimer (C->Timer, TimerCancel, 0);
  J313SpiTransportStop (&C->Transport);
  js_stop (&C->Session);
}

STATIC int Pump (J313_CONTEXT *C)
{
  J313_SPI_STATUS Bus = J313_SPI_OK;
  UINT32 Op = 1, Domain = 0;
  EFI_STATUS Status = EFI_DEVICE_ERROR;
  bool Ready;
  int Result;
  if (C->Stopped) return JS_ERROR;
  if (mDiagnostic != NULL && !mDiagnostic->HidFrozen) ++mDiagnostic->HidPolls;
  Result = js_tick (&C->Session, NowMs ());
  if (Result == JS_ERROR) goto Failed;
  Op = 2;
  Bus = J313SpiTransportReady (&C->Transport, &Ready);
  if (Bus != J313_SPI_OK) goto BusFailed;
  if (!Ready) { DiagnosticHid (C, Op, Bus, 0); return JS_WAIT; }
  if (mDiagnostic != NULL && !mDiagnostic->HidFrozen) ++mDiagnostic->HidReady;
  /* Exactly one read and at most one response request per invocation. */
  Op = 3;
  Bus = J313SpiTransportReadPacket (&C->Transport, C->Packet);
  if (Bus != J313_SPI_OK) goto BusFailed;
  if (mDiagnostic != NULL && !mDiagnostic->HidFrozen) ++mDiagnostic->HidReads;
  Op = 4;
  Result = js_packet (&C->Session, NowMs (), C->Packet, &C->Action, &C->Keys);
  if (Result == JS_TX) {
    Op = 5;
    Bus = J313SpiTransportWritePacket (&C->Transport, C->Action.packet);
    if (Bus != J313_SPI_OK) goto BusFailed;
    if (mDiagnostic != NULL && !mDiagnostic->HidFrozen) ++mDiagnostic->HidWrites;
  } else if (Result == JS_KEYS && C->Published) {
    if (!C->KeySeen && C->Keys.count != 0) {
      C->KeySeen = TRUE;
      DEBUG ((DEBUG_ERROR, "J313-SPI FIRST-KEY decoded\n"));
    }
    Status = J313KeyboardFeed (C->Keyboard, &C->Keys);
    if (EFI_ERROR (Status)) {
      if (!C->QueueWarning) {
        C->QueueWarning = TRUE;
        DEBUG ((DEBUG_ERROR, "J313-SPI QUEUE %r\n", Status));
      }
      if (Status != EFI_OUT_OF_RESOURCES) goto Failed;
    }
  } else if (Result == JS_DROP) {
    if (mDiagnostic != NULL && !mDiagnostic->HidFrozen) ++mDiagnostic->HidDrops;
    if (!C->DropWarning) {
      C->DropWarning = TRUE;
      DEBUG ((DEBUG_WARN, "J313-SPI DROP detail=%d errors=%u\n", C->Session.detail, C->Session.errors));
    }
  } else if (Result == JS_ERROR) goto Failed;
  DiagnosticHid (C, Op, Bus, 0);
  return Result;
BusFailed:
  Domain = 7;
  DEBUG ((DEBUG_ERROR, "J313-SPI BUS-FAIL status=%u\n", (UINT32)Bus));
Failed:
  DiagnosticHid (C, Op, Bus, Domain);
  if (mDiagnostic != NULL) mDiagnostic->HidFrozen = 1;
  DiagnosticStatus (J313StagePump, Status);
  DEBUG ((DEBUG_ERROR, "J313-SPI STOP phase=%u detail=%d\n", (UINT32)C->Session.init.phase, C->Session.detail));
  Stop (C);
  return JS_ERROR;
}

STATIC VOID EFIAPI Poll (EFI_EVENT Event, VOID *Context)
{
  (VOID)Event;
  (VOID)Pump ((J313_CONTEXT *)Context);
}

STATIC VOID EFIAPI ExitBootServices (EFI_EVENT Event, VOID *Context)
{
  J313_CONTEXT *C = Context;
  (VOID)Event;
  C->Exited = TRUE;
  Stop (C); /* Last MMIO here; all later callbacks return before touching bus. */
}

STATIC EFI_STATUS CloseEvents (J313_CONTEXT *C)
{
  EFI_STATUS Status;
  if (C->Timer != NULL) {
    Status = gBS->CloseEvent (C->Timer);
    if (EFI_ERROR (Status)) return Status;
    C->Timer = NULL;
  }
  if (C->ExitEvent != NULL) {
    Status = gBS->CloseEvent (C->ExitEvent);
    if (EFI_ERROR (Status)) return Status;
    C->ExitEvent = NULL;
  }
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI AppleSpiHidDxeUnload (EFI_HANDLE ImageHandle)
{
  EFI_STATUS Status;
  EFI_TPL OldTpl;
  J313_CONTEXT *C = mContext;
  (VOID)ImageHandle;
  if (C == NULL) return EFI_SUCCESS;
  if (C->Exited) return EFI_UNSUPPORTED;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Stop (C);
  gBS->RestoreTPL (OldTpl);
  Status = CloseEvents (C);
  if (EFI_ERROR (Status)) return Status; /* Callback context must remain live. */
  Status = C->Keyboard == NULL ? EFI_SUCCESS : J313KeyboardDestroy (&C->Keyboard);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J313-SPI RETAIN uninstall=%r\n", Status));
    return Status; /* Protocol users still own references: never free/unload. */
  }
  FreePool (C);
  mContext = NULL;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI AppleSpiHidDxeInitialize (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  J313_CONTEXT *C;
  J313_SPI_RESOURCES Resources;
  EFI_STATUS Status, Cleanup;
  J313_SPI_STATUS Bus;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  UINTN Tick, Index;
  UINT64 Started, Now;
  EFI_HANDLE KeyboardHandle;
  EFI_DEVICE_PATH_PROTOCOL *KeyboardPath;
  int Result = JS_WAIT;
  (VOID)SystemTable;
  DEBUG ((DEBUG_ERROR, "J313-SPI ENTRY real-path unverified-hardware\n"));
  DiagnosticCreate ();
  if (mContext != NULL) return DiagnosticStatus (J313StageAlreadyStarted, EFI_ALREADY_STARTED);
  Status = J313BoardResourcesLoadEx (&Resources, mDiagnostic);
  DiagnosticStatus (J313StageBoard, Status);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "J313-SPI BOARD-FAIL %r\n", Status));
    PhotoDelay ("BOARD-FAIL");
    return Status;
  }
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  DiagnosticStatus (J313StageLoadedImage, Status);
  if (EFI_ERROR (Status)) return Status;
  C = AllocateZeroPool (sizeof (*C));
  if (C == NULL) return DiagnosticStatus (J313StageContext, EFI_OUT_OF_RESOURCES);
  mContext = C;
  LoadedImage->Unload = AppleSpiHidDxeUnload;
  Status = J313KeyboardCreate (&C->Keyboard);
  DiagnosticStatus (J313StageKeyboard, Status);
  if (EFI_ERROR (Status)) goto Fail;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBootServices,
                              C, &gEfiEventExitBootServicesGuid, &C->ExitEvent);
  DiagnosticStatus (J313StageExitEvent, Status);
  if (EFI_ERROR (Status)) goto Fail;
  /* Waitable timer permits firmware event dispatch between bounded packets;
     Publish must run at APPLICATION, never from a timer notification. */
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &C->Timer);
  DiagnosticStatus (J313StageBootTimer, Status);
  if (EFI_ERROR (Status)) goto Fail;
  Bus = J313SpiTransportInit (&C->Transport, &Resources, NULL);
  if (mDiagnostic != NULL) { mDiagnostic->BusCode = (UINT32)Bus; mDiagnostic->Valid |= J313_DIAG_BUS_VALID; }
  DiagnosticStatus (J313StageSpiInit, Bus == J313_SPI_OK ? EFI_SUCCESS : EFI_DEVICE_ERROR);
  if (Bus != J313_SPI_OK) {
    DEBUG ((DEBUG_ERROR, "J313-SPI INIT-FAIL status=%u\n", (UINT32)Bus));
    Status = EFI_DEVICE_ERROR; goto Fail;
  }
  Started = NowMs ();
  js_start (&C->Session, Started);
  DiagnosticHid (C, 1, J313_SPI_OK, 0);
  Status = gBS->SetTimer (C->Timer, TimerPeriodic, POLL_100NS);
  DiagnosticStatus (J313StageStartTimer, Status);
  if (EFI_ERROR (Status)) goto Fail;
  for (Tick = 0; Tick < BOOT_TICKS; ++Tick) {
    Status = gBS->WaitForEvent (1, &C->Timer, &Index);
  DiagnosticStatus (J313StageWait, Status);
    if (EFI_ERROR (Status)) goto Fail;
    Now = NowMs ();
    if (Now < Started || Now - Started >= 6000) { DiagnosticHid (C, 6, J313_SPI_OK, 8); Status = DiagnosticStatus (J313StageTimeout, EFI_TIMEOUT); goto Fail; }
    Result = Pump (C);
    if (Result == JS_PUBLISH || Result == JS_ERROR) break;
  }
  if (Result != JS_PUBLISH) { Status = DiagnosticStatus (J313StageHandshake, EFI_DEVICE_ERROR); goto Fail; }
  Status = gBS->SetTimer (C->Timer, TimerCancel, 0);
  DiagnosticStatus (J313StageCancelTimer, Status);
  if (EFI_ERROR (Status)) goto Fail;
  Status = gBS->CloseEvent (C->Timer);
  DiagnosticStatus (J313StageCloseTimer, Status);
  if (EFI_ERROR (Status)) goto Fail;
  C->Timer = NULL;
  Status = J313KeyboardPublish (C->Keyboard);
  DiagnosticStatus (J313StagePublish, Status);
  if (EFI_ERROR (Status)) goto Fail;
  C->Published = TRUE;
  KeyboardHandle = J313KeyboardHandle (C->Keyboard);
  KeyboardPath = DevicePathFromHandle (KeyboardHandle);
  if (KeyboardPath == NULL) {
    DiagnosticStatus (J313StageConsolePath, EFI_NOT_FOUND);
    DEBUG ((DEBUG_ERROR, "J313-SPI CONSOLE-NO-PATH\n"));
  } else {
    Status = EfiBootManagerUpdateConsoleVariable (ConIn, KeyboardPath, NULL);
  DiagnosticStatus (J313StageConIn, Status);
    DEBUG ((EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO, "J313-SPI CONIN %r\n", Status));
    Status = gBS->ConnectController (KeyboardHandle, NULL, NULL, TRUE);
  DiagnosticStatus (J313StageConnect, Status);
    DEBUG ((EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO, "J313-SPI CONNECT %r\n", Status));
  }
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Poll, C, &C->Timer);
  DiagnosticStatus (J313StagePollEvent, Status);
  if (EFI_ERROR (Status)) goto Fail;
  Status = gBS->SetTimer (C->Timer, TimerPeriodic, POLL_100NS);
  DiagnosticStatus (J313StagePollTimer, Status);
  if (EFI_ERROR (Status)) goto Fail;
  DEBUG ((DEBUG_ERROR, "J313-SPI READY live-descriptor bytes=%u\n", (UINT32)C->Session.init.descriptor_length[0]));
  DiagnosticStatus (J313StageReady, EFI_SUCCESS);
  PhotoDelay ("READY");
  return EFI_SUCCESS;
Fail:
  DEBUG ((DEBUG_ERROR, "J313-SPI ENTRY-FAIL %r\n", Status));
  Cleanup = AppleSpiHidDxeUnload (ImageHandle);
  if (mDiagnostic != NULL) { mDiagnostic->CleanupStatus = (UINT64)Cleanup; mDiagnostic->Valid |= J313_DIAG_CLEANUP_VALID; }
  if (EFI_ERROR (Cleanup)) {
    /* Returning an entry error lets the loader free this image while events or
       protocols still reference it. Retain it, visibly failed, for safe unload. */
    DEBUG ((DEBUG_ERROR, "J313-SPI RETAIN-IMAGE cleanup=%r original=%r\n", Cleanup, Status));
  }
  /* Cleanup attempt is complete, including retained contexts; never pump here. */
  PhotoDelay ("FAIL-CLEANUP");
  if (EFI_ERROR (Cleanup)) return EFI_SUCCESS;
  return Status;
}
