"""Compile actual framebuffer rendering functions with guarded host memory."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Silicon/Apple/AppleSiliconPkg/Library/AppleFbDebugLib/AppleFbDebugLib.c'
PRE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t UINTN;
typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef char CHAR8;
typedef int BOOLEAN;
#define STATIC static
#define VOID void
#define IN
#define CONST const
#define TRUE 1
#define FALSE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SetMem(p,n,c) memset(p,c,n)
#define CopyMem memmove
static uintptr_t boot_pointer;
static int mirror;
#define FixedPcdGet64(x) boot_pointer
#define FixedPcdGetBool(x) mirror
static uintptr_t low, high;
static void WriteBackDataCacheRange(void *p, size_t n) {
  assert((uintptr_t)p >= low && (uintptr_t)p <= high);
  assert(n <= high - (uintptr_t)p);
}
typedef struct { UINT16 UnicodeWeight; UINT8 Attributes; UINT8 GlyphCol1[19]; } APPLE_FB_GLYPH;
static APPLE_FB_GLYPH mAppleFbGlyphs[95];
'''
MAIN = r'''
static void untouched(unsigned char *p, size_t n) {
  for (size_t i=0; i<n; ++i) assert(p[i] == 0xa5);
}
int main(void) {
  for (int scale=1; scale<=2; ++scale) for (mirror=0; mirror<=1; ++mirror) {
    size_t width=scale == 1 ? 640 : 1280, height=480, stride=(width+16)*4;
    size_t size=stride*height;
    unsigned char *allocation=malloc(size+128), *pixels=allocation+64;
    memset(allocation,0xa5,size+128);
    APPLE_BOOT_ARGS args={0};
    args.VideoBase=(uintptr_t)pixels; args.VideoWidth=width; args.VideoHeight=height;
    args.VideoStride=stride; args.VideoDepth=32;
    boot_pointer=(uintptr_t)&args;
    low=(uintptr_t)pixels; high=low+stride*304;
    memset(&mFb,0,sizeof(mFb));
    FbProbe(); assert(mFb.Ready && FB_CELL_H*mFb.Rows == 304);
    untouched(pixels+stride*304,size-stride*304);
    for (int n=0;n<80;++n) FbPuts("LAST STEP LONG ENOUGH TO DRAW AND SCROLL\n");
    FbPixel(width,0,0); FbPixel(0,304,0); FbFlush(stride*304,4);
    untouched(pixels+stride*304,size-stride*304);
    untouched(allocation,64); untouched(pixels+size,64);
    // Simulate another linked module's first DEBUG: reset module-local state.
    memset(&mFb,0,sizeof(mFb)); FbProbe();
    untouched(pixels+stride*304,size-stride*304);
    // Reject malformed stride and overflow before touching memory.
    for (int bad=0;bad<3;++bad) {
      memset(allocation,0xa5,size+128); memset(&mFb,0,sizeof(mFb));
      args.VideoStride=bad==0 ? width*4-4 : bad==1 ? stride+1 : UINT64_MAX-3;
      FbProbe(); assert(!mFb.Ready); untouched(allocation,size+128);
    }
    free(allocation);
  }
  return 0;
}
'''

class StripeTests(unittest.TestCase):
    def test_actual_clear_glyph_scroll_and_geometry(self):
        text = SOURCE.read_text()
        text = text[:text.index('\nVOID\nEFIAPI\nDebugPrint (')]
        text = re.sub(r'^#include .*$', '', text, flags=re.M)
        with tempfile.TemporaryDirectory() as directory:
            source, exe = Path(directory)/'stripe.c', Path(directory)/'stripe'
            source.write_text(PRE + text + MAIN)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    unittest.main()
