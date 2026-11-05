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
/* Synthetic ADT encoding, not a captured UEFI blob or processed-tree import. */
static const char *const common_keys[] = {
    "name", "compatible", "reg", "interrupts", "interrupt-parent", "AAPL,phandle",
    "#gpio-cells", "gpio-controller", "function-spi_en", "function-reset",
    "function-gpio", "function-irq", "clock-gates", "clock-frequency"
};
static const unsigned char opaque[] = {0x6e,0,0,0,0x4f,0x49,0x50,0x47,
                                      49,0,0,0,1,1,0,0};
static void board_fixture(const char *spi_token, const char *ipd_token)
{
    size_t i;
    const unsigned char cells[] = {2,0,0,0};
    const unsigned char ranges[] = {0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,
                                    0,0,0,0,1,0,0,0};
    reset(); node("device-tree", 4, 1);
    property("model", "synthetic", sizeof("synthetic"));
    property("compatible", "synthetic,root", sizeof("synthetic,root"));
    property("board-id", cells, sizeof(cells));
    property("serial-number", "PRIVATE-SENTINEL", sizeof("PRIVATE-SENTINEL"));
    node("arm-io", 4, 3);
    property("compatible", "arm-io,t8103", sizeof("arm-io,t8103"));
    property("ranges", ranges, sizeof(ranges));
    property("#address-cells", cells, sizeof(cells));
    property("#size-cells", cells, sizeof(cells));
    node("spi3", 2, 1);
    property("compatible", spi_token, strlen(spi_token) + 1);
    property("function-spi_cs0", opaque, sizeof(opaque));
    node("ipd", 2, 0);
    property("compatible", ipd_token, strlen(ipd_token) + 1);
    property("function-enable_cs", opaque, sizeof(opaque));
    for (i = 0; i < 2; ++i) {
        size_t k;
        node(i ? "nub-gpio" : "gpio", 13, 0);
        property("compatible", "gpio,t8101", sizeof("gpio,t8101"));
        for (k = 2; k < sizeof(common_keys) / sizeof(common_keys[0]); ++k)
            property(common_keys[k], opaque, !strcmp(common_keys[k], "gpio-controller") ? 0 : sizeof(opaque));
    }
}
static void expect(const char *path, const char *key, radt_status status)
{
    radt_view v = {b, 42};
    assert(radt_lookup(b, used, &w, path, key, &v) == status);
    if (status != RADT_OK) assert(!v.data && !v.size);
    else assert(v.data >= b && v.data <= b + used && v.size <= (size_t)(b + used - v.data));
}
static void board_tests(void)
{
    static const char *const paths[] = {"/", "/arm-io", "/arm-io/spi3",
        "/arm-io/spi3/ipd", "/arm-io/gpio", "/arm-io/nub-gpio"};
    static const char *const bus_keys[] = {"ranges", "#address-cells", "#size-cells", "compatible"};
    static const char *const denied_paths[] = {"arm-io", "/arm-io/", "//arm-io",
        "/arm-ioX", "/device-tree/arm-io", "/arm-io/nub-gpio/",
        "/arm-io/nub-gpioX", "/arm-io/gpio-nub", "/nub-gpio",
        "/arm-io//nub-gpio", "/arm-io/./nub-gpio", "/arm-io/spi3/nub-gpio",
        "/arm-io/spi3/ipd/", "/arm-io/spi30", "/arm-io/spi3/ipdX"};
    size_t i, j, n;
    radt_view v;
    unsigned char *copy;
    board_fixture("spi-1,spimc", "hid-transport,spi");
    assert(radt_validate(b, used, &w) == RADT_OK && w.node_count == 6);
    for (i = 0; i < 4; ++i) expect("/arm-io", bus_keys[i], RADT_OK);
    expect("/", "model", RADT_OK); expect("/", "compatible", RADT_OK);
    expect("/", "board-id", RADT_OK); expect("/", "name", RADT_DENIED);
    expect("/arm-io", "name", RADT_DENIED); expect("/arm-io", "reg", RADT_DENIED);
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        expect(paths[i], "serial-number", RADT_DENIED);
        expect(paths[i], "unknown", RADT_DENIED);
        expect(paths[i], "function-spi_cs0", i == 2 ? RADT_OK : RADT_DENIED);
        expect(paths[i], "function-enable_cs", i == 3 ? RADT_OK : RADT_DENIED);
        for (j = 0; j < 3; ++j)
            expect(paths[i], bus_keys[j], i == 1 ? RADT_OK : RADT_DENIED);
    }
    for (i = 0; i < sizeof(common_keys) / sizeof(common_keys[0]); ++i) {
        expect("/arm-io/gpio", common_keys[i], RADT_OK);
        expect("/arm-io/nub-gpio", common_keys[i], RADT_OK);
    }
    for (i = 0; i < sizeof(denied_paths) / sizeof(denied_paths[0]); ++i)
        expect(denied_paths[i], "compatible", RADT_DENIED);
    expect("/arm-io", "rangesX", RADT_DENIED);
    expect("/arm-io/spi3", "function-spi_cs", RADT_DENIED);
    expect("/arm-io/spi3/ipd", "function-enable_csX", RADT_DENIED);
    assert(radt_lookup(b, used, &w, "/arm-io/spi3/ipd", "function-enable_cs", &v) == RADT_OK);
    assert(v.size == sizeof(opaque) && !memcmp(v.data, opaque, sizeof(opaque)));
    n = used;
    for (i = 0; i < n; ++i) {
        copy = malloc(i ? i : 1); assert(copy); memcpy(copy, b, i);
        failure(copy, i); free(copy);
    }
    board_fixture("spi-1,spimcX", "hid-transport,spi");
    expect("/arm-io/spi3", "function-spi_cs0", RADT_INCOMPATIBLE);
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_INCOMPATIBLE);
    board_fixture("spi-1,spimc", "hid-transport,spiX");
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_INCOMPATIBLE);
    board_fixture("Xspi-1,spimc", "hid-transport,spi");
    expect("/arm-io/spi3", "function-spi_cs0", RADT_INCOMPATIBLE);
    board_fixture("spi-1,spimc", "Xhid-transport,spi");
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_INCOMPATIBLE);
    reset(); node("root", 0, 1); node("arm-io", 0, 1);
    node("spi3", 2, 1);
    property("compatible", "other\0spi-1,spimc", sizeof("other\0spi-1,spimc"));
    property("function-spi_cs0", opaque, sizeof(opaque));
    node("ipd", 2, 0);
    property("compatible", "other\0hid-transport,spi", sizeof("other\0hid-transport,spi"));
    property("function-enable_cs", opaque, sizeof(opaque));
    expect("/arm-io/spi3", "function-spi_cs0", RADT_OK);
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_OK);
    reset(); node("root", 0, 1); node("arm-io", 0, 1);
    node("spi3", 1, 1); property("function-spi_cs0", opaque, sizeof(opaque));
    node("ipd", 1, 0); property("function-enable_cs", opaque, sizeof(opaque));
    expect("/arm-io/spi3", "function-spi_cs0", RADT_INCOMPATIBLE);
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_INCOMPATIBLE);
    reset(); node("root", 0, 1); node("elsewhere", 0, 2);
    node("arm-io", 0, 0); node("nub-gpio", 0, 0);
    expect("/arm-io", "ranges", RADT_NOT_FOUND);
    expect("/arm-io/nub-gpio", "reg", RADT_NOT_FOUND);
    valid(); expect("/arm-io", "ranges", RADT_NOT_FOUND);
    expect("/arm-io/nub-gpio", "reg", RADT_NOT_FOUND);
    expect("/arm-io/spi3", "function-spi_cs0", RADT_NOT_FOUND);
    expect("/arm-io/spi3/ipd", "function-enable_cs", RADT_NOT_FOUND);
}
static uint32_t rng = 0x12345678;
static uint32_t random32(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
int main(void)
{
    size_t i, j, n, at; radt_view v;
    unsigned char *copy;
    board_tests();
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
        if (i & 1) {
            if (i & 2) board_fixture("spi-1,spimc", "hid-transport,spi");
            else valid();
            for (j = 0; j < 1 + i % 8; ++j)
            b[random32() % used] = (unsigned char)random32(); }
        else { used = random32() % 2048;
            for (j = 0; j < used; ++j) b[j] = (unsigned char)random32(); }
        {
            static const char *const paths[] = {"/arm-io", "/arm-io", "/arm-io",
                "/arm-io", "/arm-io/nub-gpio", "/arm-io/spi3", "/arm-io/spi3/ipd"};
            static const char *const keys[] = {"ranges", "#address-cells", "#size-cells",
                "compatible", "reg", "function-spi_cs0", "function-enable_cs"};
            for (j = 0; j < sizeof(paths) / sizeof(paths[0]); ++j) {
                if (radt_lookup(b, used, &w, paths[j], keys[j], &v) != RADT_OK)
                    assert(!v.data && !v.size);
                else assert(v.data >= b && v.data <= b + used && v.size <= (size_t)(b + used - v.data));
            }
        }
        (void)radt_validate(b, used, &w);
        if (radt_lookup(b, used, &w, "/arm-io/spi3", "reg", &v) != RADT_OK)
            assert(!v.data && !v.size);
        else assert(v.data >= b && v.data <= b + used && v.size <= (size_t)(b + used - v.data));
    }
    puts("PASS: valid/prefix/unaligned/depth/count/flags/duplicate/malformed/privacy + 30000 fuzz cases");
    return 0;
}
