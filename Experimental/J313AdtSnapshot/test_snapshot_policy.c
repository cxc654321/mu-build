/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "snapshot_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define B UINT64_C(0x840000000)
#define A UINT64_C(0x840004000)
static unsigned checks;
static void check(SnapStatus got, SnapStatus want) { ++checks; assert(got == want); }
static void range_tests(void)
{
    SnapMemory m[3] = {{B,0x4000,0,4,0},{A,0x1000,0,4,0},{A+0x1000,0x1000,0,7,0}};
    SnapSpan out;
    SnapSpan r = {A+0xfff,2};
    check(snap_check_range(m,3,r,&out),SNAP_OK);
    assert(out.base == r.base && out.length == 2);
    { SnapMemory t=m[0]; m[0]=m[2]; m[2]=t; }
    check(snap_check_range(m,3,r,&out),SNAP_OK); /* unsorted */
    m[0].base += 0x1000;
    check(snap_check_range(m,3,r,&out),SNAP_HOLE);
    assert(out.base == 0 && out.length == 0);
    m[0].base = A;
    check(snap_check_range(m,3,r,&out),SNAP_OVERLAP);
    m[0].base = A+0x1000;
    for (uint32_t t=0;t<32;++t) {
        m[0].type=t;
        check(snap_check_range(m,3,r,&out),
              ((t>=1 && t<=7)||t==9||t==10)?SNAP_OK:SNAP_TYPE);
    }
    m[0].type=UINT32_MAX;
    check(snap_check_range(m,3,r,&out),SNAP_TYPE);
    m[0].type=4;
    const uint64_t attrs[]={SNAP_EFI_WP,SNAP_EFI_RP,SNAP_EFI_RO};
    for(size_t i=0;i<3;++i) {
        m[0].attributes=attrs[i];
        check(snap_check_range(m,3,r,&out),SNAP_PROTECTED);
    }
    m[0].attributes=0; m[0].protected_region=1;
    check(snap_check_range(m,3,r,&out),SNAP_PROTECTED);
    m[0].protected_region=0;
    check(snap_check_range(m,3,(SnapSpan){A+0x2000,1},&out),SNAP_HOLE);
    check(snap_check_range(m,3,(SnapSpan){A,0x2000},&out),SNAP_OK);
    check(snap_check_range(m,3,(SnapSpan){A,0},&out),SNAP_SIZE);
    check(snap_check_range(m,3,(SnapSpan){UINT64_MAX,1},&out),SNAP_OVERFLOW);
    m[0].base=UINT64_MAX-4095;
    check(snap_check_range(m,3,r,&out),SNAP_OVERFLOW);
    m[0].base=A+0x1001;
    check(snap_check_range(m,3,r,&out),SNAP_BAD_MAP);
    m[0].base=A+0x1000; m[0].length=0;
    check(snap_check_range(m,3,r,&out),SNAP_SIZE);
    check(snap_check_range(NULL,3,r,&out),SNAP_ARGUMENT);
    check(snap_check_range(m,0,r,&out),SNAP_ARGUMENT);
    check(snap_check_range(m,SNAP_MAX_DESCRIPTORS+1,r,&out),SNAP_ARGUMENT);
    check(snap_check_range(m,3,r,NULL),SNAP_ARGUMENT);
    m[0]=(SnapMemory){UINT64_C(0xffffffffffffd000),0x1000,0,4,0};
    check(snap_check_range(m,1,(SnapSpan){m[0].base+4095,1},&out),SNAP_OK);
    m[0]=(SnapMemory){0,0x1000,0,4,0};
    check(snap_check_range(m,1,(SnapSpan){0,1},&out),SNAP_OK); /* numeric only */
}
static void plan_tests(void)
{
    SnapMemory m[]={{B,0x4000,0,4,0},{A,0x2000,0,4,0}};
    SnapSpan b={B,0x1000}, a={A,0x2000};
    SnapPlan out;
    check(snap_plan(m,2,b,a,0x2000,0x2000,&out),SNAP_OK);
    assert(out.adt_readable.length==0x2000);
    check(snap_plan(m,2,b,a,0,0x2000,&out),SNAP_SIZE);
    assert(out.bootargs_readable.length==0 && out.adt_readable.length==0);
    check(snap_plan(m,2,b,a,0x2001,0x4000,&out),SNAP_SIZE);
    check(snap_plan(m,2,b,a,0x1001,0x1000,&out),SNAP_SIZE);
    check(snap_plan(m,2,b,a,1,0,&out),SNAP_SIZE);
    check(snap_plan(m,2,b,a,1,SNAP_MAX_BLOB_SIZE+1,&out),SNAP_SIZE);
    check(snap_plan(m,2,b,(SnapSpan){B+1,0x1000},1,100,&out),SNAP_OVERLAP);
    check(snap_plan(m,2,b,(SnapSpan){UINT64_MAX,2},1,100,&out),SNAP_OVERFLOW);
    check(snap_plan(m,2,(SnapSpan){UINT64_MAX,2},a,1,100,&out),SNAP_OVERFLOW);
    m[1].type=11;
    check(snap_plan(m,2,b,a,1,100,&out),SNAP_TYPE);
    assert(out.bootargs_readable.length==0 && out.adt_readable.length==0);
    check(snap_plan(m,2,b,a,1,100,NULL),SNAP_ARGUMENT);
}
/* Reproduce copy-before-check ordering with safe host arrays only.
 * A bad tree can already modify destination before the later rejection.
 * Never deliberately perform the out-of-bounds access of an oversized input. */
static void early_setup_order_model(void)
{
    unsigned char source[8]={0}, destination[8];
    unsigned copies=0, checks_done=0;
    memset(destination,0xa5,sizeof destination);
    memcpy(destination,source,sizeof source); ++copies;
    assert(copies==1 && checks_done==0 && destination[0]==0);
    ++checks_done; /* mock structural validation rejects zero-filled tree */
    assert(checks_done==1 && destination[0]!=0xa5);
    ++checks;
}
int main(void)
{
    range_tests(); plan_tests(); early_setup_order_model();
    printf("PASS: %u policy/order checks; no physical address dereferenced\n",checks);
    return 0;
}
