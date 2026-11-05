/* SPDX-License-Identifier: MIT */
#include "raw_adt.h"
#include <string.h>

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int eq(const unsigned char *b, size_t off, const char *s)
{
    return strcmp((const char *)b + off, s) == 0;
}
/* Require exactly one terminating NUL, printable nonempty path component. */
static int node_name(const unsigned char *p, size_t n)
{
    size_t i;
    if (n < 2 || n > 256 || p[n - 1] != 0) return 0;
    for (i = 0; i + 1 < n; ++i)
        if (p[i] < 33 || p[i] > 126 || p[i] == '/') return 0;
    if ((n == 2 && p[0] == '.') ||
        (n == 3 && p[0] == '.' && p[1] == '.')) return 0;
    return 1;
}
static int strings(const unsigned char *p, size_t n)
{
    size_t i, start = 0;
    if (!n || p[n - 1]) return 0;
    for (i = 0; i < n; ++i) {
        if (!p[i]) { if (i == start) return 0; start = i + 1; }
        else if (p[i] < 32 || p[i] > 126) return 0;
    }
    return 1;
}
radt_status radt_validate(const void *buffer, size_t size, radt_workspace *w)
{
    const unsigned char *b = buffer;
    struct { size_t index; uint32_t left; } stack[RADT_DEPTH];
    size_t depth = 0, off = 0;
    if (!w) return RADT_ARGUMENT;
    w->node_count = w->prop_count = 0;
    if (!b) return RADT_ARGUMENT;
    if (size > RADT_BYTES) return RADT_BUDGET;
    if (size >= 4 && b[0] == 0xd0 && b[1] == 0x0d &&
        b[2] == 0xfe && b[3] == 0xed) return RADT_UNSUPPORTED;
    do {
        uint32_t np, nc;
        size_t idx, parent, i, j;
        radt_node *node;
        if (depth == RADT_DEPTH || w->node_count == RADT_NODES)
            return RADT_BUDGET;
        if (size - off < 8) return RADT_TRUNCATED;
        np = le32(b + off); nc = le32(b + off + 4); off += 8;
        if (np > RADT_PROPS - w->prop_count ||
            nc > RADT_NODES - w->node_count - 1) return RADT_BUDGET;
        idx = w->node_count++;
        parent = depth ? stack[depth - 1].index : SIZE_MAX;
        node = &w->nodes[idx];
        *node = (radt_node){parent, w->prop_count, np, SIZE_MAX};
        for (i = 0; i < np; ++i) {
            size_t k, len, pad, keyoff = off;
            radt_property *p;
            if (size - off < 36) return RADT_TRUNCATED;
            for (k = 0; k < 32 && b[off + k]; ++k)
                if (b[off + k] < 33 || b[off + k] > 126)
                    return RADT_FORMAT;
            if (!k || k == 32) return RADT_FORMAT;
            /* Trailing key bytes are not semantically significant. */
            for (j = node->first; j < w->prop_count; ++j)
                if (eq(b, w->props[j].key, (const char *)b + off))
                    return RADT_DUPLICATE;
            len = le32(b + off + 32);
            /* bit31 templates and bits24..30 deliberately unsupported. */
            if (len & UINT32_C(0xff000000)) return RADT_UNSUPPORTED;
            off += 36;
            if (len > size - off) return RADT_TRUNCATED;
            pad = (4 - (len & 3)) & 3;
            if (pad > size - off - len) return RADT_TRUNCATED;
            p = &w->props[w->prop_count++];
            *p = (radt_property){keyoff, off, len};
            if (eq(b, keyoff, "name")) {
                if (!node_name(b + off, len)) return RADT_FORMAT;
                node->name = off;
            }
            if (eq(b, keyoff, "compatible") && !strings(b + off, len))
                return RADT_FORMAT;
            off += len + pad;
        }
        if (node->name == SIZE_MAX) return RADT_FORMAT;
        for (j = 1; j < idx; ++j)
            if (w->nodes[j].parent == parent &&
                eq(b, w->nodes[j].name, (const char *)b + node->name))
                return RADT_DUPLICATE;
        if (depth) --stack[depth - 1].left;
        stack[depth].index = idx; stack[depth].left = nc; ++depth;
        while (depth && !stack[depth - 1].left) --depth;
    } while (depth);
    return off == size ? RADT_OK : RADT_FORMAT;
}
static const radt_property *prop(const unsigned char *b,
    const radt_workspace *w, size_t node, const char *key)
{
    size_t i;
    for (i = w->nodes[node].first;
         i < w->nodes[node].first + w->nodes[node].count; ++i)
        if (eq(b, w->props[i].key, key)) return &w->props[i];
    return NULL;
}
static size_t exact_path(const unsigned char *b, const radt_workspace *w,
                         const char *path)
{
    size_t node = 0;
    const char *s = path + 1;
    while (*s) {
        const char *end = strchr(s, '/');
        size_t len = end ? (size_t)(end - s) : strlen(s), i;
        if (!len) return SIZE_MAX;
        for (i = 1; i < w->node_count; ++i)
            if (w->nodes[i].parent == node &&
                strlen((const char *)b + w->nodes[i].name) == len &&
                !memcmp(b + w->nodes[i].name, s, len)) break;
        if (i == w->node_count) return SIZE_MAX;
        node = i;
        if (!end) return node;
        s = end + 1;
        if (!*s) return SIZE_MAX;
    }
    return node;
}
static int compatible(const unsigned char *b, const radt_workspace *w,
                       size_t node, const char *wanted)
{
    const radt_property *p = prop(b, w, node, "compatible");
    size_t i = 0;
    if (!p) return 0;
    while (i < p->size) {
        if (eq(b, p->data + i, wanted)) return 1;
        i += strlen((const char *)b + p->data + i) + 1;
    }
    return 0;
}
static int allowed(const char *path, const char *key)
{
    static const char *const common[] = {"name", "compatible", "reg",
        "interrupts", "interrupt-parent", "AAPL,phandle", "#gpio-cells",
        "gpio-controller", "function-spi_en", "function-reset",
        "function-gpio", "function-irq", "clock-gates", "clock-frequency"};
    size_t i;
    if (!strcmp(path, "/"))
        return !strcmp(key, "model") || !strcmp(key, "compatible") ||
               !strcmp(key, "board-id");
    if (!strcmp(path, "/arm-io"))
        return !strcmp(key, "ranges") || !strcmp(key, "#address-cells") ||
               !strcmp(key, "#size-cells") || !strcmp(key, "compatible");
    /* Board-audit candidates only: exact node-specific raw metadata. */
    if (!strcmp(path, "/arm-io/spi3") && !strcmp(key, "function-spi_cs0"))
        return 1;
    if (!strcmp(path, "/arm-io/spi3/ipd") && !strcmp(key, "function-enable_cs"))
        return 1;
    if (strcmp(path, "/arm-io/spi3") && strcmp(path, "/arm-io/spi3/ipd") &&
        strcmp(path, "/arm-io/gpio") && strcmp(path, "/arm-io/nub-gpio"))
        return 0;
    for (i = 0; i < sizeof(common) / sizeof(common[0]); ++i)
        if (!strcmp(key, common[i])) return 1;
    return 0;
}
radt_status radt_lookup(const void *buffer, size_t size, radt_workspace *w,
                        const char *path, const char *key, radt_view *out)
{
    const unsigned char *b = buffer;
    radt_status status;
    size_t node, spi;
    const radt_property *p;
    if (!out) return RADT_ARGUMENT;
    *out = (radt_view){NULL, 0};
    if (!path || !key) return RADT_ARGUMENT;
    if (!allowed(path, key)) return RADT_DENIED;
    status = radt_validate(buffer, size, w);
    if (status != RADT_OK) return status;
    node = exact_path(b, w, path);
    if (node == SIZE_MAX) return RADT_NOT_FOUND;
    if (!strcmp(path, "/arm-io/spi3") || !strcmp(path, "/arm-io/spi3/ipd")) {
        spi = exact_path(b, w, "/arm-io/spi3");
        if (spi == SIZE_MAX || !compatible(b, w, spi, "spi-1,spimc"))
            return RADT_INCOMPATIBLE;
        if (!strcmp(path, "/arm-io/spi3/ipd") &&
            !compatible(b, w, node, "hid-transport,spi"))
            return RADT_INCOMPATIBLE;
    }
    p = prop(b, w, node, key);
    if (!p) return RADT_NOT_FOUND;
    *out = (radt_view){b + p->data, p->size};
    return RADT_OK;
}
