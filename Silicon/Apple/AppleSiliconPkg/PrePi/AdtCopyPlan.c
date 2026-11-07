/** SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdtCopyPlan.h"

// m1n1 src/xnuboot.h, AArch64 ABI. Revision is not version.
STATIC_ASSERT (sizeof (VOID *) == 8, "boot_args requires AArch64 pointers");
STATIC_ASSERT (offsetof (struct boot_args, virt_base) == 8, "virt_base ABI");
STATIC_ASSERT (offsetof (struct boot_args, video) == 40, "video ABI");
STATIC_ASSERT (offsetof (struct boot_args, devtree) == 96, "devtree ABI");
STATIC_ASSERT (offsetof (struct boot_args, devtree_size) == 104, "size ABI");
STATIC_ASSERT (offsetof (struct boot_args, rv1) == 112, "union ABI");
STATIC_ASSERT (sizeof (((struct boot_args *)0)->rv1) == 272, "rv1 ABI");
STATIC_ASSERT (sizeof (((struct boot_args *)0)->rv2) == 624, "rv2 ABI");
STATIC_ASSERT (sizeof (((struct boot_args *)0)->rv3) == 1040, "rv3 ABI");
STATIC_ASSERT (sizeof (struct boot_args) == 1152, "boot_args ABI");

UINTN
AppleBootArgsSize (UINT16 Revision)
{
  switch (Revision) {
    case 1: return offsetof (struct boot_args, rv1) + sizeof (((struct boot_args *)0)->rv1);
    case 2: return offsetof (struct boot_args, rv2) + sizeof (((struct boot_args *)0)->rv2);
    case 3: return offsetof (struct boot_args, rv3) + sizeof (((struct boot_args *)0)->rv3);
    default: return 0;
  }
}

STATIC BOOLEAN
Range (UINT64 Base, UINT64 Size, ADT_COPY_RANGE *Out)
{
  if ((Size == 0) || (Base > MAX_UINT64 - Size)) {
    return FALSE;
  }
  Out->Base = Base;
  Out->Size = Size;
  return TRUE;
}

STATIC BOOLEAN
Contains (ADT_COPY_RANGE Outer, ADT_COPY_RANGE Inner)
{
  return (Inner.Base >= Outer.Base) &&
         (Inner.Base + Inner.Size <= Outer.Base + Outer.Size);
}

STATIC BOOLEAN
Overlap (ADT_COPY_RANGE A, ADT_COPY_RANGE B)
{
  return (A.Base < B.Base + B.Size) && (B.Base < A.Base + A.Size);
}

STATIC BOOLEAN
Pages (UINT64 Base, UINT64 Size, ADT_COPY_RANGE *Out)
{
  UINT64 End;
  if ((Size == 0) || (Base > MAX_UINT64 - Size) ||
      (Base + Size > MAX_UINT64 - 4095)) {
    return FALSE;
  }
  End = (Base + Size + 4095) & ~((UINT64)4095);
  return Range (Base & ~((UINT64)4095), End - (Base & ~((UINT64)4095)), Out);
}

ADT_COPY_RESULT
AppleAdtPlanCopy (
  CONST struct boot_args *Args, UINT64 ArgsAddress,
  UINT64 BootDest, UINT64 AdtDest, UINT64 FdBase, UINT64 FdSize,
  UINT64 ArenaSize, ADT_COPY_PLAN *Plan
  )
{
  ADT_COPY_PLAN P;
  ADT_COPY_RANGE Dram, Input, Fd, Fb;
  UINT64 Offset, Top, FdTop, FbSize;

  if ((Args == NULL) || (Plan == NULL)) {
    return AdtCopyInvalid;
  }
  P.BootArgsSize = AppleBootArgsSize (Args->revision);
  if ((P.BootArgsSize == 0) || (Args->version == 0) || (Args->version > 3) ||
      !Range (Args->phys_base, Args->mem_size, &Dram) ||
      !Range (ArgsAddress, P.BootArgsSize, &Input) || !Contains (Dram, Input) ||
      !Range (FdBase, FdSize, &Fd) ||
      ((UINT64)(UINTN)Args->devtree < Args->virt_base)) {
    return AdtCopyInvalid;
  }
  Offset = (UINT64)(UINTN)Args->devtree - Args->virt_base;
  if ((Offset >= Dram.Size) || (Offset > MAX_UINT64 - Dram.Base) ||
      !Range (Dram.Base + Offset, Args->devtree_size, &P.AdtSource) ||
      !Contains (Dram, P.AdtSource) ||
      !Pages (BootDest, P.BootArgsSize, &P.BootArgsPages) ||
      !Pages (AdtDest, Args->devtree_size, &P.AdtPages) ||
      !Contains (Dram, P.BootArgsPages) || !Contains (Dram, P.AdtPages)) {
    return AdtCopyInvalid;
  }

  // Mirror the normal non-overflow arena selection (inclusive tops in asm).
  // FdBase + 1 reflects the existing assembly branch, not an alignment fix.
  // Excluding the whole FD also excludes its intended temporary-stack window;
  // this does not prove that an overflowing temporary stack stays inside FD.
  Top = Dram.Base + Dram.Size;
  FdTop = Fd.Base + Fd.Size;
  if ((FdTop <= Top) && (Top - FdTop < ArenaSize)) {
    Top = Fd.Base + 1;
  }
  if ((ArenaSize < 0x20040) || (Top < ArenaSize) ||
      !Range (Top - ArenaSize, ArenaSize, &P.Arena) || !Contains (Dram, P.Arena)) {
    return AdtCopyInvalid;
  }
  if (Overlap (P.BootArgsPages, P.AdtPages) ||
      Overlap (P.BootArgsPages, Input) || Overlap (P.AdtPages, Input) ||
      Overlap (P.BootArgsPages, P.AdtSource) || Overlap (P.AdtPages, P.AdtSource) ||
      Overlap (P.BootArgsPages, Fd) || Overlap (P.AdtPages, Fd) ||
      Overlap (P.BootArgsPages, P.Arena) || Overlap (P.AdtPages, P.Arena)) {
    return AdtCopyInvalid;
  }
  if ((Args->video.stride != 0) &&
      (Args->video.height > MAX_UINT64 / Args->video.stride)) {
    return AdtCopyInvalid;
  }
  FbSize = Args->video.stride * Args->video.height;
  if (FbSize != 0) {
    if ((FbSize > MAX_UINT64 - 0x3fff) ||
        !Range (Args->video.base, (FbSize + 0x3fff) & ~((UINT64)0x3fff), &Fb) ||
        Overlap (P.BootArgsPages, Fb) || Overlap (P.AdtPages, Fb)) {
      return AdtCopyInvalid;
    }
  }
  *Plan = P;
  // NOT a complete ownership check: loader heap, carveouts and other live
  // ranges are not enumerated by the current entry ABI. Do not call this safe.
  return AdtCopyGeometryValid;
}
