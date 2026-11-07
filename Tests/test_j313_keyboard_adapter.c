/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 J313HidKeyboard contributors
 * Actual production adapter + actual UEFI headers, only services are mocked.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#undef NULL
#include "../Silicon/Apple/AppleSiliconPkg/Drivers/AppleSpiHidDxe/J313Keyboard.c"

EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiSimpleTextInProtocolGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
typedef struct { UINT32 Type; EFI_TPL Tpl; EFI_EVENT_NOTIFY Fn; VOID *Ctx; int Signal; } MOCK_EVENT;
static MOCK_EVENT *Events[32];
static EFI_TPL Tpl = TPL_APPLICATION;
static int EventCount, Allocations, Installs, FailCreate, FailInstall, FailUninstall, FailClose;
static J313_KEYBOARD *Keyboard;
static VOID *SelfHandle, *OtherHandle;
static int Calls, OtherCalls;
VOID *EFIAPI AllocateZeroPool (UINTN Size) { VOID *P = calloc (1, Size); if(P) ++Allocations; return P; }
VOID EFIAPI FreePool (VOID *P) { assert(P); --Allocations; free(P); }
VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset(P, 0, N); }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N) { return memmove(D,S,N); }
static EFI_TPL EFIAPI Raise (EFI_TPL New) { EFI_TPL Old=Tpl; assert(New>=Old); Tpl=New; return Old; }
static VOID EFIAPI Restore (EFI_TPL Old) { assert(Old<=Tpl); Tpl=Old; }
static EFI_STATUS EFIAPI Create (UINT32 Type, EFI_TPL Level, EFI_EVENT_NOTIFY Fn, VOID *Ctx, EFI_EVENT *Out) {
  int I; if(FailCreate && --FailCreate==0) return EFI_OUT_OF_RESOURCES;
  for(I=0; I<32 && Events[I]; ++I) {} assert(I<32);
  Events[I]=calloc(1,sizeof(MOCK_EVENT)); assert(Events[I]);
  Events[I]->Type=Type; Events[I]->Tpl=Level; Events[I]->Fn=Fn; Events[I]->Ctx=Ctx;
  *Out=Events[I]; ++EventCount; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close (EFI_EVENT E) {
  int I; if(FailClose) return EFI_DEVICE_ERROR;
  for(I=0;I<32 && Events[I]!=E;++I) {} assert(I<32);
  free(E); Events[I]=NULL; --EventCount; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Signal (EFI_EVENT E) { assert(E); ((MOCK_EVENT*)E)->Signal=1; return EFI_SUCCESS; }
static void Pump(void) {
  int I; assert(Tpl==TPL_APPLICATION);
  for(I=0;I<32;++I) if(Events[I] && Events[I]->Type==EVT_NOTIFY_SIGNAL && Events[I]->Signal) {
    MOCK_EVENT *E=Events[I]; E->Signal=0; Tpl=E->Tpl; E->Fn(E,E->Ctx); Tpl=TPL_APPLICATION;
  }
}
static int Check(EFI_EVENT Event) {
  MOCK_EVENT *E=Event; assert(E->Type==EVT_NOTIFY_WAIT);
  if(!E->Signal) { EFI_TPL Old=Raise(E->Tpl); E->Fn(E,E->Ctx); Restore(Old); }
  if(E->Signal) { E->Signal=0; return 1; } return 0;
}
static void CheckProtocols(va_list Args) {
  assert(va_arg(Args,EFI_GUID*)==&gEfiDevicePathProtocolGuid);
  { EFI_DEVICE_PATH_PROTOCOL *P=va_arg(Args,VOID*); assert(P->Type==HARDWARE_DEVICE_PATH && P->SubType==HW_VENDOR_DP); }
  assert(va_arg(Args,EFI_GUID*)==&gEfiSimpleTextInProtocolGuid); assert(va_arg(Args,VOID*)!=NULL);
  assert(va_arg(Args,EFI_GUID*)==&gEfiSimpleTextInputExProtocolGuid); assert(va_arg(Args,VOID*)!=NULL);
  assert(va_arg(Args,VOID*)==NULL);
}
static EFI_STATUS EFIAPI Install (EFI_HANDLE *H, ...) {
  va_list Args; va_start(Args,H); CheckProtocols(Args); va_end(Args);
  if(FailInstall) return EFI_DEVICE_ERROR; *H=&Services; ++Installs; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Uninstall (EFI_HANDLE H, ...) {
  va_list Args; assert(H==&Services); va_start(Args,H); CheckProtocols(Args); va_end(Args);
  if(FailUninstall) return EFI_ACCESS_DENIED; --Installs; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Other(EFI_KEY_DATA *Key) { (void)Key; ++OtherCalls; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Notify(EFI_KEY_DATA *Key) {
  assert(Tpl==TPL_CALLBACK); assert(Key->Key.UnicodeChar=='a'); ++Calls;
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,SelfHandle)==EFI_SUCCESS);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,OtherHandle)==EFI_SUCCESS);
  assert(J313KeyboardDestroy(&Keyboard)==EFI_UNSUPPORTED); /* cannot destroy in callback */
  assert(Keyboard!=NULL); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ResetNotify(EFI_KEY_DATA *Key) {
  assert(Tpl==TPL_CALLBACK); assert(Key->Key.UnicodeChar=='a');
  ++Calls;
  assert(Keyboard->Ex.Reset(&Keyboard->Ex,FALSE)==EFI_SUCCESS);
  return EFI_SUCCESS;
}
int main(void) {
  EFI_KEY_DATA Key={0}, Pattern={0}; EFI_INPUT_KEY Basic={0}; VOID *Duplicate;
  EFI_KEY_TOGGLE_STATE State; hk_result R={0}; int I;
  Services.RaiseTPL=Raise; Services.RestoreTPL=Restore; Services.CreateEvent=Create;
  Services.CloseEvent=Close; Services.SignalEvent=Signal;
  Services.InstallMultipleProtocolInterfaces=Install; Services.UninstallMultipleProtocolInterfaces=Uninstall;
  for(I=1;I<=3;++I) { FailCreate=I; assert(J313KeyboardCreate(&Keyboard)==EFI_OUT_OF_RESOURCES); assert(!Keyboard && !EventCount && !Allocations); }
  FailCreate=0; assert(J313KeyboardCreate(&Keyboard)==EFI_SUCCESS); assert(EventCount==3 && Installs==0);
  assert(J313KeyboardHandle(Keyboard)==NULL); assert(!Check(Keyboard->In.WaitForKey));
  R.count=1; R.events[0].usage=4; R.events[0].pressed=1;
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_NOT_READY);
  FailInstall=1; assert(J313KeyboardPublish(Keyboard)==EFI_DEVICE_ERROR); assert(!Installs);
  FailInstall=0; assert(J313KeyboardPublish(Keyboard)==EFI_SUCCESS); assert(Installs==1);
  assert(J313KeyboardGetHandle(Keyboard)==&Services);
  assert(J313KeyboardPublish(Keyboard)==EFI_ALREADY_STARTED);
  assert(Keyboard->In.Reset(&Keyboard->In,FALSE)==EFI_SUCCESS);
  assert(Keyboard->Ex.Reset(&Keyboard->Ex,TRUE)==EFI_UNSUPPORTED);
  assert(Keyboard->Ex.ReadKeyStrokeEx(&Keyboard->Ex,&Key)==EFI_NOT_READY);
  Pattern.Key.UnicodeChar='a';
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_SUCCESS);
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Notify,&SelfHandle)==EFI_SUCCESS);
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Notify,&Duplicate)==EFI_SUCCESS && Duplicate==SelfHandle);
  Tpl=TPL_NOTIFY; assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS); assert(Calls==0); Tpl=TPL_APPLICATION;
  assert(Check(Keyboard->In.WaitForKey) && Check(Keyboard->Ex.WaitForKeyEx));
  assert(Keyboard->In.ReadKeyStroke(&Keyboard->In,&Basic)==EFI_SUCCESS && Basic.UnicodeChar=='a');
  assert(Keyboard->Ex.ReadKeyStrokeEx(&Keyboard->Ex,&Key)==EFI_NOT_READY);
  assert(!Check(Keyboard->Ex.WaitForKeyEx)); Pump(); assert(Calls==1 && OtherCalls==0);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,SelfHandle)==EFI_INVALID_PARAMETER);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,(VOID*)1)==EFI_INVALID_PARAMETER);
  State=0x87; assert(Keyboard->Ex.SetState(&Keyboard->Ex,&State)==EFI_SUCCESS);
  assert(Keyboard->Ex.SetState(&Keyboard->Ex,&State)==EFI_SUCCESS); /* idempotent */
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  assert(Keyboard->Ex.ReadKeyStrokeEx(&Keyboard->Ex,&Key)==EFI_SUCCESS);
  assert(Key.Key.UnicodeChar=='A' && Key.KeyState.KeyToggleState==0x87);
  State=0xc0; assert(Keyboard->Ex.SetState(&Keyboard->Ex,&State)==EFI_UNSUPPORTED);
  assert(J313KeyboardResetQueue(Keyboard)==EFI_SUCCESS);
  for(I=0;I<(int)KQ_CAPACITY;++I) assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_OUT_OF_RESOURCES);
  R.count=HK_MAX_EVENTS+1; assert(J313KeyboardFeed(Keyboard,&R)==EFI_INVALID_PARAMETER); R.count=1;
  for(I=0;I<(int)KQ_CAPACITY;++I) assert(Keyboard->Ex.ReadKeyStrokeEx(&Keyboard->Ex,&Key)==EFI_SUCCESS);
  assert(!Check(Keyboard->In.WaitForKey));
  /* Notifications registered after arrival must not receive historical keys. */
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,OtherHandle)==EFI_SUCCESS);
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_SUCCESS);
  Pump(); assert(OtherCalls==0);
  assert(Keyboard->In.ReadKeyStroke(&Keyboard->In,&Basic)==EFI_SUCCESS);
  /* Notify queue remains bounded even when the read queue is drained at NOTIFY. */
  Tpl=TPL_NOTIFY;
  for(I=0;I<(int)KQ_CAPACITY;++I) {
    assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
    assert(Keyboard->In.ReadKeyStroke(&Keyboard->In,&Basic)==EFI_SUCCESS);
  }
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_OUT_OF_RESOURCES);
  Tpl=TPL_APPLICATION; Pump(); assert(OtherCalls==(int)KQ_CAPACITY);
  assert(J313KeyboardResetQueue(Keyboard)==EFI_SUCCESS);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,OtherHandle)==EFI_SUCCESS);
  /* Basic reset cancels pending notifications and drains both readers. */
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  assert(Keyboard->In.Reset(&Keyboard->In,TRUE)==EFI_UNSUPPORTED);
  assert(Keyboard->PendingCount==1 && Keyboard->Queue.count==1);
  assert(Keyboard->In.Reset(&Keyboard->In,FALSE)==EFI_SUCCESS);
  Pump(); assert(OtherCalls==(int)KQ_CAPACITY);
  assert(Keyboard->Ex.ReadKeyStrokeEx(&Keyboard->Ex,&Key)==EFI_NOT_READY);
  assert(Keyboard->In.ReadKeyStroke(&Keyboard->In,&Basic)==EFI_NOT_READY);
  assert(!Check(Keyboard->In.WaitForKey) && !Check(Keyboard->Ex.WaitForKeyEx));
  /* Reset from first callback cancels the in-flight key for later callbacks,
   * as well as the second pending key. Registration nodes remain valid. */
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,ResetNotify,&SelfHandle)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  Pump(); assert(Calls==2 && OtherCalls==(int)KQ_CAPACITY);
  assert(Keyboard->PendingCount==0 && Keyboard->Queue.count==0);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,SelfHandle)==EFI_SUCCESS);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  Pump(); assert(OtherCalls==(int)KQ_CAPACITY+1);
  assert(Keyboard->Ex.Reset(&Keyboard->Ex,FALSE)==EFI_SUCCESS);
  assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,OtherHandle)==EFI_SUCCESS);
  /* Tombstones bound allocation and prevent stale handle ABA reuse. */
  while(Keyboard->LastId<128) {
    assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_SUCCESS);
    assert(Keyboard->Ex.UnregisterKeyNotify(&Keyboard->Ex,OtherHandle)==EFI_SUCCESS);
  }
  assert(Keyboard->Ex.RegisterKeyNotify(&Keyboard->Ex,&Pattern,Other,&OtherHandle)==EFI_OUT_OF_RESOURCES);
  FailUninstall=1; assert(J313KeyboardDestroy(&Keyboard)==EFI_ACCESS_DENIED);
  assert(Keyboard && EventCount==3 && Installs==1);
  assert(J313KeyboardFeed(Keyboard,&R)==EFI_SUCCESS);
  FailUninstall=0; FailClose=1; assert(J313KeyboardDestroy(&Keyboard)==EFI_DEVICE_ERROR);
  assert(Keyboard && EventCount==3 && Installs==0);
  assert(J313KeyboardPublish(Keyboard)==EFI_NOT_READY);
  FailClose=0; assert(J313KeyboardDestroy(&Keyboard)==EFI_SUCCESS);
  assert(!Keyboard && !EventCount && !Allocations && !Installs);
  FailCreate=2; FailClose=1;
  assert(J313KeyboardCreate(&Keyboard)==EFI_OUT_OF_RESOURCES && Keyboard);
  FailClose=0; assert(J313KeyboardDestroy(&Keyboard)==EFI_SUCCESS);
  assert(!EventCount && !Allocations);
  puts("production J313Keyboard adapter: PASS"); return 0;
}
