/* SPDX-License-Identifier: MIT
 * Independent host-only bounded ADT reader. See README.md for format references.
 */
#ifndef J313_RAW_ADT_H
#define J313_RAW_ADT_H
#include <stddef.h>
#include <stdint.h>
#define RADT_DEPTH 8u
#define RADT_NODES 256u
#define RADT_PROPS 2048u
#define RADT_BYTES (16u * 1024u * 1024u)
typedef enum { RADT_OK, RADT_ARGUMENT, RADT_FORMAT, RADT_TRUNCATED,
    RADT_BUDGET, RADT_UNSUPPORTED, RADT_DUPLICATE, RADT_NOT_FOUND,
    RADT_DENIED, RADT_INCOMPATIBLE } radt_status;
typedef struct { const unsigned char *data; size_t size; } radt_view;
typedef struct { size_t parent, first, count, name; } radt_node;
typedef struct { size_t key, data, size; } radt_property;
typedef struct {
    radt_node nodes[RADT_NODES];
    radt_property props[RADT_PROPS];
    size_t node_count, prop_count;
} radt_workspace;
/* b must be an immutable mapped ordinary-memory span of exactly size bytes.
 * w and out must not overlap b or each other. No allocation or MMIO.
 * Workspace contents are scratch, invalid on error, not an export interface.
 */
radt_status radt_validate(const void *b, size_t size, radt_workspace *w);
/* Exact absolute paths only; explicitly allowlisted metadata only.
 * Revalidates entire blob on each call; zeroes out on every failure.
 * Returned bytes borrow b; no address translation or numeric reinterpretation.
 */
radt_status radt_lookup(const void *b, size_t size, radt_workspace *w,
                        const char *path, const char *key, radt_view *out);
#endif
