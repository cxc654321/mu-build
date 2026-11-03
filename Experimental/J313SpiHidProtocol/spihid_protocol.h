/* SPDX-License-Identifier: GPL-2.0-only
 * Protocol adaptation informed by Asahi Linux Contributors' SPI HID core.
 * See README.md for pinned source and inherited attribution.
 */
#ifndef J313_SPIHID_PROTOCOL_H
#define J313_SPIHID_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define SH_PACKET_SIZE 256u
#define SH_PACKET_DATA 246u
#define SH_MESSAGE_MAX 2048u
#define SH_REQUEST_MAX 236u

enum sh_result {
    SH_MORE = 0, SH_MESSAGE = 1,
    SH_ARGUMENT = -1, SH_PACKET_LENGTH = -2, SH_PACKET_CRC = -3,
    SH_BOUNDS = -4, SH_SEQUENCE = -5, SH_MESSAGE_LENGTH = -6,
    SH_MESSAGE_CRC = -7, SH_FLAGS = -8
};
struct sh_rx {
    uint8_t bytes[SH_MESSAGE_MAX];
    size_t used, total;
    uint8_t device, flags;
};
struct sh_message {
    const uint8_t *payload;
    size_t length;
    uint16_t response_length;
    uint8_t device, flags, unknown0, unknown1, unknown2, id;
};
/* data must cover len bytes; NULL permitted only when len is zero. */
uint16_t sh_crc16(const uint8_t *data, size_t len);
/* Initialize before first use; also call on timeout, bus error or cancellation. */
void sh_reset(struct sh_rx *rx);
/* Exactly one packet. All errors reset rx; out is cleared on every call.
 * Completed payload is borrowed until next feed/reset. No overlapping buffers.
 * One stream per rx, externally serialized. rx fields are caller-opaque.
 */
int sh_feed(struct sh_rx *rx, const uint8_t *packet, size_t size,
            struct sh_message *out);
/* Single-packet request; explicit caller-owned ID, no I/O or probe.
 * Payload and output must not overlap. Error leaves output unchanged.
 */
int sh_request(uint8_t *out, size_t capacity, uint8_t device,
               uint8_t unknown0, uint8_t unknown1, uint8_t unknown2,
               uint8_t id, uint16_t response_length,
               const uint8_t *payload, size_t length);
#endif
