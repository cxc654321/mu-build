"""Compile/execute the actual transformed C functions, with mocked firmware services.
SWM_SOURCE_TREE selects real upstream inputs; CC selects the host compiler.
Stage2 is mocked deliberately: this tests readiness, not legacy resource rollback.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("swm_readiness", ROOT / "Tools/swm-readiness.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
TREE = Path(os.environ.get("SWM_SOURCE_TREE", ROOT.parent / "mu-build-tree"))

PREAMBLE = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#define STATIC static
#define CONST const
#define CHAR8 char
#define VOID void
#define IN
#define EFIAPI
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_DEVICE_ERROR 7
#define EFI_NOT_READY 6
#define EFI_ACCESS_DENIED 15
#define EFI_ERROR(x) ((x)!=0)
#define DEBUG(x) do {} while (0)
#define EVT_TIMER 1
#define EVT_NOTIFY_SIGNAL 2
#define TPL_CALLBACK 8
#define TimerCancel 0
#define TimerPeriodic 1
#define ByProtocol 0
#define FreePool free
typedef int EFI_STATUS;
typedef int BOOLEAN;
typedef size_t UINTN;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef int MS_RENDERING_ENGINE_PROTOCOL;
typedef int EFI_HII_FONT_PROTOCOL;
typedef int EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;
typedef struct { int HorizontalResolution, VerticalResolution; } INFO;
typedef struct { INFO *Info; } MODE;
typedef struct { MODE *Mode; } EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct { EFI_HANDLE ConsoleInHandle; } EFI_SYSTEM_TABLE;
typedef struct CLIENT { struct CLIENT *pNext; } WINMGR_CLIENT;
struct { int SWMProtocol; WINMGR_CLIENT *Clients; } mSWM;
static EFI_SYSTEM_TABLE st, *gST=&st;
static EFI_EVENT mGopRegisterEvent, mSWMWatchListTimerEvent;
static EFI_HANDLE mImageHandle;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
static MS_RENDERING_ENGINE_PROTOCOL *mRenderingEngine;
static EFI_HII_FONT_PROTOCOL *mFont;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mSimpleTextInEx;
static void *mTheme;
static int gMsSREProtocolGuid, gEfiGraphicsOutputProtocolGuid, gEfiHiiFontProtocolGuid;
static int gEfiSimpleTextInputExProtocolGuid, gMsSWMProtocolGuid;
static int theme=1,font=1,sre=1,input=1,pair=1,published=1;
static int create_error,set_error,signal_error,close_error,stage_error,locate_error;
static int stages,closes,signals,legacy_cleanup,uninstalls;
static INFO info={1920,1080};
static MODE mode={&info};
static EFI_GRAPHICS_OUTPUT_PROTOCOL wrapper={&mode},simplefb={&mode};
static int token;
static void (*callback)(EFI_EVENT,void*);
static void *MsUiGetPlatformTheme(void) { return theme? &token:NULL; }
static int locate(void *guid,void *reg,void **out) { assert(guid==&gEfiHiiFontProtocolGuid); *out=font?&token:NULL; return font?0:1; }
static int handle(void *h,void *guid,void **out) {
  *out=NULL;
  if(guid==&gEfiSimpleTextInputExProtocolGuid && h==gST->ConsoleInHandle && input) *out=&token;
  if(guid==&gMsSREProtocolGuid && h==(void*)2 && sre) *out=&token;
  if(guid==&gEfiGraphicsOutputProtocolGuid && h==(void*)1) *out=&simplefb;
  if(guid==&gEfiGraphicsOutputProtocolGuid && h==(void*)2 && pair) *out=&wrapper;
  if(guid==&gMsSWMProtocolGuid && h==mImageHandle && published) *out=&mSWM.SWMProtocol;
  return *out?0:1;
}
static int handles(int search,void *guid,void *key,UINTN *count,EFI_HANDLE **out) {
  assert(guid==&gMsSREProtocolGuid);
  if(locate_error || !sre) return 1;
  *count=2; *out=malloc(2*sizeof(void*)); (*out)[0]=(void*)1; (*out)[1]=(void*)2; return 0;
}
static int create(int flags,int tpl,void (*cb)(EFI_EVENT,void*),void *ctx,EFI_EVENT *out) {
  assert(flags==(EVT_TIMER|EVT_NOTIFY_SIGNAL)); assert(tpl==TPL_CALLBACK);
  if(create_error) return 1;
  callback=cb; *out=&token; return 0;
}
static int timer(EFI_EVENT e,int type,unsigned long interval) { if(type==TimerPeriodic) {assert(interval==1000000);return set_error;} return 0; }
static int close_event(EFI_EVENT e) { closes++; return close_error; }
static int signal_event(EFI_EVENT e) {signals++; if(signal_error)return 1; callback(e,NULL);return 0;}
static int uninstall(EFI_HANDLE h,...) { uninstalls++; return 0; }
struct BS {
 int (*LocateProtocol)(void*,void*,void**);
 int (*HandleProtocol)(void*,void*,void**);
 int (*LocateHandleBuffer)(int,void*,void*,UINTN*,EFI_HANDLE**);
 int (*CreateEvent)(int,int,void(*)(EFI_EVENT,void*),void*,EFI_EVENT*);
 int (*SetTimer)(EFI_EVENT,int,unsigned long);
 int (*CloseEvent)(EFI_EVENT);
 int (*SignalEvent)(EFI_EVENT);
 int (*UninstallMultipleProtocolInterfaces)(EFI_HANDLE,...);
} bs={locate,handle,handles,create,timer,close_event,signal_event,uninstall},*gBS=&bs;
static EFI_STATUS DriverCleanUp(EFI_HANDLE);
EFI_STATUS DriverUnload(EFI_HANDLE);
void GopRegisteredCallback(EFI_EVENT,void*);
static int DriverInitStage2(EFI_HANDLE h) {
 stages++;
 assert(mGop==&wrapper && mRenderingEngine && mFont && mSimpleTextInEx && mTheme);
 GopRegisteredCallback(NULL,NULL); /* reentrancy must not repeat effects */
 assert(DriverUnload(h)==EFI_ACCESS_DENIED);
 return stage_error;
}
'''
MAIN = r'''
int main(int argc,char **argv) {
 int n=atoi(argv[1]);
 gST->ConsoleInHandle=(void*)3;
 switch(n) {
 case 1:sre=0;break;
 case 2:gST->ConsoleInHandle=NULL;break;
 case 3:theme=0;break;
 case 4:font=0;break;
 case 5:pair=0;break;
 case 6:stage_error=1;break;
 case 7:create_error=1;break;
 case 8:set_error=1;break;
 case 9:signal_error=1;break;
 case 10:sre=0;break;
 case 11:published=0;break;
 case 12:close_error=1;break;
 case 13:locate_error=1;break;
 case 14:input=0;break;
 case 15:signal_error=close_error=1;break;
 }
 int status=DriverInit((void*)9,&st);
 if(n>=7 && n<=9) {
   assert(status!=0 && stages==0 && legacy_cleanup==0 && mGopRegisterEvent==NULL);
   return 0;
 }
 assert(status==0);
 if(n==15 || n==12) {
   assert(stages==0 && mSwmReadiness==SwmFailed && mGopRegisterEvent);
   assert(DriverUnload(mImageHandle)!=0);
   close_error=0; assert(DriverUnload(mImageHandle)==0); return 0;
 }
 if(n==10) {
   assert(stages==0); assert(DriverUnload(mImageHandle)==0);
   assert(mGopRegisterEvent==NULL && legacy_cleanup==0); return 0;
 }
 if((n>=1 && n<=5) || n==13 || n==14) {
   assert(stages==0 && mSwmReadiness==SwmWaiting && mGopRegisterEvent);
   theme=font=sre=input=pair=1; locate_error=0; gST->ConsoleInHandle=(void*)3;
   callback(mGopRegisterEvent,NULL);
 }
 assert(stages==1 && mGopRegisterEvent==NULL);
 if(n==6 || n==11) {
   assert(mSwmReadiness==SwmFailed && mGop==&wrapper && legacy_cleanup==0);
   assert(uninstalls==(n==6 ? 1 : 0));
   GopRegisteredCallback(NULL,NULL);
   assert(stages==1);
   assert(DriverCleanUp(mImageHandle)==0 && legacy_cleanup==0);
   assert(uninstalls==(n==6 ? 1 : 0));
 } else { assert(mSwmReadiness==SwmReady && mGop==&wrapper); }
 assert(DriverUnload(mImageHandle)==EFI_ACCESS_DENIED);
 return 0;
}
'''

class SwmReadinessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = {name: (TREE / patch.REL / name).read_bytes() for name in patch.HASHES}
        cls.original = cls.raw["WindowManager.c"].decode()

    def copy_tree(self, root):
        directory = root / patch.REL
        directory.mkdir(parents=True)
        for name, data in self.raw.items():
            (directory / name).write_bytes(data)
        return directory

    def test_apply_idempotent_and_preserves_stage2_inf_header(self):
        with tempfile.TemporaryDirectory() as temp:
            tree=Path(temp); directory=self.copy_tree(tree)
            self.assertIn("check only", patch.patch_tree(tree))
            self.assertEqual((directory / "WindowManager.c").read_bytes(), self.raw["WindowManager.c"])
            self.assertEqual(patch.patch_tree(tree, True), "patched")
            first=(directory / "WindowManager.c").read_bytes()
            self.assertNotIn(b"FreeAbsolutePointerInterfaceWatchList", first)
            self.assertEqual(patch.patch_tree(tree, True), "already patched")
            self.assertEqual(first,(directory / "WindowManager.c").read_bytes())
            for name in ("WindowManager.h", "SimpleWindowManagerDxe.inf"):
                self.assertEqual((directory/name).read_bytes(),self.raw[name])
            before=self.original.replace("\r\n","\n")
            a,b=patch.function_span(before,"DriverInitStage2")
            self.assertIn(before[a:b],first.decode())

    def test_anchor_failure_no_write(self):
        for name in patch.HASHES:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                tree=Path(temp); directory=self.copy_tree(tree)
                (directory/name).write_bytes(self.raw[name]+b"\n/* changed */\n")
                snapshot={p.name:p.read_bytes() for p in directory.iterdir()}
                with self.assertRaises(ValueError): patch.patch_tree(tree,True)
                self.assertEqual(snapshot,{p.name:p.read_bytes() for p in directory.iterdir()})

    def test_tampered_patch_no_write(self):
        with tempfile.TemporaryDirectory() as temp:
            tree=Path(temp); directory=self.copy_tree(tree)
            patch.patch_tree(tree,True)
            path=directory/"WindowManager.c"
            damaged=path.read_bytes().replace(b"1000000",b"2000000",1)
            path.write_bytes(damaged)
            with self.assertRaises(ValueError): patch.patch_tree(tree,True)
            self.assertEqual(path.read_bytes(),damaged)

    def test_missing_submodule_no_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(FileNotFoundError): patch.patch_tree(Path(temp),True)
            self.assertEqual(list(Path(temp).iterdir()),[])

    def test_compiled_replacement_functions(self):
        source=patch.transform(self.original)
        code=PREAMBLE + patch.GLOBALS
        for name in ("GopRegisteredCallback","DriverInit","DriverCleanUp","DriverUnload"):
            start,end=patch.function_span(source,name)
            code+=source[start:end]
        code+=MAIN
        cc=os.environ.get("CC","cc")
        self.assertIsNotNone(shutil.which(cc),"host C compiler required")
        with tempfile.TemporaryDirectory() as temp:
            c=Path(temp)/"readiness.c"; binary=Path(temp)/"readiness"
            c.write_text(code)
            result=subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Wno-unused-parameter",str(c),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for scenario in range(16):
                with self.subTest(scenario=scenario):
                    result=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)

if __name__ == "__main__":
    unittest.main()
