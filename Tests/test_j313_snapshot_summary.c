/* Real production BDS summary with mocked console, tables and TPL. */
#include "../Silicon/Apple/AppleSiliconPkg/Library/DeviceBootManagerLib/J313DiagnosticSummary.c"
#include <stdarg.h>
extern void abort(void);
extern int puts(const char *);
#define CHECK(x) do {if(!(x)) {puts(#x);abort();}} while(0)
static EFI_BOOT_SERVICES bs;
static EFI_SYSTEM_TABLE st;
EFI_BOOT_SERVICES *gBS=&bs;
EFI_SYSTEM_TABLE *gST=&st;
static EFI_TPL tpl=TPL_APPLICATION;
static unsigned lines, unknown, stalls, bottom;
static UINT64 resource, identity, bus, stage, status;
static EFI_CONFIGURATION_TABLE table;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL con;
static EFI_SIMPLE_TEXT_OUTPUT_MODE mode;
EFI_TPL EFIAPI EfiGetCurrentTpl(VOID) {return tpl;}
static EFI_TPL EFIAPI raise(EFI_TPL t) {EFI_TPL old=tpl;CHECK(t>=old);tpl=t;return old;}
static VOID EFIAPI restore(EFI_TPL t) {tpl=t;}
BOOLEAN EFIAPI CompareGuid(CONST GUID *a,CONST GUID *b) {
 const UINT8 *x=(const UINT8 *)a,*y=(const UINT8 *)b;UINTN i;
 for(i=0;i<sizeof(*a);i++) if(x[i]!=y[i]) return FALSE; return TRUE;
}
static int contains(const CHAR8 *s,const CHAR8 *key) {
 UINTN i,j;for(i=0;s[i];i++){for(j=0;key[j]&&s[i+j]==key[j];j++) {}if(!key[j])return 1;}return 0;
}
BOOLEAN EFIAPI DebugPrintEnabled(VOID) {return TRUE;}
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN level) {(VOID)level;return TRUE;}
VOID EFIAPI DebugPrint(UINTN level,CONST CHAR8 *fmt,...) {
 (VOID)level;
 va_list ap;CHECK(tpl==TPL_APPLICATION);lines++;unknown+=contains(fmt,"UNKNOWN");
 va_start(ap,fmt);
 if(contains(fmt,"Resource code=%"))resource=va_arg(ap,UINT32);
 if(contains(fmt,"Identity mask=%"))identity=va_arg(ap,UINT32);
 if(contains(fmt,"SPI init buscode=%"))bus=va_arg(ap,UINT32);
 if(contains(fmt,"J313 stage=")){stage=va_arg(ap,UINT32);status=va_arg(ap,UINT64);}
 va_end(ap);
}
static EFI_STATUS EFIAPI stall(UINTN n){CHECK(tpl==TPL_APPLICATION&&n==30000000);stalls++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI query(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *c,UINTN m,UINTN *x,UINTN *y){(VOID)c;(VOID)m;*x=100;*y=30;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *c,UINTN x,UINTN y){(VOID)c;CHECK(x==0&&y==23);bottom++;return EFI_SUCCESS;}
static void reset(void){lines=unknown=stalls=bottom=0;}
int main(void){
 EFI_GUID guid=J313_BOOT_DIAGNOSTIC_GUID;J313_BOOT_DIAGNOSTIC d={0};unsigned i;
 bs.RaiseTPL=raise;bs.RestoreTPL=restore;bs.Stall=stall;
 con.Mode=&mode;con.QueryMode=query;con.SetCursorPosition=cursor;st.ConOut=&con;
 J313DiagnosticSummary();CHECK(lines==1&&unknown==1&&stalls==1&&bottom==0);
 table.VendorGuid=guid;table.VendorTable=&d;st.ConfigurationTable=&table;st.NumberOfTableEntries=1;
 reset();d.Version=1;d.Size=56;J313DiagnosticSummary();CHECK(lines==1&&unknown==1&&stalls==1);
 reset();d.Version=99;J313DiagnosticSummary();CHECK(lines==1&&unknown==1&&stalls==1);
 reset();d.Version=J313_DIAG_VERSION;d.Size=sizeof(d)-1;J313DiagnosticSummary();CHECK(unknown==1&&lines==1);
 reset();d.Size=sizeof(d);J313DiagnosticSummary();CHECK(lines==6&&unknown==5&&stalls==1);
 d.Valid=J313_DIAG_RESOURCE_VALID|J313_DIAG_IDENTITY_VALID|J313_DIAG_BUS_VALID|J313_DIAG_CLEANUP_VALID|J313_DIAG_PMGR_VALID;
 for(i=0;i<=5;i++) {reset();d.ResourceCode=i;d.IdentityMask=31;d.BusCode=i;d.Stage=J313StageBoard;d.Status=EFI_UNSUPPORTED;
 J313DiagnosticSummary();CHECK(lines==7&&unknown==0&&resource==i&&identity==31&&bus==i&&stage==J313StageBoard&&status==EFI_UNSUPPORTED);}
 for(i=TPL_CALLBACK;i<=TPL_HIGH_LEVEL;i++){reset();tpl=i;J313DiagnosticSummary();CHECK(!lines&&!stalls&&!bottom&&tpl==i);}
 puts("snapshot summary unknown, resource/bus codes, bottom placement, high TPL PASS");return 0;
}
