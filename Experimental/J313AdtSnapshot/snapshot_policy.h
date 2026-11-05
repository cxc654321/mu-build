/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef J313_SNAPSHOT_POLICY_H
#define J313_SNAPSHOT_POLICY_H
#include <stddef.h>
#include <stdint.h>

#define SNAP_MAX_DESCRIPTORS 4096u
#define SNAP_MAX_BLOB_SIZE UINT64_C(16777216) /* experimental ceiling, not ADT ABI */
#define SNAP_EFI_WP UINT64_C(0x1000)
#define SNAP_EFI_RP UINT64_C(0x2000)
#define SNAP_EFI_RO UINT64_C(0x20000)

typedef struct { uint64_t base, length; } SnapSpan;
/* PhysicalStart, checked NumberOfPages * 4096, Attribute, Type.
 * Never cast EFI_MEMORY_DESCRIPTOR buffers to this host representation. */
typedef struct {
    uint64_t base, length, attributes;
    uint32_t type;
    uint32_t protected_region; /* caller knowledge beyond EFI attributes */
} SnapMemory;
typedef enum {
    SNAP_OK = 0, SNAP_ARGUMENT, SNAP_OVERFLOW, SNAP_SIZE,
    SNAP_BAD_MAP, SNAP_OVERLAP, SNAP_HOLE, SNAP_TYPE, SNAP_PROTECTED
} SnapStatus;
typedef struct {
    SnapSpan bootargs_readable;
    SnapSpan adt_readable;
} SnapPlan;

/* All spans are physical numeric half-open ranges, never pointers.
 * On failure output is zero. Caller provides valid, non-aliasing host objects.
 * Success is only a descriptor policy result, NOT page-table verification.
 * Validate bootargs BEFORE attempting to extract devtree_size on firmware. */
SnapStatus snap_check_range(const SnapMemory *map, size_t count,
                            SnapSpan range, SnapSpan *readable);
/* adt_span is independently known capacity, NOT capacity inferred from size.
 * length is an already safely obtained bootargs devtree_size scalar.
 * cap must be 1..SNAP_MAX_BLOB_SIZE. No allocation, read, or copy occurs. */
SnapStatus snap_plan(const SnapMemory *map, size_t count,
                     SnapSpan bootargs_span, SnapSpan adt_span,
                     uint64_t length, uint64_t cap, SnapPlan *plan);
#endif
