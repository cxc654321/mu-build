/* SPDX-License-Identifier: GPL-2.0-only
 * Real production entry/lifecycle, mocked Boot Services and bus. NO MMIO.
 */
#include "../AppleSpiHidDxe.c"
extern void *calloc(__SIZE_TYPE__,__SIZE_TYPE__);
extern void free(void *);
extern int puts(const char *);
extern void abort(void);
#define CHECK(x) do { if(!(x)) { puts(#x); abort(); } } while(0)
EFI_GUID gEfiLoadedImageProtocolGuid, gEfiEventExitBootServicesGuid;
static EFI_BOOT_SERVICES bs;
EFI_BOOT_SERVICES *gBS=&bs;
static EFI_LOADED_IMAGE_PROTOCOL image;
static J313_BOOT_DIAGNOSTIC *table;
static unsigned alloc_fail, install_fail;
static UINT32 injected_stage;
static unsigned skip_photo;
#define INJECT(stage) do { if(injected_stage==(stage)) return EFI_DEVICE_ERROR; } while(0)
static EFI_STATUS EFIAPI allocate(EFI_MEMORY_TYPE type, UINTN n, VOID **p) {
 (VOID)type;
 if(alloc_fail) return EFI_OUT_OF_RESOURCES;
 *p=calloc(1,n); return *p?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI install(EFI_GUID *g, VOID *p) {
 (VOID)g;
 if(install_fail) return EFI_DEVICE_ERROR;
 table=p; return EFI_SUCCESS;
}
static EFI_DEVICE_PATH_PROTOCOL path;
static unsigned board_fail, handshake_fail, destroy_fail, close_fail, create_fail;
static unsigned initialized, stopped, published, connected, updated, fed, reads;
static UINT64 clock_ms;
static UINTN event_id;
static EFI_TPL current_tpl;
static unsigned stalls, photo_polls;
static BOOLEAN poll_during_stall;
struct J313_KEYBOARD {int unused;};
static struct J313_KEYBOARD keyboard;
BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN Level) { (VOID)Level;return FALSE; }
VOID EFIAPI DebugPrint(UINTN Level, CONST CHAR8 *Format, ...) { (VOID)Level;(VOID)Format; }
VOID *EFIAPI AllocateZeroPool(UINTN n) { if(injected_stage==J313StageContext)return NULL; return calloc(1,n); }
VOID EFIAPI FreePool(VOID *p) {free(p);}
UINT64 EFIAPI GetPerformanceCounter(VOID) { return clock_ms; }
UINT64 EFIAPI GetTimeInNanoSecond(UINT64 t) { return t*1000000; }
EFI_DEVICE_PATH_PROTOCOL *EFIAPI DevicePathFromHandle(EFI_HANDLE h) {CHECK(h==&keyboard);return &path;}
EFI_STATUS EFIAPI EfiBootManagerUpdateConsoleVariable(CONSOLE_TYPE v, EFI_DEVICE_PATH_PROTOCOL *a, EFI_DEVICE_PATH_PROTOCOL *r)
{(VOID)v;(VOID)r;CHECK(a==&path);CHECK(published);updated++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI handle(EFI_HANDLE h, EFI_GUID *g, VOID **p) {INJECT(J313StageLoadedImage);(VOID)h;(VOID)g;*p=&image;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI create(UINT32 t, EFI_TPL tpl, EFI_EVENT_NOTIFY n, VOID *c, EFI_EVENT *e)
{INJECT((t&EVT_NOTIFY_SIGNAL)?J313StagePollEvent:J313StageBootTimer);(VOID)t;(VOID)tpl;(VOID)n;(VOID)c;*e=(EFI_EVENT)++event_id;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI create_ex(UINT32 t,EFI_TPL tpl,EFI_EVENT_NOTIFY n,CONST VOID*c,CONST EFI_GUID*g,EFI_EVENT*e)
{INJECT(J313StageExitEvent);(VOID)g;(VOID)t;(VOID)tpl;(VOID)n;(VOID)c;*e=(EFI_EVENT)++event_id;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI timer(EFI_EVENT e,EFI_TIMER_DELAY t,UINT64 n) {INJECT(t==TimerCancel?J313StageCancelTimer:(published?J313StagePollTimer:J313StageStartTimer));(VOID)e;(VOID)t;(VOID)n;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI wait(UINTN n,EFI_EVENT *e,UINTN *i) {INJECT(J313StageWait);(VOID)n;(VOID)e;*i=0;clock_ms+=5;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI close_event(EFI_EVENT e) {INJECT(J313StageCloseTimer);(VOID)e;return close_fail?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_TPL EFIAPI raise(EFI_TPL t) {EFI_TPL old=current_tpl;CHECK(t>=old);current_tpl=t;return old;}
static VOID EFIAPI restore(EFI_TPL t) {CHECK(t<=current_tpl);current_tpl=t;}
/* No host sleep: verify delay duration, restored TPL and completed lifecycle. */
static EFI_STATUS EFIAPI stall(UINTN us)
{
 CHECK(us==10000000&&us==J313_PHOTO_DELAY_US&&current_tpl==TPL_APPLICATION);
 CHECK(stalls++==0);
 if(skip_photo) return EFI_SUCCESS;
 if(board_fail) CHECK(!initialized&&!mContext);
 else if(create_fail) CHECK(!initialized&&!mContext);
 else if(handshake_fail) {
   CHECK(stopped==1&&!published);
   CHECK(mContext==NULL||(mContext->Stopped&&(destroy_fail||close_fail)));
 } else {
   CHECK(mContext&&mContext->Published&&connected&&updated&&!mContext->Stopped);
   CHECK(mContext->Timer!=NULL);
   if(poll_during_stall) {
     EFI_TPL old=raise(TPL_NOTIFY);
     Poll(NULL,mContext);photo_polls++;
     restore(old);
   }
 }
 return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI connect(EFI_HANDLE h,EFI_HANDLE *d,EFI_DEVICE_PATH_PROTOCOL*p,BOOLEAN recursive)
{(VOID)d;(VOID)p;(VOID)recursive;CHECK(h==&keyboard&&updated);connected++;return EFI_SUCCESS;}
EFI_STATUS J313BoardResourcesLoadEx(J313_SPI_RESOURCES *r, J313_BOOT_DIAGNOSTIC *d) {(VOID)r;(VOID)d;return board_fail?EFI_NOT_FOUND:EFI_SUCCESS;}
J313_SPI_STATUS J313SpiTransportInit(J313_SPI_TRANSPORT*t,const J313_SPI_RESOURCES*r,const J313_SPI_IO*i)
{(VOID)r;(VOID)i;if(injected_stage==J313StageSpiInit)return (J313_SPI_STATUS)1;t->Initialized=true;initialized++;return J313_SPI_OK;}
void J313SpiTransportStop(J313_SPI_TRANSPORT*t) {if(t->Initialized){stopped++;t->Initialized=false;}}
J313_SPI_STATUS J313SpiTransportReady(J313_SPI_TRANSPORT*t,bool*r) {CHECK(t->Initialized);*r=true;return J313_SPI_OK;}
J313_SPI_STATUS J313SpiTransportReadPacket(J313_SPI_TRANSPORT*t,uint8_t*p) {(VOID)p;CHECK(t->Initialized);reads++;return J313_SPI_OK;}
J313_SPI_STATUS J313SpiTransportWritePacket(J313_SPI_TRANSPORT*t,const uint8_t*p) {(VOID)p;CHECK(t->Initialized);return J313_SPI_OK;}
EFI_STATUS J313KeyboardCreate(J313_KEYBOARD**k) {if(create_fail){*k=NULL;return EFI_OUT_OF_RESOURCES;}*k=&keyboard;return EFI_SUCCESS;}
EFI_STATUS J313KeyboardPublish(J313_KEYBOARD*k) {INJECT(J313StagePublish);CHECK(k==&keyboard);published++;return EFI_SUCCESS;}
EFI_HANDLE J313KeyboardHandle(J313_KEYBOARD*k) {return k;}
EFI_STATUS J313KeyboardFeed(J313_KEYBOARD*k,const hk_result*r) {(VOID)k;(VOID)r;fed++;return EFI_SUCCESS;}
EFI_STATUS J313KeyboardDestroy(J313_KEYBOARD**k) {if(destroy_fail)return EFI_ACCESS_DENIED;*k=NULL;return EFI_SUCCESS;}
void js_start(struct j313_session*s,uint64_t n) {(VOID)n;s->phase=JS_INIT;}
void js_stop(struct j313_session*s) {s->phase=JS_STOPPED;}
int js_tick(struct j313_session*s,uint64_t n) {(VOID)n;return s->phase==JS_STOPPED?JS_ERROR:JS_WAIT;}
int js_packet(struct j313_session*s,uint64_t n,const uint8_t*p,struct sh_init_action*a,hk_result*k)
{(VOID)n;(VOID)p;(VOID)a;(VOID)k;if(handshake_fail)return JS_ERROR;if(s->phase==JS_INIT){s->phase=JS_READY;return JS_PUBLISH;}return JS_KEYS;}
static void reset(void)
{
 CHECK(mContext==NULL); if(table) free(table); table=NULL; mDiagnostic=NULL; mDiagnosticAttempted=FALSE; alloc_fail=install_fail=0; board_fail=handshake_fail=destroy_fail=close_fail=create_fail=0;
 initialized=stopped=published=connected=updated=fed=reads=0;clock_ms=0;
 injected_stage=skip_photo=0;stalls=photo_polls=0;current_tpl=TPL_APPLICATION;poll_during_stall=FALSE;
}
int main(void)
{
 bs.AllocatePool=allocate;bs.InstallConfigurationTable=install;
 bs.HandleProtocol=handle;bs.CreateEvent=create;bs.CreateEventEx=create_ex;bs.SetTimer=timer;
 bs.Stall=stall;bs.WaitForEvent=wait;bs.CloseEvent=close_event;bs.RaiseTPL=raise;bs.RestoreTPL=restore;bs.ConnectController=connect;
 reset();board_fail=1;CHECK(EFI_ERROR(AppleSpiHidDxeInitialize(NULL,NULL)));CHECK(!initialized&&!published&&!mContext&&stalls==1);
 reset();create_fail=1;CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_OUT_OF_RESOURCES);CHECK(!initialized&&!published&&!mContext&&stalls==1);
 reset();handshake_fail=1;CHECK(EFI_ERROR(AppleSpiHidDxeInitialize(NULL,NULL)));CHECK(initialized==1&&stopped==1&&!published&&!mContext&&stalls==1);
 reset();CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);CHECK(published==1&&connected==1&&updated==1&&stalls==1);
 Poll(NULL,mContext);CHECK(fed==1);ExitBootServices(NULL,mContext);CHECK(stopped==1);Poll(NULL,mContext);CHECK(fed==1&&reads==2&&stalls==1);
 CHECK(AppleSpiHidDxeUnload(NULL)==EFI_UNSUPPORTED);mContext->Exited=FALSE;CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 reset();CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);destroy_fail=1;
 CHECK(AppleSpiHidDxeUnload(NULL)==EFI_ACCESS_DENIED&&mContext);CHECK(stopped==1);
 destroy_fail=0;CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS&&!mContext);
 reset();handshake_fail=destroy_fail=1;CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);CHECK(mContext&&!published&&mContext->Stopped&&stalls==1);
 destroy_fail=0;CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 reset();CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);close_fail=1;CHECK(EFI_ERROR(AppleSpiHidDxeUnload(NULL))&&mContext);
 close_fail=0;CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 CHECK(stalls==1); /* Unload/Stop/EBS never add photo delays. */
 reset();handshake_fail=close_fail=1;
 CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);
 CHECK(mContext&&mContext->Stopped&&stalls==1);
 close_fail=0;CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 reset();poll_during_stall=TRUE;
 CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);
 CHECK(stalls==1&&photo_polls==1&&fed==1&&reads==2);
 CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS&&stalls==1);
 /* Exercise the real helper at every elevated TPL without running an entry
    handshake/WaitForEvent at an illegal TPL. Board failure is safe to test. */
 {
   EFI_TPL levels[]={TPL_CALLBACK,TPL_NOTIFY,TPL_HIGH_LEVEL};
   unsigned i;
   for(i=0;i<sizeof(levels)/sizeof(levels[0]);i++) {
     reset();current_tpl=levels[i];board_fail=1;
     CHECK(EFI_ERROR(AppleSpiHidDxeInitialize(NULL,NULL)));
     PhotoDelay("READY");PhotoDelay("FAIL-CLEANUP");
     CHECK(stalls==0&&current_tpl==levels[i]&&!initialized&&!mContext);
   }
 }
 reset();handshake_fail=1;CHECK(EFI_ERROR(AppleSpiHidDxeInitialize(NULL,NULL)));
 CHECK(!mContext&&table&&table->Stage==J313StageHandshake&&table->Status==EFI_DEVICE_ERROR);
 CHECK(table->Valid&J313_DIAG_CLEANUP_VALID);CHECK(table->CleanupStatus==EFI_SUCCESS);
 mDiagnostic=NULL;CHECK(table->Stage==J313StageHandshake); /* simulate driver image gone */
 reset();alloc_fail=1;CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);CHECK(!table);CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 reset();install_fail=1;CHECK(AppleSpiHidDxeInitialize(NULL,NULL)==EFI_SUCCESS);CHECK(!table);CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 reset();
 { UINT32 stages[]={J313StageLoadedImage,J313StageSpiInit,J313StageCloseTimer,J313StageExitEvent,J313StageBootTimer,J313StageStartTimer,J313StageWait,J313StageCancelTimer,J313StagePublish,J313StagePollEvent,J313StagePollTimer}; unsigned i;
 for(i=0;i<sizeof(stages)/sizeof(stages[0]);i++) {
 reset();injected_stage=stages[i];skip_photo=1;
 (VOID)AppleSpiHidDxeInitialize(NULL,NULL);
 CHECK(table&&table->Stage==stages[i]&&table->Status==EFI_DEVICE_ERROR);
 injected_stage=0;if(mContext)CHECK(AppleSpiHidDxeUnload(NULL)==EFI_SUCCESS);
 }}
 reset();
 puts("snapshot lifetime and best-effort publication PASS");
 puts("production entry: board gate, handshake gate, console, EBS, retained cleanup PASS");
 puts("photo delay: each entry path, retained cleanup, live polling, high-TPL skip PASS");return 0;
}
