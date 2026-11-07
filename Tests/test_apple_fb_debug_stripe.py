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
  for (int scale=1; scale<=2; ++scale) for (mirror=0; mirror<=1; ++mirror)
  for (int tall=0; tall<2; ++tall) {
    size_t width=scale == 1 ? 640 : 1280, height=tall ? 1601 : 480;
    size_t stride=(width+16)*4, size=stride*height;
    size_t cell=19*scale, rows=MIN(height, APPLE_FB_DEBUG_STRIPE_HEIGHT)/cell;
    size_t stripe_height=rows*cell, origin=height-stripe_height;
    unsigned char *allocation=malloc(size+128), *pixels=allocation+64;
    assert(allocation != NULL);
    memset(allocation,0xa5,size+128);
    // Nonempty, asymmetric glyphs exercise both white/black and mirroring.
    for (size_t i=0;i<95;++i) for (size_t y=0;y<19;++y)
      mAppleFbGlyphs[i].GlyphCol1[y]=0x01;
    mFontReady=FALSE;
    APPLE_BOOT_ARGS args={0};
    args.VideoBase=(uintptr_t)pixels; args.VideoWidth=width; args.VideoHeight=height;
    args.VideoStride=stride; args.VideoDepth=tall ? 30 : 32;
    boot_pointer=(uintptr_t)&args;
    low=(uintptr_t)pixels+stride*origin; high=(uintptr_t)pixels+size;
    memset(&mFb,0,sizeof(mFb));
    FbProbe(); assert(mFb.Ready && FB_CELL_H*mFb.Rows == stripe_height);
    assert(mFb.Base == (volatile UINT32 *)(pixels+stride*origin));
    assert(mFb.Height == height && mFb.OriginY == origin && mFb.Scale == (size_t)scale);
    assert(origin >= height-MIN(height, APPLE_FB_DEBUG_STRIPE_HEIGHT));
    untouched(pixels,stride*origin);
    // Clear/banner must cover the stripe, including padding, and nothing above.
    for (size_t y=origin;y<height;++y) for (size_t x=width*4;x<stride;++x)
      assert(pixels[y*stride+x] == 0);
    FbPixel(0,0,0x12345678);
    FbPixel(width-1,stripe_height-1,0x87654321);
    assert(((UINT32 *)(pixels+stride*origin))[mirror ? width-1 : 0] == 0x12345678);
    assert(((UINT32 *)(pixels+stride*(height-1)))[mirror ? 0 : width-1] == 0x87654321);
    // Glyph coordinates are stripe-local, including scaled mirrored columns.
    mFb.CurX=0; mFb.CurY=0; FbDrawGlyph('A');
    for (size_t y=0;y<cell;++y) for (size_t x=0;x<8*(size_t)scale;++x)
      assert(((UINT32 *)(pixels+stride*(origin+y)))[mirror ? width-1-x : x]
             == (x >= 7*(size_t)scale ? UINT32_MAX : 0));
    // Direct Base scroll must copy exactly one character row, including padding.
    for (size_t y=0;y<stripe_height;++y)
      memset(pixels+stride*(origin+y),(int)(y/cell+1),stride);
    FbScroll();
    for (size_t y=0;y<stripe_height;++y) for (size_t x=0;x<stride;++x)
      assert(pixels[stride*(origin+y)+x] == (y < stripe_height-cell ? y/cell+2 : 0));
    untouched(pixels,stride*origin);
    for (int n=0;n<80;++n) FbPuts("LAST STEP LONG ENOUGH TO DRAW AND SCROLL\n");
    // Invalid local coordinates/flushes must not write into padding or guards.
    unsigned char *snapshot=malloc(size); assert(snapshot != NULL);
    memcpy(snapshot,pixels,size);
    FbPixel(width,0,0); FbPixel(0,stripe_height,0); FbPixel(0,(UINTN)-1,0);
    FbFlush(stride*stripe_height,4); FbFlush(0,stride*stripe_height+1);
    FbFlush((UINTN)-1,4); FbFlushRow(rows);
    assert(memcmp(snapshot,pixels,size) == 0); free(snapshot);
    untouched(pixels,stride*origin);
    untouched(allocation,64); untouched(pixels+size,64);
    // Simulate another linked module's first DEBUG: reset module-local state.
    memset(&mFb,0,sizeof(mFb)); FbProbe();
    assert(mFb.Base == (volatile UINT32 *)low && mFb.OriginY == origin);
    untouched(pixels,stride*origin);
    untouched(allocation,64); untouched(pixels+size,64);
    // Probe is idempotent within one module and does not clear again.
    FbPixel(0,0,0x12345678); FbProbe();
    assert(mFb.Base[mirror ? width-1 : 0] == 0x12345678);
    // Reject malformed stride, dimensions, depth and address/size overflow.
    APPLE_BOOT_ARGS valid=args;
    for (int bad=0;bad<10;++bad) {
      memset(allocation,0xa5,size+128); memset(&mFb,0,sizeof(mFb)); args=valid;
      switch (bad) {
      case 0: args.VideoStride=width*4-4; break;
      case 1: args.VideoStride=stride+1; break;
      case 2: args.VideoStride=UINT64_MAX-3; break;
      case 3: args.VideoBase=UINT64_MAX-size+1; break;
      case 4: args.VideoWidth=639; break;
      case 5: args.VideoHeight=479; break;
      case 6: args.VideoHeight=UINT64_MAX; break;
      case 7: args.VideoDepth=24; break;
      case 8: args.VideoBase=0; break;
      case 9: args.VideoStride=0; break;
      }
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
            # A non-cell-aligned cap also checks that rounding stays bottom-aligned.
            for height in (304, 303, 600):
                with self.subTest(stripe_height=height):
                    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    f'-DAPPLE_FB_DEBUG_STRIPE_HEIGHT={height}',
                                    str(source), '-o', str(exe)], check=True)
                    subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    unittest.main()
