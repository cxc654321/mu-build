#!/usr/bin/env python3
"""Compile the production planner, not a reimplementation (no hardware access)."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PKG = ROOT / "Silicon/Apple/AppleSiliconPkg"
SHIM = r"""
#ifndef TEST_UEFI_H
#define TEST_UEFI_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uintptr_t UINTN;
typedef void VOID;
typedef bool BOOLEAN;
#define STATIC static
#define CONST const
#define TRUE true
#define FALSE false
#define MAX_UINT64 UINT64_MAX
#define STATIC_ASSERT _Static_assert
#endif
"""
TEST = r"""
#include <assert.h>
#include <stdio.h>
#include "AdtCopyPlan.h"
static struct boot_args a;
static ADT_COPY_PLAN p;
static unsigned count;
static void reset(void) {
  a = (struct boot_args){0};
  a.revision = 3; a.version = 2;
  a.phys_base = 0x800000000ULL; a.mem_size = 0x200000000ULL;
  a.virt_base = 0xfffffe0000000000ULL;
  a.devtree = (void *)(uintptr_t)(a.virt_base + 0x10000000);
  a.devtree_size = 0x8000;
}
static ADT_COPY_RESULT run(uint64_t args, uint64_t boot, uint64_t adt,
                            uint64_t fd, uint64_t fds, uint64_t arena) {
  count++;
  return AppleAdtPlanCopy(&a,args,boot,adt,fd,fds,arena,&p);
}
#define RUN() run(0x820000000ULL,0x840000000ULL,0x840004000ULL,0x830000000ULL,0x2000000,0x10000000)
#define BAD(change) do {reset(); change; assert(RUN()==AdtCopyInvalid);} while(0)
int main(void) {
  assert(AppleBootArgsSize(1)==384);
  assert(AppleBootArgsSize(2)==736);
  assert(AppleBootArgsSize(3)==1152);
  assert(!AppleBootArgsSize(0) && !AppleBootArgsSize(4));
  reset(); assert(RUN()==AdtCopyGeometryValid);
  assert(p.AdtPages.Size==0x8000 && p.BootArgsPages.Size==4096);
  assert(p.Arena.Base==0x9f0000000ULL && p.AdtSource.Base==0x810000000ULL);
  BAD(a.revision=4); BAD(a.version=0); BAD(a.version=4);
  BAD(a.mem_size=0); BAD(a.mem_size=UINT64_MAX);
  BAD(a.devtree=(void *)(uintptr_t)(a.virt_base-1));
  BAD(a.devtree=(void *)(uintptr_t)(a.virt_base+a.mem_size));
  BAD(a.devtree_size=0);
  BAD(a.devtree=(void *)(uintptr_t)(a.virt_base+a.mem_size-1));
  BAD(a.devtree=(void *)(uintptr_t)(a.virt_base+0x40000000));
  BAD(a.video.base=0x840000000ULL; a.video.stride=64; a.video.height=64);
  BAD(a.video.stride=UINT64_MAX; a.video.height=2);
  BAD(a.video.stride=UINT64_MAX; a.video.height=1);
  BAD(a.video.base=UINT64_MAX-4096; a.video.stride=64; a.video.height=64);
  reset(); assert(run(0x840004000ULL,0x840000000ULL,0x840004000ULL,0x830000000ULL,0x2000000,0x10000000)==AdtCopyInvalid);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,0x840000800ULL,0x830000000ULL,0x2000000,0x10000000)==AdtCopyInvalid);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,0x840004000ULL,0x840000000ULL,0x2000000,0x10000000)==AdtCopyInvalid);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,0x9f0000000ULL,0x830000000ULL,0x2000000,0x10000000)==AdtCopyInvalid);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,UINT64_MAX-15,0x830000000ULL,0x2000000,0x10000000)==AdtCopyInvalid);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,0x840004000ULL,0x9ff000000ULL,0x1000000,0x10000000)==AdtCopyGeometryValid);
  assert(p.Arena.Base==0x9ef000001ULL);
  reset(); assert(run(0x820000000ULL,0x840000000ULL,0x840004000ULL,0x1000000000ULL,0x1000000,0x10000000)==AdtCopyGeometryValid);
  assert(p.Arena.Base==0x9f0000000ULL);
  // No arbitrary ADT size cap: 2 MiB is geometrically valid here.
  reset(); a.devtree_size=0x200000; assert(RUN()==AdtCopyGeometryValid);
  // Revision one need not have readable bytes for the largest union.
  reset(); a.revision=1; assert(RUN()==AdtCopyGeometryValid); assert(p.BootArgsSize==384);
  printf("production AdtCopyPlan: %u range cases passed\n", count);
}
"""
class AdtCopyPlanTests(unittest.TestCase):
    def test_production_range_checks(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            (tmp / "Uefi.h").write_text(SHIM)
            (tmp / "test.c").write_text(TEST)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I" + str(tmp),
                            "-I" + str(PKG / "Include"), "-I" + str(PKG / "PrePi"),
                            str(PKG / "PrePi/AdtCopyPlan.c"), str(tmp / "test.c"),
                            "-o", str(tmp / "test")], check=True)
            subprocess.run([str(tmp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
