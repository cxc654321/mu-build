/* SPDX-License-Identifier: MIT */
#include "key_queue.h"
#include "J313CoreCompat.h"

static uint32_t shift_state(uint8_t modifiers)
{
    /* HID: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI. */
    static const uint32_t bits[8] = {8, 2, 32, 128, 4, 1, 16, 64};
    uint32_t state = KQ_SHIFT_VALID;
    unsigned i;
    for (i = 0; i < 8; ++i)
        if (modifiers & (1u << i)) state |= bits[i];
    return state;
}

static int translate(uint8_t usage, int shift, int caps, kq_key *key)
{
    static const char digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";
    /* 2D..38: non-US # (32) deliberately unsupported. */
    static const uint16_t punctuation[] = {45,61,91,93,92,0,59,39,96,44,46,47};
    static const uint16_t shifted_punctuation[] = {95,43,123,125,124,0,58,34,126,60,62,63};
    key->scan_code = 0;
    key->unicode_char = 0;
    if (usage >= 4 && usage <= 0x1d)
        key->unicode_char = (uint16_t)((shift != caps ? 'A' : 'a') + usage - 4);
    else if (usage >= 0x1e && usage <= 0x27)
        key->unicode_char = (uint16_t)(shift ? shifted_digits[usage - 0x1e] : digits[usage - 0x1e]);
    else if (usage >= 0x2d && usage <= 0x38)
        key->unicode_char = (uint16_t)(shift ? shifted_punctuation[usage - 0x2d] : punctuation[usage - 0x2d]);
    else if (usage >= 0x3a && usage <= 0x45)
        key->scan_code = (uint16_t)(0x0b + usage - 0x3a);
    else switch (usage) {
        case 0x28: key->unicode_char = 0x0d; break;
        case 0x29: key->scan_code = 0x17; break;
        case 0x2a: key->unicode_char = 8; break;
        case 0x2b: key->unicode_char = 9; break;
        case 0x2c: key->unicode_char = ' '; break;
        case 0x49: key->scan_code = 7; break;
        case 0x4a: key->scan_code = 5; break;
        case 0x4b: key->scan_code = 9; break;
        case 0x4c: key->scan_code = 8; break;
        case 0x4d: key->scan_code = 6; break;
        case 0x4e: key->scan_code = 10; break;
        case 0x4f: key->scan_code = 3; break;
        case 0x50: key->scan_code = 4; break;
        case 0x51: key->scan_code = 2; break;
        case 0x52: key->scan_code = 1; break;
        default: break;
    }
    return key->scan_code != 0 || key->unicode_char != 0;
}

void kq_init(kq_queue *queue)
{
    if (!queue) return;
    memset(queue, 0, sizeof(*queue));
    queue->shift_state = KQ_SHIFT_VALID;
    queue->toggle_state = KQ_TOGGLE_VALID;
}

kq_status kq_feed(kq_queue *queue, const hk_result *result)
{
    uint8_t seen[HK_MAX_USAGES] = {0};
    size_t i;
    int overflow = 0;
    if (!queue || !result || result->count > HK_MAX_EVENTS) return KQ_INVALID;
    /* Decoder emits unique usages, with boolean pressed. Validate before any
     * mutation; unsupported but in-range usages are harmlessly ignored. */
    for (i = 0; i < result->count; ++i) {
        hk_event event = result->events[i];
        if (event.usage < 4 || event.usage >= HK_MAX_USAGES ||
            event.pressed > 1 || seen[event.usage]) return KQ_INVALID;
        seen[event.usage] = 1;
    }
    queue->shift_state = shift_state(result->modifiers);
    /* A report is a simultaneous snapshot, not chronological key ordering. */
    for (i = 0; i < result->count; ++i) {
        hk_event event = result->events[i];
        if (!event.pressed) continue;
        if (event.usage == 0x39) queue->toggle_state ^= KQ_CAPS_ACTIVE;
        if (event.usage == 0x47) queue->toggle_state ^= 1u;
        if (event.usage == 0x53) queue->toggle_state ^= 2u;
    }
    for (i = 0; i < result->count; ++i) {
        kq_key key = {0};
        if (!result->events[i].pressed ||
            !translate(result->events[i].usage, (result->modifiers & 0x22) != 0,
                       (queue->toggle_state & KQ_CAPS_ACTIVE) != 0, &key)) continue;
        key.shift_state = queue->shift_state;
        key.toggle_state = queue->toggle_state;
        if (queue->count == KQ_CAPACITY) {
            overflow = 1;
            if (queue->dropped != UINT64_MAX) ++queue->dropped;
        } else {
            queue->keys[(queue->head + queue->count) % KQ_CAPACITY] = key;
            ++queue->count;
        }
    }
    return overflow ? KQ_OVERFLOW : KQ_OK;
}

kq_status kq_pop(kq_queue *queue, kq_key *key)
{
    if (!queue || !key) return KQ_INVALID;
    if (!queue->count) return KQ_EMPTY;
    *key = queue->keys[queue->head];
    queue->head = (queue->head + 1) % KQ_CAPACITY;
    --queue->count;
    return KQ_OK;
}
