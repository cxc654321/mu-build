#!/usr/bin/env python3
"""Link and execute actual EarlySetup + planner, with host-only IO mocks."""
import pathlib
import subprocess
import tempfile
import unittest

from test_adt_copy_plan import SHIM, PKG
TEST = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AdtCopyPlan.h"
#include <Library/PcdLib.h>
BOOLEAN EarlySetup(VOID *, UINT64 *, UINT64 *, VOID *, VOID *);
UINT64 MockFdBase, MockFbBase, MockFbSize;
static unsigned copies, checks;
static size_t sizes[2];
static int check_result;
static unsigned char *ram, *bootdest, *adtdest;
static struct boot_args *args;
VOID *CopyMem(VOID *dest, CONST VOID *src, UINTN size) {
  assert(copies < 2);
  sizes[copies++] = size;
  return memcpy(dest, src, size);
}
int dt_check(void *mem, size_t size, size_t *off) {
  checks++;
  assert(copies == 2 && mem == adtdest && size == args->devtree_size);
  assert(off == NULL);
  assert(!memcmp(mem, ram + 0x200000, size));
  return check_result;
}
static void reset(unsigned revision) {
  memset(ram, 0, 0x1000000);
  args = (struct boot_args *)(ram + 0x10000);
  args->revision = revision; args->version = 2;
  args->phys_base = (UINTN)ram; args->mem_size = 0x1000000;
  args->virt_base = 0xfffffe0000000000ULL;
  args->devtree = (void *)(UINTN)(args->virt_base + 0x200000);
  args->devtree_size = 0x2001;
  args->video.base = (UINTN)(ram + 0x800000);
  args->video.stride = 256; args->video.height = 17;
  MockFdBase = (UINTN)(ram + 0x600000);
  bootdest = ram + 0x400000; adtdest = ram + 0x404000;
  memset(bootdest, 0xa5, 0x2000);
  memset(ram + 0x200000, 0x6b, args->devtree_size);
  copies = checks = 0; check_result = 0;
  MockFbBase = MockFbSize = 0;
}
int main(void) {
  ram = aligned_alloc(4096, 0x1000000); assert(ram);
  for(unsigned revision = 1; revision <= 3; revision++) {
    UINT64 base = 0, size = 0;
    reset(revision);
    assert(EarlySetup(args, &base, &size, bootdest, adtdest));
    assert(copies == 2 && checks == 1);
    const size_t expected[] = {0, 384, 736, 1152};
    assert(sizes[0] == expected[revision] && sizes[1] == 0x2001);
    assert(!memcmp(bootdest, args, sizes[0]));
    assert(bootdest[sizes[0]] == 0xa5); // no largest-union overcopy
    assert(base == (UINTN)ram && size == 0x1000000);
    assert(MockFbBase == args->video.base && MockFbSize == 0x4000);
  }
  for(unsigned bad = 0; bad < 4; bad++) {
    UINT64 base = 0x1234, size = 0x5678;
    reset(1);
    if(bad == 0) args->revision = 4;
    if(bad == 1) args->devtree = (void *)(UINTN)(args->virt_base - 1);
    if(bad == 2) adtdest = bootdest;
    if(bad == 3) args->devtree_size = 0;
    assert(!EarlySetup(args, &base, &size, bootdest, adtdest));
    assert(!copies && !checks && !MockFbBase && !MockFbSize);
    assert(base == 0x1234 && size == 0x5678 && bootdest[0] == 0xa5);
  }
  reset(2); check_result = -1;
  UINT64 base = 0x1234, size = 0x5678;
  assert(!EarlySetup(args, &base, &size, bootdest, adtdest));
  assert(copies == 2 && checks == 1);
  assert(base == 0x1234 && size == 0x5678 && !MockFbBase && !MockFbSize);
  free(ram);
  puts("production EarlySetup: 8 integration cases passed");
}
"""
PCD = r"""
extern UINT64 MockFdBase, MockFbBase, MockFbSize;
#define PcdFdBaseAddress MockFdBase
#define PcdFrameBufferAddress MockFbBase
#define PcdFrameBufferSize MockFbSize
#define PcdGet64(Token) (Token)
#define FixedPcdGet32(Token) _PCD_VALUE_##Token
#define _PCD_VALUE_PcdFdSize 0x100000U
#define _PCD_VALUE_PcdSystemMemoryUefiRegionSize 0x100000U
#define PatchPcdSet64(Token, Value) ((Token) = (Value))
"""
class AdtEarlySetupTests(unittest.TestCase):
    def test_production_integration(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            (tmp / "Library").mkdir()
            (tmp / "Uefi.h").write_text(SHIM + "\n#define IN\n#define OUT\n")
            (tmp / "Library/PcdLib.h").write_text(PCD)
            (tmp / "Library/BaseMemoryLib.h").write_text(
                "VOID *CopyMem(VOID *, CONST VOID *, UINTN);\n")
            (tmp / "Library/DebugAgentLib.h").write_text("")
            (tmp / "Library/DebugLib.h").write_text("#define DEBUG(Args) ((void)0)\n")
            (tmp / "test.c").write_text(TEST)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I" + str(tmp),
                            "-I" + str(PKG / "Include"), "-I" + str(PKG / "PrePi"),
                            str(PKG / "PrePi/AdtCopyPlan.c"),
                            str(PKG / "PrePi/AdtParser.c"), str(tmp / "test.c"),
                            "-o", str(tmp / "test")], check=True)
            subprocess.run([str(tmp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
