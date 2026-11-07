/* SPDX-License-Identifier: MIT */
#ifndef J313_KEY_QUEUE_H
#define J313_KEY_QUEUE_H
#include "../J313HidKeyboard/hid_keyboard.h"

#define KQ_CAPACITY 32u
#define KQ_SHIFT_VALID UINT32_C(0x80000000)
#define KQ_TOGGLE_VALID 0x80u
#define KQ_CAPS_ACTIVE 0x04u

/* Numeric values follow SimpleTextIn.h / SimpleTextInEx.h, not ABI structs. */
typedef struct {
    uint16_t scan_code, unicode_char;
    uint32_t shift_state;
    uint8_t toggle_state;
} kq_key;
typedef struct {
    kq_key keys[KQ_CAPACITY];
    size_t head, count;
    uint64_t dropped; /* Saturating count of translated keys dropped on full. */
    uint32_t shift_state;
    uint8_t toggle_state;
} kq_queue;
typedef enum { KQ_OK, KQ_EMPTY, KQ_OVERFLOW, KQ_INVALID } kq_status;

/* Initialize/reset software state, including counters and locks (all off).
 * Caller serializes all access; public state must not be manually mutated. */
void kq_init(kq_queue *queue);
/* Feed each successful hk_decode result exactly once. Invalid batches are
 * rejected atomically. Full queue drops newest, but still updates lock state.
 * Releases, modifiers, locks and unsupported usages never enqueue a key. */
kq_status kq_feed(kq_queue *queue, const hk_result *result);
/* Empty/invalid calls leave *key unchanged. No EFI_STATUS or protocol here. */
kq_status kq_pop(kq_queue *queue, kq_key *key);
#endif
