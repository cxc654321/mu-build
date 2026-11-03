/* Host fake for the actual driver source, not a reimplementation. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define IN
#define OUT
#define EFIAPI
#define VOID void
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_NOT_FOUND 1
#define EFI_UNSUPPORTED 2
#define EFI_DEVICE_ERROR 3
#define EFI_ALREADY_STARTED 4
#define EFI_ERROR(s) ((s) != 0)
#define EFI_NATIVE_INTERFACE 0
#define ByProtocol 0
#define DEBUG_INFO 0
#define DEBUG_ERROR 1
#define DEBUG(x) debug_ignore x
static void debug_ignore(int level, const char *fmt, ...) {(void)level; (void)fmt;}
typedef int EFI_STATUS;
typedef int BOOLEAN;
typedef size_t UINTN;
typedef void *EFI_HANDLE;
typedef void EFI_SYSTEM_TABLE;
typedef int EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct {uint32_t a; uint16_t b,c; uint8_t d[8];} EFI_GUID;
typedef struct {int end; EFI_GUID guid;} EFI_DEVICE_PATH_PROTOCOL;
typedef EFI_DEVICE_PATH_PROTOCOL MEDIA_FW_VOL_FILEPATH_DEVICE_PATH;
typedef struct {EFI_DEVICE_PATH_PROTOCOL *FilePath;} EFI_LOADED_IMAGE_PROTOCOL;
typedef struct binding EFI_DRIVER_BINDING_PROTOCOL;
struct binding {
 EFI_STATUS (*Supported)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, void *);
 EFI_STATUS (*Start)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, void *);
 EFI_STATUS (*Stop)(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, UINTN, EFI_HANDLE *);
 EFI_HANDLE ImageHandle, DriverBindingHandle;
};
static EFI_GUID gEfiGraphicsOutputProtocolGuid, gEfiDriverBindingProtocolGuid,
 gEfiLoadedImageProtocolGuid, gMsSREProtocolGuid, override;
#define PcdGetPtr(x) (&override)
#define FreePool free
#define CompareGuid(a,b) (!memcmp(a,b,sizeof(EFI_GUID)))
#define IsDevicePathEnd(n) ((n)->end)
#define NextDevicePathNode(n) ((n)+1)
#define EfiGetNameGuidFromFwVolDevicePathNode(n) (&(n)->guid)
static struct {
 EFI_STATUS (*LocateHandleBuffer)(int, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
 EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
 EFI_STATUS (*UninstallProtocolInterface)(EFI_HANDLE, EFI_GUID *, void *);
 EFI_STATUS (*InstallProtocolInterface)(EFI_HANDLE *, EFI_GUID *, int, void *);
 EFI_STATUS (*CloseProtocol)(EFI_HANDLE, EFI_GUID *, EFI_HANDLE, EFI_HANDLE);
} bs, *gBS = &bs;
#include "../Silicon/Apple/AppleSiliconPkg/Drivers/J313SreHandoffDxe/J313SreHandoffDxe.c"

enum { OK, NO_BINDING, TWO_BINDINGS, NO_GOP, TWO_GOPS, UNINSTALL_FAIL,
 OVERRIDE_FAIL, SUPPORTED_FAIL, START_EARLY_FAIL, START_LATE_FAIL,
 VERIFY_FAIL, STOP_FAIL, REMOVE_OVERRIDE_FAIL, RESTORE_FAIL, EXISTING_OVERRIDE };
static int scenario, original, wrapper, sre, has_override, opened, mutations, started;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static EFI_DRIVER_BINDING_PROTOCOL binding;
static EFI_DEVICE_PATH_PROTOCOL path[2];
static EFI_LOADED_IMAGE_PROTOCOL image = {path};
#define CONTROLLER ((void *)1)
#define DRIVER ((void *)2)
static EFI_STATUS locate(int search, EFI_GUID *guid, void *key, UINTN *count, EFI_HANDLE **handles) {
 (void)search; (void)key;
 int driver = guid == &gEfiDriverBindingProtocolGuid;
 if ((driver && scenario == NO_BINDING) || (!driver && scenario == NO_GOP)) return EFI_NOT_FOUND;
 *count = ((driver && scenario == TWO_BINDINGS) || (!driver && scenario == TWO_GOPS)) ? 2 : 1;
 *handles = malloc(*count * sizeof(**handles));
 for (UINTN i=0;i<*count;i++) (*handles)[i] = driver ? DRIVER : CONTROLLER;
 return 0;
}
static EFI_STATUS handle(EFI_HANDLE h, EFI_GUID *guid, void **out) {
 if (guid == &gEfiDriverBindingProtocolGuid) {*out=&binding; return 0;}
 if (guid == &gEfiLoadedImageProtocolGuid) {*out=&image; return 0;}
 assert(h == CONTROLLER);
 if (guid == &gEfiGraphicsOutputProtocolGuid && gop) {*out=gop; return 0;}
 if (guid == &gMsSREProtocolGuid && sre) {*out=&sre; return 0;}
 if (guid == &override && has_override) {*out=&original; return 0;}
 return EFI_NOT_FOUND;
}
static EFI_STATUS uninstall(EFI_HANDLE h, EFI_GUID *guid, void *ptr) {
 assert(h == CONTROLLER && ptr == &original);
 if (guid == &override) {
   if (scenario == REMOVE_OVERRIDE_FAIL) return EFI_DEVICE_ERROR;
   assert(!opened); has_override=0;
 } else {
   assert(gop == &original);
   if (scenario == UNINSTALL_FAIL) return EFI_DEVICE_ERROR;
   gop=NULL;
 }
 mutations++; return 0;
}
static EFI_STATUS install(EFI_HANDLE *h, EFI_GUID *guid, int type, void *ptr) {
 (void)type; assert(*h == CONTROLLER && ptr == &original);
 if (guid == &override) {
   assert(!gop);
   if (scenario == OVERRIDE_FAIL) return EFI_DEVICE_ERROR;
   has_override=1;
 } else {
   assert(!gop);
   if (scenario == RESTORE_FAIL) return EFI_DEVICE_ERROR;
   gop=&original;
 }
 mutations++; return 0;
}
static EFI_STATUS close_protocol(EFI_HANDLE h, EFI_GUID *guid, EFI_HANDLE agent, EFI_HANDLE child) {
 assert(h==CONTROLLER && child==h && agent==DRIVER && guid==&override);
 opened=0; return 0;
}
static EFI_STATUS supported(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h, void *dp) {
 (void)dp; assert(b==&binding && h==CONTROLLER && has_override && !gop);
 return (scenario==SUPPORTED_FAIL || scenario==REMOVE_OVERRIDE_FAIL || scenario==RESTORE_FAIL) ? EFI_DEVICE_ERROR : 0;
}
static EFI_STATUS start(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h, void *dp) {
 (void)dp; assert(b==&binding && h==CONTROLLER && has_override && !gop);
 started++; opened=1;
 if (scenario==START_EARLY_FAIL) return EFI_DEVICE_ERROR;
 gop=&wrapper; sre=(scenario!=VERIFY_FAIL);
 return (scenario==START_LATE_FAIL || scenario==STOP_FAIL) ? EFI_DEVICE_ERROR : 0;
}
static EFI_STATUS stop(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h, UINTN n, EFI_HANDLE *children) {
 assert(b==&binding && h==CONTROLLER && !n && !children);
 if (scenario==STOP_FAIL) return EFI_DEVICE_ERROR;
 gop=NULL; sre=0; opened=0; return 0;
}
int main(void) {
 bs.LocateHandleBuffer=locate; bs.HandleProtocol=handle;
 bs.UninstallProtocolInterface=uninstall; bs.InstallProtocolInterface=install; bs.CloseProtocol=close_protocol;
 binding=(EFI_DRIVER_BINDING_PROTOCOL){supported,start,stop,DRIVER,DRIVER};
 path[0].guid=mRenderingEngineFile; path[1].end=1;
 for (scenario=OK; scenario<=EXISTING_OVERRIDE; scenario++) {
   gop=&original; sre=opened=mutations=started=0; has_override=(scenario==EXISTING_OVERRIDE);
   EFI_STATUS status=J313SreHandoffEntry(NULL,NULL);
   assert((status==0)==(scenario==OK));
   if (scenario==OK || scenario==STOP_FAIL) {assert(gop==&wrapper && has_override && opened);}
   else if (scenario==RESTORE_FAIL) {assert(!gop && !has_override);}
   else {assert(gop==&original && !opened);}
   if (scenario>=NO_BINDING && scenario<=UNINSTALL_FAIL) assert(!mutations && !started);
   if (scenario==EXISTING_OVERRIDE) assert(!mutations && !started);
   if (scenario!=OK && scenario!=STOP_FAIL && scenario!=REMOVE_OVERRIDE_FAIL && scenario!=EXISTING_OVERRIDE) assert(!has_override);
   printf("PASS fault scenario %d\n",scenario);
 }
 puts("15 handoff scenarios passed");
 return 0;
}
