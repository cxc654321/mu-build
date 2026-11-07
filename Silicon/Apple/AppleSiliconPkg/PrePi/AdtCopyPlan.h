/** SPDX-License-Identifier: BSD-2-Clause-Patent
 * Read-only preflight; a geometrically valid plan is NOT ownership evidence.
 */
#ifndef APPLE_ADT_COPY_PLAN_H
#define APPLE_ADT_COPY_PLAN_H
#include <Uefi.h>
#include <Library/AppleDTLib.h>

typedef struct {
  UINT64 Base;
  UINT64 Size;
} ADT_COPY_RANGE;

typedef struct {
  UINT64 BootArgsSize;
  ADT_COPY_RANGE BootArgsPages;
  ADT_COPY_RANGE AdtPages;
  ADT_COPY_RANGE AdtSource;
  ADT_COPY_RANGE Arena;
} ADT_COPY_PLAN;

typedef enum {
  AdtCopyInvalid,
  // Geometry only: no loader ownership grant exists in the current ABI.
  AdtCopyGeometryValid
} ADT_COPY_RESULT;

UINTN AppleBootArgsSize (UINT16 Revision);
ADT_COPY_RESULT AppleAdtPlanCopy (
  CONST struct boot_args *Args,
  UINT64 ArgsAddress,
  UINT64 BootDest,
  UINT64 AdtDest,
  UINT64 FdBase,
  UINT64 FdSize,
  UINT64 ArenaSize,
  ADT_COPY_PLAN *Plan
  );
#endif
