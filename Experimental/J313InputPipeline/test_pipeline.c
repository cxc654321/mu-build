/* SPDX-License-Identifier: GPL-2.0-only
 * Host integration test; all SPI packets and HID input reports are SYNTHETIC.
 * Routing informed by Asahi Linux Contributors' SPI HID core; see README.md.
 * Linked MIT keyboard library remains Copyright (c) 2026
 * J313HidKeyboard contributors, with its original license unchanged.
 */
#include "spihid_protocol.h"
#include "hid_keyboard.h"
#include "observed_descriptor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Tests require assertions"
#endif

struct pipeline {
    struct sh_rx rx;
    hk_descriptor descriptor;
    hk_state state;
    unsigned decode_calls;
};
struct outcome {
    int transport;
    int decoded;
    hk_status hid;
    hk_result keys;
};
static struct pipeline pipeline; /* Descriptor is too large for firmware stacks. */

/* Test-local adapter, not a firmware/public API. Buffers do not overlap.
 * Consume borrowed message payload synchronously, before another feed/reset.
 * This upper-layer selector is an explicit Asahi-derived policy assumption,
 * NOT a claim that unknown0/1/2 have fully known protocol semantics.
 */
static struct outcome feed(const uint8_t *packet, size_t size)
{
    struct sh_message message;
    struct outcome out = {0};
    out.transport = sh_feed(&pipeline.rx, packet, size, &message);
    if (out.transport != SH_MESSAGE || message.flags != 0x20 ||
        message.device != 1 || message.unknown0 != 0x10 || !message.length)
        return out;
    out.decoded = 1;
    ++pipeline.decode_calls;
    out.hid = hk_decode(&pipeline.descriptor, &pipeline.state,
                       message.payload, message.length, &out.keys);
    return out;
}
static void le16(uint8_t *p, size_t n)
{
    assert(n <= 65535);
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void seal_packet(uint8_t *p)
{
    le16(p + 254, sh_crc16(p, 254));
}
/* Independent framing, using the existing CRC primitive, not sh_request.
 * Arbitrary unknown1/2/id/rsplen test that they are NOT guessed selectors.
 */
static size_t message(uint8_t *m, const uint8_t *report, size_t n)
{
    assert(n <= 65);
    memset(m, 0, 75);
    m[0] = 0x10; m[1] = 0xa7; m[2] = 0x6d; m[3] = 0xe3;
    le16(m + 4, 0x1234);
    le16(m + 6, n);
    memcpy(m + 8, report, n);
    le16(m + 8 + n, sh_crc16(m, 8 + n));
    return n + 10;
}
static void packet(uint8_t *p, const uint8_t *m, size_t total,
                   size_t offset, size_t length, uint8_t device, uint8_t flags)
{
    assert(offset <= total && length <= total - offset);
    assert(length && length <= SH_PACKET_DATA);
    memset(p, 0, SH_PACKET_SIZE);
    p[0] = flags; p[1] = device;
    le16(p + 2, offset); le16(p + 4, total - offset - length);
    le16(p + 6, length);
    memcpy(p + 8, m + offset, length);
    seal_packet(p);
}
static struct outcome report(const uint8_t *r, size_t n)
{
    uint8_t m[75], p[SH_PACKET_SIZE];
    size_t total = message(m, r, n);
    packet(p, m, total, 0, total, 1, 0x20);
    return feed(p, sizeof(p));
}
static void quiet(struct outcome out, const hk_state *old,
                  int transport, int decoded, hk_status hid)
{
    assert(out.transport == transport && out.decoded == decoded);
    if (decoded) assert(out.hid == hid);
    assert(out.keys.count == 0);
    assert(memcmp(old, &pipeline.state, sizeof(*old)) == 0);
}
static void event(struct outcome out, uint8_t usage, uint8_t pressed)
{
    assert(out.transport == SH_MESSAGE && out.decoded && out.hid == HK_OK);
    assert(out.keys.count == 1);
    assert(out.keys.events[0].usage == usage);
    assert(out.keys.events[0].pressed == pressed);
}
static void test_filters_and_errors(void)
{
    const uint8_t held[10] = {1, 2, 0, 4, 0, 0, 0, 0, 0, 0};
    const uint8_t release[10] = {1};
    uint8_t m[75], p[SH_PACKET_SIZE], r[65] = {0};
    struct outcome out = report(held, sizeof(held));
    hk_state old = pipeline.state;
    size_t total, n;
    unsigned before;
    assert(out.keys.count == 2 && out.keys.modifiers == 2);
    assert(out.keys.events[0].usage == 4 && out.keys.events[0].pressed == 1);
    assert(out.keys.events[1].usage == 0xe1 && out.keys.events[1].pressed == 1);
    quiet(report(held, sizeof(held)), &old, SH_MESSAGE, 1, HK_OK);
    total = message(m, release, sizeof(release));
    packet(p, m, total, 0, total, 1, 0x20);
    p[16] ^= 1;
    before = pipeline.decode_calls;
    quiet(feed(p, sizeof(p)), &old, SH_PACKET_CRC, 0, HK_OK);
    seal_packet(p); /* valid outer CRC, invalid inner CRC */
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE_CRC, 0, HK_OK);
    packet(p, m, total, 0, total, 2, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE, 0, HK_OK);
    packet(p, m, total, 0, total, 1, 0x40);
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE, 0, HK_OK);
    m[0] = 0x20;
    le16(m + total - 2, sh_crc16(m, total - 2));
    packet(p, m, total, 0, total, 1, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE, 0, HK_OK);
    total = message(m, release, 0);
    packet(p, m, total, 0, total, 1, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE, 0, HK_OK);
    assert(pipeline.decode_calls == before);
    total = message(m, release, sizeof(release));
    m[6] = 9; /* declared message length disagrees even with a valid CRC */
    le16(m + total - 2, sh_crc16(m, total - 2));
    packet(p, m, total, 0, total, 1, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_MESSAGE_LENGTH, 0, HK_OK);
    quiet(feed(p, sizeof(p) - 1), &old, SH_PACKET_LENGTH, 0, HK_OK);

    /* All HID errors must preserve held A AND Shift; no synthesized release. */
    for (n = 1; n <= 11; ++n) {
        memset(r, 0, sizeof(r)); r[0] = 1;
        if (n == 10) continue;
        quiet(report(r, n), &old, SH_MESSAGE, 1,
              n < 10 ? HK_TRUNCATED : HK_INVALID);
    }
    for (n = 1; n <= 3; ++n) {
        memcpy(r, release, sizeof(release)); r[8] = (uint8_t)n;
        quiet(report(r, 10), &old, SH_MESSAGE, 1, HK_ROLLOVER);
    }
    r[0] = 0xfe;
    quiet(report(r, 10), &old, SH_MESSAGE, 1, HK_UNKNOWN_REPORT);
    r[0] = 9;
    quiet(report(r, 4), &old, SH_MESSAGE, 1, HK_UNKNOWN_REPORT);
    r[0] = 0x52; r[1] = 0x1f;
    out = report(r, 2);
    quiet(out, &old, SH_MESSAGE, 1, HK_OK);
    assert(out.keys.modifiers == 2);
    memset(r, 0xff, sizeof(r)); r[0] = 0x3f;
    quiet(report(r, 65), &old, SH_MESSAGE, 1, HK_OK);
    /* Full keyboard report with changed ignored consumer/vendor trailing bits. */
    memcpy(r, held, sizeof(held)); r[9] = 0xff;
    quiet(report(r, 10), &old, SH_MESSAGE, 1, HK_OK);
    out = report(release, sizeof(release));
    assert(out.hid == HK_OK && out.keys.count == 2 && !out.keys.modifiers);
    assert(out.keys.events[0].usage == 4 && !out.keys.events[0].pressed);
    assert(out.keys.events[1].usage == 0xe1 && !out.keys.events[1].pressed);
    puts("PASS filters, CRC/length/HID errors, consumer/vendor isolation, release");
}
static void test_fragmentation(void)
{
    const uint8_t down[10] = {1, 0, 0, 4};
    const uint8_t up[10] = {1};
    uint8_t m[75], p[SH_PACKET_SIZE];
    size_t total = message(m, down, sizeof(down)), split, offset;
    hk_state old;
    for (split = 1; split < total; ++split) {
        old = pipeline.state;
        packet(p, m, total, 0, split, 1, 0x20);
        quiet(feed(p, sizeof(p)), &old, SH_MORE, 0, HK_OK);
        packet(p, m, total, split, total - split, 1, 0x20);
        event(feed(p, sizeof(p)), 4, 1);
        event(report(up, sizeof(up)), 4, 0);
    }
    old = pipeline.state;
    for (offset = 0; offset < total; ++offset) {
        packet(p, m, total, offset, 1, 1, 0x20);
        if (offset + 1 < total)
            quiet(feed(p, sizeof(p)), &old, SH_MORE, 0, HK_OK);
        else event(feed(p, sizeof(p)), 4, 1);
    }
    old = pipeline.state;
    packet(p, m, total, 0, 12, 1, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_MORE, 0, HK_OK);
    p[254] ^= 1;
    quiet(feed(p, sizeof(p)), &old, SH_PACKET_CRC, 0, HK_OK);
    packet(p, m, total, 12, total - 12, 1, 0x20);
    quiet(feed(p, sizeof(p)), &old, SH_SEQUENCE, 0, HK_OK);
    /* After rejection, a fresh valid release recovers the stream. */
    event(report(up, sizeof(up)), 4, 0);
    puts("PASS every two-fragment split, bytewise reassembly, corruption recovery");
}
int main(void)
{
    assert(sizeof(observed_descriptor) == 182);
    assert(hk_parse(observed_descriptor, sizeof(observed_descriptor),
                    &pipeline.descriptor) == HK_OK);
    hk_state_init(&pipeline.state);
    sh_reset(&pipeline.rx);
    test_filters_and_errors();
    test_fragmentation();
    printf("ALL PIPELINE TESTS PASSED (%u synchronous HID decodes)\n",
           pipeline.decode_calls);
    return 0;
}
