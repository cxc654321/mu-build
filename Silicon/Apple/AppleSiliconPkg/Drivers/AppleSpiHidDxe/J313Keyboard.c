/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 J313HidKeyboard contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "J313Keyboard.h"
#include <Guid/J313Keyboard.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

/* Notifications have a separate bounded queue, never consume the read queue.
 * Registration nodes are retained until Destroy: stale handles cannot alias a
 * new registration, and callbacks may unregister themselves or their successor.
 */
typedef struct KEY_NOTIFY KEY_NOTIFY;
struct KEY_NOTIFY {
  KEY_NOTIFY *Next;
  EFI_KEY_DATA Key;
  EFI_KEY_NOTIFY_FUNCTION Function;
  UINT64 Id;
  BOOLEAN Active;
};
typedef struct {
  EFI_KEY_DATA Key;
  UINT64 LastId;
} PENDING_KEY;
struct J313_KEYBOARD {
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL In;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL Ex;
  struct { VENDOR_DEVICE_PATH Vendor; EFI_DEVICE_PATH_PROTOCOL End; } Path;
  EFI_HANDLE Handle;
  EFI_EVENT NotifyEvent;
  kq_queue Queue;
  UINT8 Modifiers;
  KEY_NOTIFY *Notifications;
  UINT64 LastId;
  PENDING_KEY Pending[KQ_CAPACITY];
  UINTN PendingHead, PendingCount;
  UINT64 ResetGeneration;
  BOOLEAN Published, Busy, Dispatching, Closing;
};
#define FROM_IN(p) ((J313_KEYBOARD *)((UINT8 *)(p) - OFFSET_OF (J313_KEYBOARD, In)))
#define FROM_EX(p) ((J313_KEYBOARD *)((UINT8 *)(p) - OFFSET_OF (J313_KEYBOARD, Ex)))

STATIC BOOLEAN AtApplication (VOID) {
  EFI_TPL Old = gBS->RaiseTPL (TPL_NOTIFY);
  gBS->RestoreTPL (Old);
  return Old == TPL_APPLICATION;
}
STATIC VOID Convert (CONST kq_key *Source, EFI_KEY_DATA *Key) {
  ZeroMem (Key, sizeof (*Key));
  Key->Key.ScanCode = Source->scan_code;
  Key->Key.UnicodeChar = Source->unicode_char;
  Key->KeyState.KeyShiftState = Source->shift_state;
  Key->KeyState.KeyToggleState = Source->toggle_state;
}
STATIC BOOLEAN Matches (CONST EFI_KEY_DATA *Pattern, CONST EFI_KEY_DATA *Key) {
  return Pattern->Key.ScanCode == Key->Key.ScanCode &&
    Pattern->Key.UnicodeChar == Key->Key.UnicodeChar &&
    (Pattern->KeyState.KeyShiftState == 0 ||
     Pattern->KeyState.KeyShiftState == Key->KeyState.KeyShiftState) &&
    (Pattern->KeyState.KeyToggleState == 0 ||
     Pattern->KeyState.KeyToggleState == Key->KeyState.KeyToggleState);
}
STATIC VOID EFIAPI WaitKey (EFI_EVENT Event, VOID *Context) {
  J313_KEYBOARD *C = Context;
  EFI_TPL Old = gBS->RaiseTPL (TPL_NOTIFY);
  if (C->Published && C->Queue.count != 0) {
    gBS->SignalEvent (Event);
  }
  gBS->RestoreTPL (Old);
}
STATIC VOID EFIAPI Dispatch (EFI_EVENT Event, VOID *Context) {
  J313_KEYBOARD *C = Context;
  EFI_TPL Old;
  UINTN Budget = KQ_CAPACITY;
  (VOID)Event;
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  if (C->Dispatching) { gBS->RestoreTPL (Old); return; }
  C->Dispatching = TRUE;
  while (C->Published && C->PendingCount != 0 && Budget-- != 0) {
    PENDING_KEY Pending = C->Pending[C->PendingHead];
    KEY_NOTIFY *Node = C->Notifications;
    UINT64 Generation = C->ResetGeneration;
    C->PendingHead = (C->PendingHead + 1) % KQ_CAPACITY;
    --C->PendingCount;
    while (Node != NULL && Generation == C->ResetGeneration) {
      KEY_NOTIFY *Next = Node->Next;
      if (Node->Active && Node->Id <= Pending.LastId && Matches (&Node->Key, &Pending.Key)) {
        EFI_KEY_NOTIFY_FUNCTION Function = Node->Function;
        EFI_KEY_DATA Copy = Pending.Key;
        gBS->RestoreTPL (Old);
        /* EVT_NOTIFY_SIGNAL event runs at CALLBACK, never at NOTIFY. */
        Function (&Copy);
        Old = gBS->RaiseTPL (TPL_NOTIFY);
      }
      Node = Next;
    }
  }
  C->Dispatching = FALSE;
  if (C->Published && C->PendingCount != 0) { gBS->SignalEvent (C->NotifyEvent); }
  gBS->RestoreTPL (Old);
}
STATIC EFI_STATUS Read (J313_KEYBOARD *C, EFI_KEY_DATA *Key) {
  kq_key Raw;
  kq_status Status;
  EFI_TPL Old = gBS->RaiseTPL (TPL_NOTIFY);
  Status = C->Published ? kq_pop (&C->Queue, &Raw) : KQ_EMPTY;
  if (Status == KQ_OK) { Convert (&Raw, Key); }
  gBS->RestoreTPL (Old);
  return Status == KQ_OK ? EFI_SUCCESS : EFI_NOT_READY;
}
STATIC EFI_STATUS EFIAPI ReadKey (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key) {
  EFI_KEY_DATA Data;
  EFI_STATUS Status;
  if (This == NULL || Key == NULL) { return EFI_INVALID_PARAMETER; }
  Status = Read (FROM_IN (This), &Data);
  if (!EFI_ERROR (Status)) { *Key = Data.Key; }
  return Status;
}
STATIC EFI_STATUS EFIAPI ReadKeyEx (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *Key) {
  if (This == NULL || Key == NULL) { return EFI_INVALID_PARAMETER; }
  return Read (FROM_EX (This), Key);
}
STATIC EFI_STATUS EFIAPI Reset (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN Extended) {
  if (This == NULL) { return EFI_INVALID_PARAMETER; }
  /* Basic reset clears software input, like UsbKbDxe. Extended hardware
   * diagnostics are not implemented; do not pretend they succeeded. */
  return Extended ? EFI_UNSUPPORTED : J313KeyboardResetQueue (FROM_IN (This));
}
STATIC EFI_STATUS EFIAPI ResetEx (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, BOOLEAN Extended) {
  if (This == NULL) { return EFI_INVALID_PARAMETER; }
  return Extended ? EFI_UNSUPPORTED : J313KeyboardResetQueue (FROM_EX (This));
}
STATIC EFI_STATUS EFIAPI SetState (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_TOGGLE_STATE *State) {
  J313_KEYBOARD *C;
  EFI_TPL Old;
  hk_result Result;
  UINT8 Delta;
  if (This == NULL || State == NULL) { return EFI_INVALID_PARAMETER; }
  /* Logical locks only. No LEDs, partial-key exposure, or reserved bits. */
  if ((*State & EFI_TOGGLE_STATE_VALID) == 0 || (*State & ~0x87u) != 0) { return EFI_UNSUPPORTED; }
  C = FROM_EX (This);
  ZeroMem (&Result, sizeof (Result));
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  Delta = (UINT8)(C->Queue.toggle_state ^ *State);
  Result.modifiers = C->Modifiers;
  if (Delta & 1) { Result.events[Result.count].usage = 0x47; Result.events[Result.count++].pressed = 1; }
  if (Delta & 2) { Result.events[Result.count].usage = 0x53; Result.events[Result.count++].pressed = 1; }
  if (Delta & 4) { Result.events[Result.count].usage = 0x39; Result.events[Result.count++].pressed = 1; }
  kq_feed (&C->Queue, &Result);
  gBS->RestoreTPL (Old);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Register (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, EFI_KEY_DATA *Key,
  EFI_KEY_NOTIFY_FUNCTION Function, VOID **Handle) {
  J313_KEYBOARD *C;
  KEY_NOTIFY *Node;
  EFI_TPL Old;
  if (This == NULL || Key == NULL || Function == NULL || Handle == NULL) { return EFI_INVALID_PARAMETER; }
  C = FROM_EX (This);
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  for (Node = C->Notifications; Node != NULL; Node = Node->Next) {
    if (Node->Active && Node->Function == Function &&
        Node->Key.Key.ScanCode == Key->Key.ScanCode && Node->Key.Key.UnicodeChar == Key->Key.UnicodeChar &&
        Node->Key.KeyState.KeyShiftState == Key->KeyState.KeyShiftState &&
        Node->Key.KeyState.KeyToggleState == Key->KeyState.KeyToggleState) {
      *Handle = Node; gBS->RestoreTPL (Old); return EFI_SUCCESS;
    }
  }
  /* Bound lifetime tombstones; duplicate active registrations remain free. */
  if (C->LastId == 128) { gBS->RestoreTPL (Old); return EFI_OUT_OF_RESOURCES; }
  Node = AllocateZeroPool (sizeof (*Node));
  if (Node == NULL) { gBS->RestoreTPL (Old); return EFI_OUT_OF_RESOURCES; }
  Node->Key = *Key; Node->Function = Function; Node->Active = TRUE;
  Node->Id = ++C->LastId;
  Node->Next = C->Notifications; C->Notifications = Node;
  *Handle = Node;
  gBS->RestoreTPL (Old);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Unregister (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This, VOID *Handle) {
  J313_KEYBOARD *C;
  KEY_NOTIFY *Node;
  EFI_TPL Old;
  if (This == NULL || Handle == NULL) { return EFI_INVALID_PARAMETER; }
  C = FROM_EX (This);
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  for (Node = C->Notifications; Node != NULL; Node = Node->Next) {
    if (Node == Handle && Node->Active) {
      Node->Active = FALSE; gBS->RestoreTPL (Old); return EFI_SUCCESS;
    }
  }
  gBS->RestoreTPL (Old);
  return EFI_INVALID_PARAMETER;
}
EFI_STATUS J313KeyboardResetQueue (J313_KEYBOARD *C) {
  EFI_TPL Old;
  if (C == NULL) { return EFI_INVALID_PARAMETER; }
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  kq_init (&C->Queue); C->Modifiers = 0; C->PendingHead = C->PendingCount = 0;
  ++C->ResetGeneration; /* Invalidate the key already dequeued by Dispatch. */
  gBS->RestoreTPL (Old);
  return EFI_SUCCESS;
}
EFI_STATUS J313KeyboardFeed (J313_KEYBOARD *C, CONST hk_result *Result) {
  EFI_TPL Old;
  kq_status Status;
  UINTN Before, Index;
  BOOLEAN Overflow = FALSE;
  if (C == NULL || Result == NULL) { return EFI_INVALID_PARAMETER; }
  Old = gBS->RaiseTPL (TPL_NOTIFY);
  if (!C->Published || C->Busy) { gBS->RestoreTPL (Old); return EFI_NOT_READY; }
  Before = C->Queue.count;
  Status = kq_feed (&C->Queue, Result);
  if (Status == KQ_INVALID) { gBS->RestoreTPL (Old); return EFI_INVALID_PARAMETER; }
  C->Modifiers = Result->modifiers;
  for (Index = Before; Index < C->Queue.count; ++Index) {
    EFI_KEY_DATA Key;
    KEY_NOTIFY *Node;
    Convert (&C->Queue.keys[(C->Queue.head + Index) % KQ_CAPACITY], &Key);
    for (Node = C->Notifications; Node != NULL; Node = Node->Next) {
      if (Node->Active && Matches (&Node->Key, &Key)) { break; }
    }
    if (Node == NULL) { continue; }
    if (C->PendingCount == KQ_CAPACITY) { Overflow = TRUE; continue; }
    C->Pending[(C->PendingHead + C->PendingCount) % KQ_CAPACITY].Key = Key;
    C->Pending[(C->PendingHead + C->PendingCount) % KQ_CAPACITY].LastId = C->LastId;
    ++C->PendingCount;
  }
  /* Wait events are level-tested by their EVT_NOTIFY_WAIT callback. Signalling
   * both here would leave stale signals when one reader drains the shared queue. */
  if (C->PendingCount != 0) { gBS->SignalEvent (C->NotifyEvent); }
  gBS->RestoreTPL (Old);
  return Status == KQ_OVERFLOW || Overflow ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}
EFI_STATUS J313KeyboardCreate (J313_KEYBOARD **Context) {
  J313_KEYBOARD *C;
  EFI_STATUS Status;
  STATIC CONST EFI_GUID Vendor = J313_KEYBOARD_DEVICE_PATH_GUID;
  if (Context == NULL) { return EFI_INVALID_PARAMETER; }
  *Context = NULL;
  if (!AtApplication ()) { return EFI_UNSUPPORTED; }
  C = AllocateZeroPool (sizeof (*C));
  if (C == NULL) { return EFI_OUT_OF_RESOURCES; }
  kq_init (&C->Queue);
  C->In.Reset = Reset; C->In.ReadKeyStroke = ReadKey;
  C->Ex.Reset = ResetEx; C->Ex.ReadKeyStrokeEx = ReadKeyEx;
  C->Ex.SetState = SetState; C->Ex.RegisterKeyNotify = Register; C->Ex.UnregisterKeyNotify = Unregister;
  C->Path.Vendor.Header.Type = HARDWARE_DEVICE_PATH;
  C->Path.Vendor.Header.SubType = HW_VENDOR_DP;
  C->Path.Vendor.Header.Length[0] = sizeof (VENDOR_DEVICE_PATH);
  CopyMem (&C->Path.Vendor.Guid, &Vendor, sizeof (Vendor));
  C->Path.End.Type = END_DEVICE_PATH_TYPE; C->Path.End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  C->Path.End.Length[0] = sizeof (EFI_DEVICE_PATH_PROTOCOL);
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitKey, C, &C->In.WaitForKey);
  if (!EFI_ERROR (Status)) { Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitKey, C, &C->Ex.WaitForKeyEx); }
  if (!EFI_ERROR (Status)) { Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Dispatch, C, &C->NotifyEvent); }
  if (EFI_ERROR (Status)) {
    *Context = C;
    /* Even exceptional CloseEvent failure must retain the callback context. */
    J313KeyboardDestroy (Context);
    return Status;
  }
  *Context = C;
  return EFI_SUCCESS;
}
EFI_HANDLE J313KeyboardHandle (J313_KEYBOARD *C) {
  return C != NULL && C->Published ? C->Handle : NULL;
}
EFI_HANDLE J313KeyboardGetHandle (J313_KEYBOARD *C) {
  return J313KeyboardHandle (C);
}
EFI_STATUS J313KeyboardPublish (J313_KEYBOARD *C) {
  EFI_STATUS Status;
  if (C == NULL) { return EFI_INVALID_PARAMETER; }
  if (!AtApplication ()) { return EFI_UNSUPPORTED; }
  if (C->Busy || C->Dispatching) { return EFI_NOT_READY; }
  if (C->Closing) { return EFI_NOT_READY; }
  if (C->Published) { return EFI_ALREADY_STARTED; }
  C->Busy = TRUE;
  Status = gBS->InstallMultipleProtocolInterfaces (&C->Handle,
    &gEfiDevicePathProtocolGuid, &C->Path,
    &gEfiSimpleTextInProtocolGuid, &C->In,
    &gEfiSimpleTextInputExProtocolGuid, &C->Ex, NULL);
  if (!EFI_ERROR (Status)) { C->Published = TRUE; }
  C->Busy = FALSE;
  return Status;
}
EFI_STATUS J313KeyboardUnpublish (J313_KEYBOARD *C) {
  EFI_STATUS Status;
  if (C == NULL) { return EFI_INVALID_PARAMETER; }
  if (!AtApplication ()) { return EFI_UNSUPPORTED; }
  if (C->Busy || C->Dispatching) { return EFI_NOT_READY; }
  if (!C->Published) { return EFI_SUCCESS; }
  C->Busy = TRUE;
  Status = gBS->UninstallMultipleProtocolInterfaces (C->Handle,
    &gEfiDevicePathProtocolGuid, &C->Path,
    &gEfiSimpleTextInProtocolGuid, &C->In,
    &gEfiSimpleTextInputExProtocolGuid, &C->Ex, NULL);
  if (!EFI_ERROR (Status)) { C->Published = FALSE; C->Handle = NULL; J313KeyboardResetQueue (C); }
  C->Busy = FALSE;
  return Status;
}
EFI_STATUS J313KeyboardDestroy (J313_KEYBOARD **Context) {
  J313_KEYBOARD *C;
  KEY_NOTIFY *Node;
  EFI_STATUS Status;
  if (Context == NULL || *Context == NULL) { return EFI_INVALID_PARAMETER; }
  C = *Context;
  Status = J313KeyboardUnpublish (C);
  if (EFI_ERROR (Status)) { return Status; }
  C->Closing = TRUE;
  /* Close before freeing any callback context. Valid owned events must close.
   * In the exceptional error case retain context and remaining events to retry. */
  if (C->NotifyEvent != NULL) {
    Status = gBS->CloseEvent (C->NotifyEvent);
    if (EFI_ERROR (Status)) { return Status; }
    C->NotifyEvent = NULL;
  }
  if (C->In.WaitForKey != NULL) {
    Status = gBS->CloseEvent (C->In.WaitForKey);
    if (EFI_ERROR (Status)) { return Status; }
    C->In.WaitForKey = NULL;
  }
  if (C->Ex.WaitForKeyEx != NULL) {
    Status = gBS->CloseEvent (C->Ex.WaitForKeyEx);
    if (EFI_ERROR (Status)) { return Status; }
    C->Ex.WaitForKeyEx = NULL;
  }
  while ((Node = C->Notifications) != NULL) { C->Notifications = Node->Next; FreePool (Node); }
  FreePool (C); *Context = NULL;
  return EFI_SUCCESS;
}
