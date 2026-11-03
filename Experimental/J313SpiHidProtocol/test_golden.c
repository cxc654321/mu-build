/* SPDX-License-Identifier: GPL-2.0-only
 * Offline synthetic tests based on the pinned Asahi layout; see GOLDEN.md.
 * No expected bytes or CRCs are generated through the implementation under test.
 */
#include "spihid_protocol.h"
#include "golden_vectors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) {     fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

/* Independent MSB-first polynomial division; not the DUT's reflected loop.
 * Used ONLY to audit frozen fixture integrity, never to construct expectations.
 */
static unsigned reverse(unsigned value, unsigned bits)
{
    unsigned result = 0;
    while (bits--) { result = (result << 1) | (value & 1u); value >>= 1; }
    return result;
}
static unsigned reference_crc(const uint8_t *p, size_t n)
{
    unsigned r = 0, bit;
    size_t i;
    for (i = 0; i < n; ++i) {
        r ^= reverse(p[i], 8) << 8;
        for (bit = 0; bit < 8; ++bit)
            r = ((r << 1) ^ ((r & 0x8000u) ? 0x8005u : 0)) & 0xffffu;
    }
    return reverse(r, 16);
}
static unsigned le16(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void audit(void)
{
    const uint8_t *packets[] = {
        golden256, golden_descriptor256, golden_part0, golden_part1, golden_part2,
        golden_bad_message_crc, golden_bad_message_length, golden_bad_offset,
        golden_bad_bounds
    };
    size_t i;
    CHECK(reference_crc((const uint8_t *)"123456789", 9) == 0xbb3d);
    for (i = 0; i < sizeof(packets) / sizeof(packets[0]); ++i) {
        CHECK(reference_crc(packets[i], 254) == le16(packets[i] + 254));
        CHECK(reference_crc(packets[i], 256) == 0);
    }
    CHECK(reference_crc(golden256 + 8, 15) == le16(golden256 + 23));
    CHECK(reference_crc(golden_descriptor256 + 8, 8) == le16(golden_descriptor256 + 16));
    CHECK(reference_crc(golden_message, 495) == 0xb590);
    CHECK(reference_crc(golden_message, 497) == 0);
    CHECK(memcmp(golden_message, golden_part0 + 8, 246) == 0);
    CHECK(memcmp(golden_message + 246, golden_part1 + 8, 246) == 0);
    CHECK(memcmp(golden_message + 492, golden_part2 + 8, 5) == 0);
    puts("fixture audit: independent CRC and frozen fragmentation PASS");
}
static void request(void)
{
    uint8_t actual[256];
    size_t i;
    memset(actual, 0xcc, sizeof(actual));
    CHECK(sh_request(actual, sizeof(actual), 1, 0x51, 1, 9, 0xa7, 0x1234,
                     golden_request_payload, sizeof(golden_request_payload)) == SH_MESSAGE);
    for (i = 0; i < 256; ++i) CHECK(actual[i] == golden256[i]);
    memset(actual, 0xcc, sizeof(actual));
    CHECK(sh_request(actual, sizeof(actual), 0xd0, 0x20, 0x10, 2, 0x5c, 512,
                     NULL, 0) == SH_MESSAGE);
    for (i = 0; i < 256; ++i) CHECK(actual[i] == golden_descriptor256[i]);
    puts("sh_request: two complete frozen 256-byte comparisons PASS");
}
static void empty(const struct sh_message *m)
{
    CHECK(m->payload == NULL && m->length == 0 && m->response_length == 0);
    CHECK(m->device == 0 && m->flags == 0 && m->unknown0 == 0);
    CHECK(m->unknown1 == 0 && m->unknown2 == 0 && m->id == 0);
}
static void reset_state(const struct sh_rx *rx)
{
    size_t i;
    CHECK(rx->used == 0 && rx->total == 0 && rx->device == 0 && rx->flags == 0);
    for (i = 0; i < sizeof(rx->bytes); ++i) CHECK(rx->bytes[i] == 0);
}
static void sequence(void)
{
    struct sh_rx rx;
    struct sh_message m;
    uint8_t unaligned[257];
    sh_reset(&rx);
    CHECK(sh_feed(&rx, golden_part0, 256, &m) == SH_MORE); empty(&m);
    CHECK(rx.used == 246 && rx.total == 497);
    CHECK(sh_feed(&rx, golden_part1, 256, &m) == SH_MORE); empty(&m);
    CHECK(rx.used == 492 && rx.total == 497);
    memcpy(unaligned + 1, golden_part2, 256);
    CHECK(sh_feed(&rx, unaligned + 1, 256, &m) == SH_MESSAGE);
    CHECK(m.flags == 0x20 && m.device == 2);
    CHECK(m.unknown0 == 0x10 && m.unknown1 == 2 && m.unknown2 == 0x7b);
    CHECK(m.id == 0xe3 && m.response_length == 0x1234 && m.length == 487);
    CHECK(memcmp(m.payload, golden_payload, sizeof(golden_payload)) == 0);
    CHECK(rx.used == 0 && rx.total == 0);
    CHECK(sh_feed(&rx, golden256, 256, &m) == SH_MESSAGE);
    CHECK(m.flags == 0x40 && m.device == 1 && m.unknown0 == 0x51);
    CHECK(m.unknown1 == 1 && m.unknown2 == 9 && m.id == 0xa7);
    CHECK(m.response_length == 0x1234 && m.length == 7);
    CHECK(memcmp(m.payload, golden_request_payload, 7) == 0);
    CHECK(sh_feed(&rx, golden_descriptor256, 256, &m) == SH_MESSAGE);
    CHECK(m.flags == 0x40 && m.device == 0xd0 && m.unknown0 == 0x20);
    CHECK(m.unknown1 == 0x10 && m.unknown2 == 2 && m.id == 0x5c);
    CHECK(m.response_length == 512 && m.length == 0);
    puts("sh_feed: three fixed fragments, 5-byte tail, fields/payload/length PASS");
}
static void negatives(void)
{
    struct sh_rx rx;
    struct sh_message m;
    const uint8_t *parts[] = {golden_part0, golden_part1, golden_part2};
    uint8_t changed[256];
    size_t i, j, k;
    unsigned bit;
    /* Every bit, including both CRC bytes and padding, of every fragment. */
    for (i = 0; i < 3; ++i) for (j = 0; j < 256; ++j) for (bit = 0; bit < 8; ++bit) {
        sh_reset(&rx);
        for (k = 0; k < i; ++k) CHECK(sh_feed(&rx, parts[k], 256, &m) == SH_MORE);
        memcpy(changed, parts[i], 256); changed[j] ^= (uint8_t)(1u << bit);
        CHECK(sh_feed(&rx, changed, 256, &m) == SH_PACKET_CRC);
        empty(&m); reset_state(&rx);
    }
    /* These mutations have PRECOMPUTED valid packet CRCs, so they exercise
     * inner CRC, inner length, sequence and bounds rather than outer CRC. */
    sh_reset(&rx);
    CHECK(sh_feed(&rx, golden_part0, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_part1, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_bad_message_crc, 256, &m) == SH_MESSAGE_CRC);
    empty(&m); reset_state(&rx);
    CHECK(sh_feed(&rx, golden_bad_message_length, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_part1, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_part2, 256, &m) == SH_MESSAGE_LENGTH);
    empty(&m); reset_state(&rx);
    CHECK(sh_feed(&rx, golden_part0, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_bad_offset, 256, &m) == SH_SEQUENCE);
    empty(&m); reset_state(&rx);
    CHECK(sh_feed(&rx, golden_bad_bounds, 256, &m) == SH_BOUNDS);
    empty(&m); reset_state(&rx);
    CHECK(sh_feed(&rx, golden_part1, 256, &m) == SH_SEQUENCE);
    empty(&m); reset_state(&rx);
    for (i = 0; i < 256; ++i) {
        CHECK(sh_feed(&rx, golden_part0, i, &m) == SH_PACKET_LENGTH);
        empty(&m); reset_state(&rx);
    }
    /* Error recovery must accept a new intact sequence. */
    CHECK(sh_feed(&rx, golden_part0, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_part1, 256, &m) == SH_MORE);
    CHECK(sh_feed(&rx, golden_part2, 256, &m) == SH_MESSAGE);
    CHECK(m.length == 487 && memcmp(m.payload, golden_payload, 487) == 0);
    puts("mutation negatives: 6144 bit flips, repaired-CRC inner/length/offset/bounds, truncation/recovery PASS");
}
int main(void)
{
    audit(); request(); sequence(); negatives();
    puts("ALL GOLDEN TESTS PASSED");
    return 0;
}
