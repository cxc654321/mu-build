/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "snapshot_policy.h"

static SnapStatus span_end(SnapSpan s, uint64_t *end)
{
    if (s.length == 0) return SNAP_SIZE;
    if (s.length > UINT64_MAX - s.base) return SNAP_OVERFLOW;
    *end = s.base + s.length;
    return SNAP_OK;
}

static int intersects(uint64_t a, uint64_t ae, uint64_t b, uint64_t be)
{
    return a < be && b < ae;
}

static int allowed_type(uint32_t type)
{
    /* EFI Loader/BootServices/RuntimeServices code+data, Conventional,
     * ACPI reclaim/NVS only. Reserved, MMIO, unusable, persistent,
     * unaccepted and unknown types are deliberately not accepted. */
    return (type >= 1 && type <= 7) || type == 9 || type == 10;
}

SnapStatus snap_check_range(const SnapMemory *map, size_t count,
                            SnapSpan range, SnapSpan *readable)
{
    uint64_t end, cursor;
    size_t i, j;
    SnapStatus status;
    if (readable == NULL) return SNAP_ARGUMENT;
    *readable = (SnapSpan){0, 0};
    if (map == NULL || count == 0 || count > SNAP_MAX_DESCRIPTORS)
        return SNAP_ARGUMENT;
    status = span_end(range, &end);
    if (status != SNAP_OK) return status;
    /* Fail closed on malformed or overlapping descriptors ANYWHERE in map.
     * Order does not matter. Bound count to bound quadratic work. */
    for (i = 0; i < count; ++i) {
        uint64_t ie;
        status = span_end((SnapSpan){map[i].base, map[i].length}, &ie);
        if (status != SNAP_OK) return status;
        if ((map[i].base & 4095) || (map[i].length & 4095))
            return SNAP_BAD_MAP;
        for (j = 0; j < i; ++j) {
            uint64_t je = map[j].base + map[j].length; /* validated */
            if (intersects(map[i].base, ie, map[j].base, je))
                return SNAP_OVERLAP;
        }
        if (intersects(range.base, end, map[i].base, ie)) {
            if (!allowed_type(map[i].type)) return SNAP_TYPE;
            if (map[i].protected_region ||
                (map[i].attributes & (SNAP_EFI_WP | SNAP_EFI_RP | SNAP_EFI_RO)))
                return SNAP_PROTECTED;
        }
    }
    cursor = range.base;
    while (cursor < end) {
        int found = 0;
        for (i = 0; i < count; ++i) {
            uint64_t ie = map[i].base + map[i].length;
            if (map[i].base <= cursor && cursor < ie) {
                cursor = ie < end ? ie : end;
                found = 1;
                break;
            }
        }
        if (!found) return SNAP_HOLE;
    }
    *readable = range;
    return SNAP_OK;
}

SnapStatus snap_plan(const SnapMemory *map, size_t count,
                     SnapSpan bootargs_span, SnapSpan adt_span,
                     uint64_t length, uint64_t cap, SnapPlan *plan)
{
    uint64_t be, ae;
    SnapPlan local = {{0, 0}, {0, 0}};
    SnapStatus status;
    if (plan == NULL) return SNAP_ARGUMENT;
    *plan = local;
    status = span_end(bootargs_span, &be);
    if (status != SNAP_OK) return status;
    status = span_end(adt_span, &ae);
    if (status != SNAP_OK) return status;
    if (cap == 0 || cap > SNAP_MAX_BLOB_SIZE || length == 0 ||
        length > cap || length > adt_span.length || length > SIZE_MAX)
        return SNAP_SIZE;
    if (intersects(bootargs_span.base, be, adt_span.base, ae))
        return SNAP_OVERLAP;
    status = snap_check_range(map, count, bootargs_span, &local.bootargs_readable);
    if (status != SNAP_OK) return status;
    status = snap_check_range(map, count, (SnapSpan){adt_span.base, length},
                              &local.adt_readable);
    if (status != SNAP_OK) return status;
    *plan = local;
    return SNAP_OK;
}
