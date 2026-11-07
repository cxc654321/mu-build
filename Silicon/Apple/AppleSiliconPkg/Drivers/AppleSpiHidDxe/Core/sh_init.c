/* SPDX-License-Identifier: GPL-2.0-only
 * Protocol adaptation: Copyright (C) The Asahi Linux Contributors.
 * Based upstream on applespi.c, Copyright (c) 2015-2018 Federico Lorenzi
 * and Copyright (c) 2017-2018 Ronald Tschalär. See INIT.md.
 */
#include "sh_init.h"
#include "J313CoreCompat.h"
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static int stop(struct sh_init *s, struct sh_init_action *a, int error)
{
    s->phase = a->phase = SH_INIT_FAILED;
    memset(s->descriptor, 0, sizeof(s->descriptor));
    memset(s->descriptor_length, 0, sizeof(s->descriptor_length));
    return error;
}
void sh_init_reset(struct sh_init *s)
{ if (s) memset(s, 0, sizeof(*s)); }
static int begin(struct sh_init *s, uint64_t now, struct sh_init_action *a)
{
    if (a) memset(a, 0, sizeof(*a));
    if (!s || !a) return SH_INIT_ARGUMENT;
    a->phase = s->phase;
    if (s->phase == SH_INIT_IDLE || s->phase == SH_INIT_FAILED)
        return SH_INIT_STATE;
    if (now < s->last_ms) return stop(s, a, SH_INIT_CLOCK);
    s->last_ms = now;
    if (s->phase == SH_INIT_DONE) return SH_INIT_COMPLETE;
    /* Subtraction, not deadline addition: safe near UINT64_MAX. */
    if (now - s->started_ms >= SH_INIT_TIMEOUT_MS)
        return stop(s, a, SH_INIT_TIMEOUT);
    return SH_INIT_WAIT;
}
int sh_init_restart(struct sh_init *s, uint64_t now, struct sh_init_action *a)
{
    unsigned next;
    if (a) memset(a, 0, sizeof(*a));
    if (!s || !a) return SH_INIT_ARGUMENT;
    if (s->phase != SH_INIT_IDLE && now < s->last_ms)
        return stop(s, a, SH_INIT_CLOCK);
    next = s->next_id;
    sh_init_reset(s);
    s->next_id = next;
    s->last_ms = s->started_ms = now;
    if (next > 251u) return stop(s, a, SH_INIT_LIMIT);
    s->phase = a->phase = SH_INIT_BOOT;
    return SH_INIT_WAIT;
}
int sh_init_poll(struct sh_init *s, uint64_t now, struct sh_init_action *a)
{ return begin(s, now, a); }
static int event(struct sh_init *s, struct sh_init_action *a)
{
    if (s->events >= SH_INIT_EVENT_MAX) return stop(s, a, SH_INIT_LIMIT);
    ++s->events;
    return SH_INIT_WAIT;
}
static int reject(struct sh_init *s)
{ ++s->rejected; return SH_INIT_REJECT; }
static int request(struct sh_init *s, uint64_t now, struct sh_init_action *a)
{
    uint8_t type, iface;
    uint16_t response = SH_INIT_DESC_MAX;
    switch (s->phase) {
    case SH_INIT_DEVICE: type = 1; iface = 0xd0; response = 0; break;
    case SH_INIT_IFACE1: type = 2; iface = 1; break;
    case SH_INIT_IFACE2: type = 2; iface = 2; break;
    case SH_INIT_DESC1: type = 0x10; iface = 1; break;
    case SH_INIT_DESC2: type = 0x10; iface = 2; break;
    default: return stop(s, a, SH_INIT_STATE);
    }
    if (s->next_id > 255u || s->requests >= 5u)
        return stop(s, a, SH_INIT_LIMIT);
    s->pending_id = (uint8_t)s->next_id++;
    s->pending_type = type; s->pending_iface = iface;
    s->pending_response = response;
    if (sh_request(a->packet, sizeof(a->packet), 0xd0, 0x20, type,
                   iface, s->pending_id, response, NULL, 0) != SH_MESSAGE)
        return stop(s, a, SH_INIT_STATE);
    ++s->requests;
    s->started_ms = now;
    a->phase = s->phase; a->packet_length = SH_PACKET_SIZE;
    return SH_INIT_REQUEST;
}
int sh_init_boot_packet(struct sh_init *s, uint64_t now,
                        const uint8_t *p, size_t n, struct sh_init_action *a)
{
    static const uint8_t boot[4] = {0xa0, 0x80, 0, 0};
    int r = begin(s, now, a);
    if (r != SH_INIT_WAIT) return r;
    if ((r = event(s, a)) != SH_INIT_WAIT) return r;
    if (s->phase != SH_INIT_BOOT || !p || n != SH_PACKET_SIZE)
        return reject(s);
    /* Upstream does not constrain boot flags/device; do not invent values.
     * Reject fragments conservatively; short notification has no inner CRC. */
    if (sh_crc16(p, 254) != le16(p + 254) || le16(p + 2) != 0 ||
        le16(p + 4) != 0 || le16(p + 6) != 4 || memcmp(p + 8, boot, 4))
        return reject(s);
    ++s->accepted; s->phase = SH_INIT_DEVICE;
    return request(s, now, a);
}
static int span(const uint8_t *p, size_t n, size_t at)
{
    size_t off = le16(p + at), len = le16(p + at + 2);
    return off <= n && len <= n - off;
}
int sh_init_message(struct sh_init *s, uint64_t now,
                    const struct sh_message *m, struct sh_init_action *a)
{
    const uint8_t *p;
    int r = begin(s, now, a);
    if (r != SH_INIT_WAIT) return r;
    if ((r = event(s, a)) != SH_INIT_WAIT) return r;
    if (s->phase == SH_INIT_BOOT || !m || !m->payload || !m->length ||
        m->length > SH_INIT_DESC_MAX || m->flags != 0x40 ||
        m->device != 0xd0 || m->unknown0 != 0x20 ||
        m->unknown1 != s->pending_type || m->unknown2 != s->pending_iface ||
        m->id != s->pending_id || m->response_length != s->pending_response)
        return reject(s);
    p = m->payload;
    switch (s->phase) {
    case SH_INIT_DEVICE:
        if (m->length < 24 || le16(p + 4) < 3 || !le16(p + 6) ||
            !span(p, m->length, 12) || !span(p, m->length, 16) ||
            !span(p, m->length, 20)) return reject(s);
        s->phase = SH_INIT_IFACE1; break;
    case SH_INIT_IFACE1: case SH_INIT_IFACE2:
        if (m->length < 16 || p[1] != s->pending_iface ||
            !le16(p + 6) || !span(p, m->length, 12)) return reject(s);
        s->phase = s->phase == SH_INIT_IFACE1 ? SH_INIT_IFACE2 : SH_INIT_DESC1;
        break;
    case SH_INIT_DESC1: case SH_INIT_DESC2: {
        unsigned i = s->pending_iface - 1u;
        memcpy(s->descriptor[i], p, m->length);
        s->descriptor_length[i] = m->length;
        s->phase = s->phase == SH_INIT_DESC1 ? SH_INIT_DESC2 : SH_INIT_DONE;
        break;
    }
    default: return stop(s, a, SH_INIT_STATE);
    }
    ++s->accepted;
    if (s->phase == SH_INIT_DONE) {
        a->phase = SH_INIT_DONE;
        return SH_INIT_COMPLETE;
    }
    return request(s, now, a);
}
const uint8_t *sh_init_descriptor(const struct sh_init *s, unsigned iface,
                                  size_t *length)
{
    if (length) *length = 0;
    if (!s || !length || s->phase != SH_INIT_DONE || iface < 1 || iface > 2)
        return NULL;
    *length = s->descriptor_length[iface - 1];
    return s->descriptor[iface - 1];
}
