/* SPDX-License-Identifier: GPL-2.0-only
 * Protocol adaptation: Copyright (C) The Asahi Linux Contributors.
 * Based upstream on applespi.c, Copyright (c) 2015-2018 Federico Lorenzi
 * and Copyright (c) 2017-2018 Ronald Tschalär. See INIT.md.
 */
#ifndef J313_SH_INIT_H
#define J313_SH_INIT_H
#include "spihid_protocol.h"
#define SH_INIT_DESC_MAX 512u
#define SH_INIT_TIMEOUT_MS 1000u
#define SH_INIT_EVENT_MAX 64u

enum sh_init_phase { SH_INIT_IDLE, SH_INIT_BOOT, SH_INIT_DEVICE,
    SH_INIT_IFACE1, SH_INIT_IFACE2, SH_INIT_DESC1, SH_INIT_DESC2,
    SH_INIT_DONE, SH_INIT_FAILED };
enum sh_init_result { SH_INIT_WAIT = 0, SH_INIT_REQUEST = 1,
    SH_INIT_COMPLETE = 2, SH_INIT_REJECT = -1, SH_INIT_ARGUMENT = -2,
    SH_INIT_TIMEOUT = -3, SH_INIT_CLOCK = -4, SH_INIT_LIMIT = -5,
    SH_INIT_STATE = -6 };
struct sh_init_action {
    enum sh_init_phase phase;
    size_t packet_length; /* 256 only on SH_INIT_REQUEST; never performs I/O */
    uint8_t packet[SH_PACKET_SIZE];
};
/* Caller-opaque fields; one externally serialized context, no overlapping
 * context/action/input buffers. Zero allocation, no MMIO or HID installation. */
struct sh_init {
    enum sh_init_phase phase;
    uint64_t last_ms, started_ms;
    unsigned next_id, events, requests, accepted, rejected;
    uint8_t pending_id, pending_type, pending_iface;
    uint16_t pending_response;
    size_t descriptor_length[2];
    uint8_t descriptor[2][SH_INIT_DESC_MAX];
};
/* First use/reset requires transport quiesced and stale RX drained: reset
 * forgets IDs. restart preserves consumed IDs, refuses reuse after 256. */
void sh_init_reset(struct sh_init *s);
int sh_init_restart(struct sh_init *s, uint64_t now_ms,
                    struct sh_init_action *out);
int sh_init_poll(struct sh_init *s, uint64_t now_ms,
                 struct sh_init_action *out);
/* Full short-notification packet; verifies outer CRC itself. No message CRC
 * exists for boot. Not a substitute for sh_feed for ordinary messages. */
int sh_init_boot_packet(struct sh_init *s, uint64_t now_ms,
                        const uint8_t *packet, size_t length,
                        struct sh_init_action *out);
/* ONLY a complete SH_MESSAGE result from sh_feed (both CRCs validated).
 * Payload storage must cover length and remain live for this call. */
int sh_init_message(struct sh_init *s, uint64_t now_ms,
                    const struct sh_message *message,
                    struct sh_init_action *out);
/* Transport/correlation/size validated bytes, NOT HID-semantic validation.
 * Only available after DONE; borrowed until reset/restart. */
const uint8_t *sh_init_descriptor(const struct sh_init *s, unsigned iface,
                                  size_t *length);
#endif
