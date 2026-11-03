/* SPDX-License-Identifier: GPL-2.0-only
 * Adapted with reference to Asahi Linux SPI HID core:
 * Copyright (C) The Asahi Linux Contributors
 * Based upstream on applespi.c:
 * Copyright (c) 2015-2018 Federico Lorenzi
 * Copyright (c) 2017-2018 Ronald Tschalaer
 * Full provenance and original name spelling: README.md.
 */
#include "spihid_protocol.h"
#include <string.h>

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
uint16_t sh_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    size_t i;
    unsigned bit;
    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0u));
    }
    return crc;
}
void sh_reset(struct sh_rx *rx)
{
    if (rx != NULL)
        memset(rx, 0, sizeof(*rx));
}
static int fail(struct sh_rx *rx, int error)
{
    sh_reset(rx);
    return error;
}
int sh_feed(struct sh_rx *rx, const uint8_t *p, size_t size,
            struct sh_message *out)
{
    size_t off, len, remain, total;
    if (out != NULL)
        memset(out, 0, sizeof(*out));
    if (rx == NULL || p == NULL || out == NULL)
        return fail(rx, SH_ARGUMENT);
    if (size != SH_PACKET_SIZE)
        return fail(rx, SH_PACKET_LENGTH);
    if (sh_crc16(p, 254) != get16(p + 254))
        return fail(rx, SH_PACKET_CRC);
    if (p[0] != 0x20 && p[0] != 0x40)
        return fail(rx, SH_FLAGS);
    off = get16(p + 2);
    remain = get16(p + 4);
    len = get16(p + 6);
    /* Subtract only after bounding operands: no narrow addition overflow. */
    if (len == 0 || len > SH_PACKET_DATA || off > SH_MESSAGE_MAX ||
        len > SH_MESSAGE_MAX - off || remain > SH_MESSAGE_MAX - off - len)
        return fail(rx, SH_BOUNDS);
    total = off + len + remain;
    if (total < 10)
        return fail(rx, SH_MESSAGE_LENGTH);
    if (off != rx->used)
        return fail(rx, SH_SEQUENCE);
    if (rx->used != 0) {
        if (rx->total != total || rx->device != p[1] || rx->flags != p[0])
            return fail(rx, SH_SEQUENCE);
    } else {
        rx->total = total;
        rx->device = p[1];
        rx->flags = p[0];
    }
    memcpy(rx->bytes + off, p + 8, len);
    rx->used += len;
    if (remain != 0)
        return SH_MORE;
    if ((size_t)get16(rx->bytes + 6) != total - 10)
        return fail(rx, SH_MESSAGE_LENGTH);
    if (sh_crc16(rx->bytes, total - 2) != get16(rx->bytes + total - 2))
        return fail(rx, SH_MESSAGE_CRC);
    out->device = rx->device;
    out->flags = rx->flags;
    out->unknown0 = rx->bytes[0];
    out->unknown1 = rx->bytes[1];
    out->unknown2 = rx->bytes[2];
    out->id = rx->bytes[3];
    out->response_length = get16(rx->bytes + 4);
    out->length = total - 10;
    out->payload = rx->bytes + 8;
    rx->used = rx->total = 0;
    rx->device = rx->flags = 0;
    return SH_MESSAGE;
}
int sh_request(uint8_t *out, size_t capacity, uint8_t device,
               uint8_t u0, uint8_t u1, uint8_t u2, uint8_t id,
               uint16_t response_length, const uint8_t *payload, size_t length)
{
    uint8_t *m;
    if (out == NULL || (payload == NULL && length != 0))
        return SH_ARGUMENT;
    if (capacity < SH_PACKET_SIZE || length > SH_REQUEST_MAX)
        return SH_BOUNDS;
    memset(out, 0, SH_PACKET_SIZE);
    out[0] = 0x40;
    out[1] = device;
    put16(out + 6, (uint16_t)(length + 10));
    m = out + 8;
    m[0] = u0; m[1] = u1; m[2] = u2; m[3] = id;
    put16(m + 4, response_length);
    put16(m + 6, (uint16_t)length);
    if (length != 0)
        memcpy(m + 8, payload, length);
    put16(m + 8 + length, sh_crc16(m, 8 + length));
    put16(out + 254, sh_crc16(out, 254));
    return SH_MESSAGE;
}
