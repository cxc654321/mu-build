/* SPDX-License-Identifier: GPL-2.0-only */
#include "sh_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put16(uint8_t *p, unsigned v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void crc(uint8_t *p) { put16(p + 254, sh_crc16(p, 254)); }
static void boot(uint8_t *p)
{
    memset(p, 0, 256); p[6] = 4; p[8] = 0xa0; p[9] = 0x80; crc(p);
}
/* Generate CRC-correct complete messages through the unchanged parser,
 * including multi-packet replies. No sh_request payload size workaround. */
static void reply(struct sh_rx *rx, struct sh_message *m,
                  const struct sh_init *s, const uint8_t *data, size_t n)
{
    uint8_t wire[SH_MESSAGE_MAX], packet[256];
    size_t pos = 0, total = n + 10;
    assert(total <= sizeof(wire));
    memset(wire, 0, sizeof(wire));
    wire[0] = 0x20; wire[1] = s->pending_type;
    wire[2] = s->pending_iface; wire[3] = s->pending_id;
    put16(wire + 4, s->pending_response); put16(wire + 6, (unsigned)n);
    memcpy(wire + 8, data, n); put16(wire + 8 + n, sh_crc16(wire, n + 8));
    sh_reset(rx);
    while (pos < total) {
        size_t chunk = total - pos;
        if (chunk > SH_PACKET_DATA) chunk = SH_PACKET_DATA;
        memset(packet, 0, sizeof(packet)); packet[0] = 0x40; packet[1] = 0xd0;
        put16(packet + 2, (unsigned)pos);
        put16(packet + 4, (unsigned)(total - pos - chunk));
        put16(packet + 6, (unsigned)chunk);
        memcpy(packet + 8, wire + pos, chunk); crc(packet);
        assert(sh_feed(rx, packet, sizeof(packet), m) ==
               (pos + chunk == total ? SH_MESSAGE : SH_MORE));
        pos += chunk;
    }
}
static void start(struct sh_init *s, struct sh_init_action *a, uint64_t now)
{
    uint8_t p[256]; boot(p); sh_init_reset(s);
    assert(sh_init_restart(s, now, a) == SH_INIT_WAIT);
    assert(sh_init_boot_packet(s, now, p, sizeof(p), a) == SH_INIT_REQUEST);
    assert(s->phase == SH_INIT_DEVICE && a->packet_length == 256);
}
static void payload(const struct sh_init *s, uint8_t *p, size_t *n)
{
    memset(p, 0, 513);
    switch (s->phase) {
    case SH_INIT_DEVICE: *n = 24; p[4] = 3; put16(p + 6, 0x05ac); break;
    case SH_INIT_IFACE1: case SH_INIT_IFACE2:
        *n = 16; p[1] = s->pending_iface; put16(p + 6, 2048); break;
    default: *n = 512; memset(p, s->pending_iface, *n); break;
    }
}
static void normal_and_errors(void)
{
    struct sh_init s; struct sh_init_action a;
    struct sh_rx rx; struct sh_message m, bad;
    uint8_t p[513]; size_t n, got; unsigned i;
    start(&s, &a, 0);
    for (i = 0; i < 5; ++i) {
        struct sh_rx reqrx; struct sh_message req;
        enum sh_init_phase phase = s.phase;
        sh_reset(&reqrx);
        assert(sh_feed(&reqrx, a.packet, a.packet_length, &req) == SH_MESSAGE);
        assert(req.id == i && req.device == 0xd0 && req.unknown0 == 0x20);
        assert(req.unknown1 == s.pending_type && req.unknown2 == s.pending_iface);
        assert(req.response_length == s.pending_response && req.length == 0);
        payload(&s, p, &n); reply(&rx, &m, &s, p, n);
        bad = m; bad.id = (uint8_t)(m.id - 1);
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        assert(s.phase == phase && !a.packet_length);
        bad = m; bad.unknown2 ^= 3;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.unknown1 ^= 1;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.flags = 0x20;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.device = 1;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.response_length ^= 1;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.unknown0 = 0xe0; /* unknown/error type cannot advance */
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.length = SIZE_MAX;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        bad = m; bad.length = 0;
        assert(sh_init_message(&s, i, &bad, &a) == SH_INIT_REJECT);
        assert(sh_init_descriptor(&s, 1, &got) == NULL && got == 0);
        assert(sh_init_message(&s, i, &m, &a) ==
               (i == 4 ? SH_INIT_COMPLETE : SH_INIT_REQUEST));
    }
    assert(s.requests == 5 && s.accepted == 6 && s.rejected == 45);
    assert(sh_init_descriptor(&s, 1, &got) && got == 512);
    assert(sh_init_descriptor(&s, 2, &got)[511] == 2 && got == 512);
    assert(!sh_init_descriptor(&s, 3, &got) && !got);
    assert(sh_init_poll(&s, 99999, &a) == SH_INIT_COMPLETE);
    assert(!a.packet_length);
    sh_init_reset(&s);
    assert(!sh_init_descriptor(&s, 1, &got) && !got);
}
static void boot_errors(void)
{
    struct sh_init s; struct sh_init_action a; uint8_t p[256];
    sh_init_reset(&s); boot(p);
    assert(sh_init_restart(&s, 0, &a) == SH_INIT_WAIT);
    p[254] ^= 1;
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    boot(p); p[2] = 1; crc(p);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    boot(p); p[4] = 1; crc(p);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    boot(p); p[6] = 5; crc(p);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    boot(p); p[8] ^= 1; crc(p);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    assert(sh_init_boot_packet(&s, 0, p, 255, &a) == SH_INIT_REJECT);
    assert(s.phase == SH_INIT_BOOT && !a.packet_length);
    boot(p);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REQUEST);
    assert(sh_init_boot_packet(&s, 0, p, 256, &a) == SH_INIT_REJECT);
    assert(s.phase == SH_INIT_DEVICE && s.requests == 1);
}
static void bounds_and_deadlines(void)
{
    struct sh_init s; struct sh_init_action a;
    struct sh_rx rx; struct sh_message m;
    uint8_t p[513]; size_t n; unsigned i;
    start(&s, &a, 0); payload(&s, p, &n);
    reply(&rx, &m, &s, p, 23);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REJECT);
    put16(p + 12, 24); put16(p + 14, 1); reply(&rx, &m, &s, p, 24);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REJECT);
    payload(&s, p, &n); reply(&rx, &m, &s, p, n);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REQUEST);
    payload(&s, p, &n); p[1] = 2; reply(&rx, &m, &s, p, n);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REJECT);
    for (i = 0; i < 2; ++i) {
        payload(&s, p, &n); reply(&rx, &m, &s, p, n);
        assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REQUEST);
    }
    assert(s.phase == SH_INIT_DESC1);
    memset(p, 1, sizeof(p)); reply(&rx, &m, &s, p, 513);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REJECT);
    assert(!s.descriptor_length[0]);
    reply(&rx, &m, &s, p, 512);
    assert(sh_init_message(&s, 0, &m, &a) == SH_INIT_REQUEST);
    assert(sh_init_poll(&s, 999, &a) == SH_INIT_WAIT);
    assert(sh_init_poll(&s, 1000, &a) == SH_INIT_TIMEOUT);
    assert(s.phase == SH_INIT_FAILED && !s.descriptor_length[0]);
    assert(sh_init_message(&s, 1000, &m, &a) == SH_INIT_STATE);
    start(&s, &a, 100);
    assert(sh_init_poll(&s, 99, &a) == SH_INIT_CLOCK);
    assert(sh_init_restart(&s, 99, &a) == SH_INIT_CLOCK);
    assert(sh_init_restart(&s, 100, &a) == SH_INIT_WAIT);
    assert(sh_init_poll(&s, 1100, &a) == SH_INIT_TIMEOUT); /* boot deadline */
    start(&s, &a, UINT64_MAX - 500);
    assert(sh_init_poll(&s, UINT64_MAX, &a) == SH_INIT_WAIT);
    assert(sh_init_poll(&s, 0, &a) == SH_INIT_CLOCK);
    start(&s, &a, 0); payload(&s, p, &n); reply(&rx, &m, &s, p, n);
    assert(sh_init_message(&s, 1000, &m, &a) == SH_INIT_TIMEOUT);
}
static void restart_and_limits(void)
{
    struct sh_init s; struct sh_init_action a;
    struct sh_rx rx; struct sh_message old;
    uint8_t p[513], b[256]; size_t n; unsigned i;
    start(&s, &a, 0); payload(&s, p, &n); reply(&rx, &old, &s, p, n);
    assert(sh_init_restart(&s, 1, &a) == SH_INIT_WAIT); boot(b);
    assert(sh_init_boot_packet(&s, 1, b, 256, &a) == SH_INIT_REQUEST);
    assert(s.pending_id == 1);
    assert(sh_init_message(&s, 1, &old, &a) == SH_INIT_REJECT);
    assert(s.phase == SH_INIT_DEVICE);
    start(&s, &a, 0);
    for (i = 1; i < SH_INIT_EVENT_MAX; ++i)
        assert(sh_init_message(&s, 0, NULL, &a) == SH_INIT_REJECT);
    assert(sh_init_message(&s, 0, NULL, &a) == SH_INIT_LIMIT);
    assert(!a.packet_length && s.phase == SH_INIT_FAILED);
    sh_init_reset(&s);
    for (i = 0; i < 252; ++i) {
        assert(sh_init_restart(&s, i, &a) == SH_INIT_WAIT);
        assert(sh_init_boot_packet(&s, i, b, 256, &a) == SH_INIT_REQUEST);
        assert(s.pending_id == i);
    }
    assert(sh_init_restart(&s, 252, &a) == SH_INIT_LIMIT);
    assert(!a.packet_length);
    sh_init_reset(&s);
    assert(sh_init_poll(&s, 0, &a) == SH_INIT_STATE);
    assert(sh_init_poll(NULL, 0, &a) == SH_INIT_ARGUMENT);
    assert(sh_init_restart(&s, 0, NULL) == SH_INIT_ARGUMENT);
}
int main(void)
{
    normal_and_errors(); boot_errors(); bounds_and_deadlines(); restart_and_limits();
    puts("sh_init: all host tests passed");
    return 0;
}
