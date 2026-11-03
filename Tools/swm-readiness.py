#!/usr/bin/env python3
"""Fail-closed SWM readiness patch for the audited MU revision.

Default is check-only. --apply explicitly patches WindowManager.c in --tree.
Header/INF are verified, never changed. No git/submodule operations are performed.
Stage2 remains unchanged. Cleanup is replaced with conservative fail-stop:
stop timers and attempt SWM withdrawal, retain allocations and other interfaces.
After Stage2 starts, unloading is refused: legacy cleanup may leave interfaces.
Theme-null gating is defensive only; MsUiThemeLib caches its constructor result,
so its existing theme protocol/library dependency must still be satisfied.
Fingerprints normalize CRLF/LF only. Other source edits fail closed, including
old ui-trace.py changes. Apply this patch to the pristine audited MU revision.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

REL = Path("Common/MU/MsGraphicsPkg/SimpleWindowManagerDxe")
HASHES = {
    "WindowManager.c": "280be9aadb507f6434626c29580cf003885fca1dfc39c3453cc6aa11daa5d818",
    "WindowManager.h": "39e5fbca20432c2e1f42ce387761990deb3f06d117d8b6a1dab0b4e772313c60",
    "SimpleWindowManagerDxe.inf": "ccc507000e6000451bb7c95a15c497f46b73b462366553a88db016f762d6c936",
}
MARKER = "// SWM readiness v1 (single callback timer; 100 ms)."
GLOBALS = r'''
// SWM readiness v1 (single callback timer; 100 ms).
typedef enum {
  SwmWaiting,
  SwmProbing,
  SwmInitializing,
  SwmReady,
  SwmFailed
} SWM_READINESS_STATE;
STATIC SWM_READINESS_STATE  mSwmReadiness = SwmWaiting;
STATIC BOOLEAN              mSwmStage2Started = FALSE;
STATIC BOOLEAN              mSwmCleanupAttempted = FALSE;
STATIC UINTN                mSwmWaitReason = 0;

STATIC VOID
SwmWaitingFor (UINTN Reason, CONST CHAR8 *Name)
{
  if (mSwmWaitReason != Reason) {
    mSwmWaitReason = Reason;
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] waiting: %a\n", Name));
  }
}

STATIC EFI_STATUS
SwmCloseReadinessTimer (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  CancelStatus;

  if (mGopRegisterEvent == NULL) {
    return EFI_SUCCESS;
  }

  CancelStatus = gBS->SetTimer (mGopRegisterEvent, TimerCancel, 0);
  Status = gBS->CloseEvent (mGopRegisterEvent);
  if (!EFI_ERROR (Status)) {
    mGopRegisterEvent = NULL;
  }

  if (EFI_ERROR (CancelStatus) || EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] timer cancel=%r close=%r\n", CancelStatus, Status));
  }

  // A successfully closed event cannot fire, even if cancellation failed.
  return Status;
}
'''
CALLBACK = r'''VOID
EFIAPI
GopRegisteredCallback (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  )
{
  EFI_STATUS                         Status;
  EFI_HANDLE                         *Handles;
  UINTN                              Count;
  UINTN                              Index;
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *Gop;
  MS_RENDERING_ENGINE_PROTOCOL       *RenderingEngine;
  VOID                               *Published;

  if (mSwmReadiness != SwmWaiting) {
    return;
  }

  mSwmReadiness = SwmProbing;
  Handles = NULL;
  Count = 0;
  mGop = NULL;
  mRenderingEngine = NULL;
  mTheme = MsUiGetPlatformTheme ();
  if (mTheme == NULL) {
    SwmWaitingFor (1, "theme");
    goto Waiting;
  }

  mFont = NULL;
  Status = gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **)&mFont);
  if (EFI_ERROR (Status) || (mFont == NULL)) {
    SwmWaitingFor (2, "HII font");
    goto Waiting;
  }

  mSimpleTextInEx = NULL;
  if (gST->ConsoleInHandle == NULL) {
    SwmWaitingFor (3, "ConsoleInHandle");
    goto Waiting;
  }

  Status = gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&mSimpleTextInEx);
  if (EFI_ERROR (Status) || (mSimpleTextInEx == NULL)) {
    SwmWaitingFor (4, "console TextInputEx");
    goto Waiting;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gMsSREProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    SwmWaitingFor (5, "SRE handles");
    goto Waiting;
  }

  for (Index = 0; Index < Count; Index++) {
    Gop = NULL;
    RenderingEngine = NULL;
    Status = gBS->HandleProtocol (Handles[Index], &gMsSREProtocolGuid, (VOID **)&RenderingEngine);
    if (EFI_ERROR (Status) || (RenderingEngine == NULL)) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
    if (EFI_ERROR (Status) || (Gop == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) {
      continue;
    }

    mGop = Gop;
    mRenderingEngine = RenderingEngine;
    break;
  }

  FreePool (Handles);
  Handles = NULL;
  if (mGop == NULL) {
    SwmWaitingFor (6, "same-handle SRE + GOP");
    goto Waiting;
  }

  // Gate reentrancy BEFORE closing the timer or any Stage2 side effects.
  mSwmReadiness = SwmInitializing;
  Status = SwmCloseReadinessTimer ();
  if (EFI_ERROR (Status)) {
    mSwmReadiness = SwmFailed;
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] timer close failed; Stage2 not attempted\n"));
    return;
  }

  mSwmStage2Started = TRUE;
  DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] Stage2 starting (one attempt)\n"));
  Status = DriverInitStage2 (mImageHandle);
  if (!EFI_ERROR (Status)) {
    Published = NULL;
    Status = gBS->HandleProtocol (mImageHandle, &gMsSWMProtocolGuid, &Published);
    if (!EFI_ERROR (Status) && (Published != (VOID *)&mSWM.SWMProtocol)) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  if (EFI_ERROR (Status)) {
    mSwmReadiness = SwmFailed;
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] Stage2/publication failed (%r); no retry\n", Status));
    Status = DriverCleanUp (mImageHandle);
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] cleanup returned %r (fail-stop retains storage until reboot)\n", Status));
    // Retained interfaces may still reference the GOP and client storage.
    return;
  }

  mSwmReadiness = SwmReady;
  DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] Stage2 complete; SWM protocol verified\n"));
  return;

Waiting:
  if (Handles != NULL) {
    FreePool (Handles);
  }
  mSwmReadiness = SwmWaiting;
}
'''
ENTRY = r'''EFI_STATUS
EFIAPI
DriverInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  CloseStatus;

  mImageHandle = ImageHandle;
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  GopRegisteredCallback,
                  NULL,
                  &mGopRegisterEvent
                  );
  if (EFI_ERROR (Status)) {
    mSwmReadiness = SwmFailed;
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] CreateEvent failed (%r)\n", Status));
    return Status;
  }

  Status = gBS->SetTimer (mGopRegisterEvent, TimerPeriodic, 1000000);
  if (!EFI_ERROR (Status)) {
    // Probe protocols already installed; do not wait for a future notification.
    Status = gBS->SignalEvent (mGopRegisterEvent);
  }

  if (EFI_ERROR (Status)) {
    mSwmReadiness = SwmFailed;
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] timer/signal failed (%r)\n", Status));
    CloseStatus = SwmCloseReadinessTimer ();
    if (EFI_ERROR (CloseStatus)) {
      // Keep image resident if an event could still reference its callback.
      DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] event remains; retain failed driver\n"));
      return EFI_SUCCESS;
    }
    return Status;
  }

  // Dependency absence (or asynchronous Stage2 failure) must not unload us.
  return EFI_SUCCESS;
}
'''
CLEANUP = r'''static EFI_STATUS
EFIAPI
DriverCleanUp (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  CloseStatus;
  VOID        *Published;

  Status = SwmCloseReadinessTimer ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mSwmReadiness = SwmFailed;
  if (!mSwmStage2Started || mSwmCleanupAttempted) {
    return EFI_SUCCESS;
  }
  mSwmCleanupAttempted = TRUE;

  // Fail-stop, not rollback: other published interfaces may still own clients.
  // Never free their storage or unload this image after Stage2 started.
  if (mSWMWatchListTimerEvent != NULL) {
    Status = gBS->SetTimer (mSWMWatchListTimerEvent, TimerCancel, 0);
    CloseStatus = gBS->CloseEvent (mSWMWatchListTimerEvent);
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] watchlist cancel=%r close=%r\n", Status, CloseStatus));
    if (!EFI_ERROR (CloseStatus)) {
      mSWMWatchListTimerEvent = NULL;
    }
  }

  Published = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gMsSWMProtocolGuid, &Published);
  if (!EFI_ERROR (Status) && (Published == (VOID *)&mSWM.SWMProtocol)) {
    Status = gBS->UninstallMultipleProtocolInterfaces (
                    ImageHandle, &gMsSWMProtocolGuid, &mSWM.SWMProtocol, NULL
                    );
    DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] SWM withdrawal status=%r\n", Status));
  }
  DEBUG ((DEBUG_ERROR, "[SWMTrace readiness] fail-stop; allocations/interfaces retained until reboot\n"));
  return Status;
}
'''
OLD_UNLOAD = '  Status = DriverCleanUp (ImageHandle);'
NEW_UNLOAD = '''  // Legacy cleanup cannot remove every published interface; retain code pages.
  if (mSwmStage2Started) {
    return EFI_ACCESS_DENIED;
  }
  if ((mSwmReadiness == SwmInitializing) || (mSwmReadiness == SwmProbing)) {
    return EFI_NOT_READY;
  }

  Status = DriverCleanUp (ImageHandle);'''


def function_span(source, name):
    """Find a full definition (not a prototype); audited source uses column-0 braces."""
    pattern = r"(?m)^(?:static )?(?:EFI_STATUS|VOID)\nEFIAPI\n" + re.escape(name) + r" \(\n[^{};]*?\n\{\n.*?^\}\n"
    matches = list(re.finditer(pattern, source, re.S | re.M))
    if len(matches) != 1:
        raise ValueError(f"expected one definition: {name}")
    return matches[0].span()


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("missing/ambiguous anchor")
    return source.replace(old, new, 1)


def transform(source):
    source = source.replace("\r\n", "\n")
    # Fail-stop deliberately retains providers; remove its now-unreachable free helper.
    unused = re.compile(r"(?ms)^static EFI_STATUS\nFreeAbsolutePointerInterfaceWatchList \(\n.*?^}\n")
    if len(unused.findall(source)) != 1:
        raise ValueError("missing/ambiguous watchlist cleanup definition")
    source = unused.sub("", source, count=1)
    source = replace_once(source, 'VOID                           *mGopRegistration;\n',
                          'VOID                           *mGopRegistration;\n' + GLOBALS)
    for name, replacement in (("GopRegisteredCallback", CALLBACK), ("DriverInit", ENTRY)):
        start, end = function_span(source, name)
        source = source[:start] + replacement + source[end:]
    start, end = function_span(source, "DriverCleanUp")
    source = source[:start] + CLEANUP + source[end:]
    return replace_once(source, OLD_UNLOAD, NEW_UNLOAD)


def digest(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def patch_tree(tree, apply=False):
    directory = Path(tree) / REL
    raw = {name: (directory / name).read_bytes() for name in HASHES}
    for name in HASHES.keys() - {"WindowManager.c"}:
        if digest(raw[name]) != HASHES[name]:
            raise ValueError(f"unsupported upstream {name}; no writes")
    original = raw["WindowManager.c"].decode("utf-8")
    if MARKER in original:
        if digest(raw["WindowManager.c"]) != "31ca194d846e28aee971323bc9626d9bd79917fb83e7ca1f9c705d991763aacf":
            raise ValueError("modified patch; no writes")
        return "already patched"
    if digest(raw["WindowManager.c"]) != HASHES["WindowManager.c"]:
        raise ValueError("unsupported WindowManager.c anchors/revision; no writes")
    result = patched_text(original)
    if apply:
        # Validate all input before the single write, retaining original file mode.
        (directory / "WindowManager.c").write_bytes(result.encode("utf-8"))
    return "patched" if apply else "patch applicable (check only)"


def patched_text(original):
    return transform(original)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=Path.cwd())
    parser.add_argument("--apply", action="store_true", help="write validated WindowManager.c; default checks only")
    args = parser.parse_args()
    try:
        print(patch_tree(args.tree, args.apply))
    except (OSError, ValueError, UnicodeError) as error:
        print(f"swm-readiness: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
