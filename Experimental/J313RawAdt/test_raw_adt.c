/* SPDX-License-Identifier: MIT */
#include "raw_adt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char b[131072];
static size_t used;
static radt_workspace w;
static void put32(size_t at, uint32_t v)
{ size_t i; for (i = 0; i < 4; ++i) b[at+i] = (unsigned char)(v >> (8*i)); }
static void reset(void) { memset(b, 0, sizeof(b)); used = 0; }
static size_t property(const char *key, const void *data, size_t n)
{
    size_t at = used;
    assert(strlen(key) < 32 && used + 36 + n + 3 <= sizeof(b));
    memcpy(b + used, key, strlen(key)); put32(used + 32, (uint32_t)n);
    if (n) memcpy(b + used + 36, data, n);
    used += 36 + ((n + 3) & ~(size_t)3); return at;
}
static size_t node(const char *name, uint32_t extra, uint32_t children)
{
    size_t at = used;
    assert(used + 8 <= sizeof(b));
    put32(used, extra + 1); put32(used + 4, children); used += 8;
    property("name", name, strlen(name) + 1); return at;
}
static void valid(void)
{
    const unsigned char reg[] = {1,2,3,4,5};
    reset(); node("device-tree", 2, 1);
    property("model", "J313", 5); property("serial-number", "NOT-EXPORTED", 13);
    node("arm-io", 0, 2); node("spi3", 2, 1);
    property("compatible", "spi-1,spimc", sizeof("spi-1,spimc"));
    property("reg", reg, sizeof(reg)); node("ipd", 1, 0);
    property("compatible", "hid-transport,spi", sizeof("hid-transport,spi"));
    node("gpio", 1, 0); property("gpio-controller", NULL, 0);
}
static void failure(const void *p, size_t n)
{
    radt_view v = {b, 42};
    assert(radt_validate(p, n, &w) != RADT_OK);
    assert(radt_lookup(p, n, &w, "/", "model", &v) != RADT_OK);
    assert(!v.data && !v.size);
}
static uint32_t rng = 0x12345678;
static uint32_t random32(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(void)
{
    size_t i, j, n, at; radt_view v;
    unsigned char *copy;
    valid(); n = used;
    assert(radt_validate(b, used, &w) == RADT_OK);
    assert(w.node_count == 5);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3", "reg", &v) == RADT_OK);
    assert(v.size == 5 && v.data[4] == 5);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3/ipd", "compatible", &v) == RADT_OK);
    assert(radt_lookup(b, used, &w, "/arm-io/gpio", "gpio-controller", &v) == RADT_OK);
    assert(v.data && !v.size);
    assert(radt_lookup(b, used, &w, "/", "serial-number", &v) == RADT_DENIED);
    assert(!v.data && !v.size);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3/", "reg", &v) == RADT_DENIED);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3", "ranges", &v) == RADT_DENIED);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3", "interrupts", &v) == RADT_NOT_FOUND);
    /* Separate allocations make every prefix's end an ASan boundary. */
    for (i = 0; i < n; ++i) {
        copy = malloc(i ? i : 1); assert(copy); memcpy(copy, b, i);
        failure(copy, i); free(copy);
    }
    for (i = 1; i <= 7; ++i) {
        copy = malloc(n + i); assert(copy); memcpy(copy + i, b, n);
        assert(radt_validate(copy + i, n, &w) == RADT_OK); free(copy);
    }
    failure(b, used + 1);
    for (i = 24; i < 32; ++i) {
        valid(); put32(40, (UINT32_C(1) << i) | 12);
        assert(radt_validate(b, used, &w) == RADT_UNSUPPORTED);
    }
    valid(); put32(40, UINT32_MAX); failure(b, used);
    valid(); put32(40, 0xffffff); failure(b, used);
    valid(); put32(0, UINT32_MAX); failure(b, used);
    valid(); put32(4, UINT32_MAX); failure(b, used);
    valid(); memset(b + 8, 'x', 32); failure(b, used);
    valid(); b[44] = 0; failure(b, used);
    valid(); b[55] = 'X'; failure(b, used);
    reset(); node("root", 1, 0); property("name", "other", 6);
    assert(radt_validate(b, used, &w) == RADT_DUPLICATE);
    reset(); node("root", 0, 2); node("same", 0, 0); node("same", 0, 0);
    assert(radt_validate(b, used, &w) == RADT_DUPLICATE);
    reset(); node("root", 0, 0); put32(0, 0); used = 8; failure(b, used);
    reset(); for (i = 0; i < 8; ++i) node("nested", 0, i != 7);
    assert(radt_validate(b, used, &w) == RADT_OK);
    reset(); for (i = 0; i < 9; ++i) node("nested", 0, i != 8);
    assert(radt_validate(b, used, &w) == RADT_BUDGET);
    reset(); node("root", 0, 1); node("arm-io", 0, 1);
    node("spi3", 1, 0); at = property("compatible", "spi-1,spimcX", 13);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3", "compatible", &v) == RADT_INCOMPATIBLE);
    b[at + 36 + 12] = 'x'; failure(b, used);
    reset(); node("root", 0, 0);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3", "reg", &v) == RADT_NOT_FOUND);
    b[0]=0xd0; b[1]=0x0d; b[2]=0xfe; b[3]=0xed;
    assert(radt_validate(b, used, &w) == RADT_UNSUPPORTED);
    assert(radt_validate(NULL, 0, &w) == RADT_ARGUMENT);
    /* Global budgets, not merely per-node count checks. */
    reset(); node("root", 0, 255);
    for (i = 0; i < 255; ++i) {
        char name[32]; (void)snprintf(name, sizeof(name), "n%zu", i);
        node(name, 0, 0);
    }
    assert(radt_validate(b, used, &w) == RADT_OK);
    put32(4, 256); assert(radt_validate(b, used, &w) == RADT_BUDGET);
    reset(); node("root", 2047, 1);
    for (i = 0; i < 2047; ++i) {
        char key[32]; (void)snprintf(key, sizeof(key), "p%zu", i);
        property(key, NULL, 0);
    }
    node("child", 0, 0);
    assert(radt_validate(b, used, &w) == RADT_BUDGET);
    assert(radt_validate(b, (size_t)RADT_BYTES + 1, &w) == RADT_BUDGET);
    /* Random bytes and structure-preserving seed mutations, deterministic. */
    for (i = 0; i < 30000; ++i) {
        if (i & 1) { valid(); for (j = 0; j < 1 + i % 8; ++j)
            b[random32() % used] = (unsigned char)random32(); }
        else { used = random32() % 2048;
            for (j = 0; j < used; ++j) b[j] = (unsigned char)random32(); }
        (void)radt_validate(b, used, &w);
        if (radt_lookup(b, used, &w, "/arm-io/spi3", "reg", &v) != RADT_OK)
            assert(!v.data && !v.size);
        else assert(v.data >= b && v.data <= b + used && v.size <= (size_t)(b + used - v.data));
    }
    puts("PASS: valid/prefix/unaligned/depth/count/flags/duplicate/malformed/privacy + 30000 fuzz cases");
    return 0;
}
