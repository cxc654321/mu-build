/* SPDX-License-Identifier: MIT */
#ifndef J313_HID_KEYBOARD_H
#define J313_HID_KEYBOARD_H
#include "J313CoreCompat.h"
#include "J313CoreCompat.h"
#define HK_MAX_DESCRIPTOR 4096u
#define HK_MAX_REPORTS 8u
#define HK_MAX_FIELDS 64u
#define HK_MAX_BITS 4096u
/* Usage tables cover page 7 through FF; emitted key bitmap stays 00..E7. */
#define HK_MAX_USAGES 232u
#define HK_USAGE_TABLE_SIZE 256u
#define HK_BITMAP_BYTES 29u
#define HK_MAX_EVENTS 232u

typedef enum {
 HK_OK = 0, HK_INVALID, HK_UNSUPPORTED, HK_LIMIT, HK_TRUNCATED,
 HK_UNKNOWN_REPORT, HK_ROLLOVER
} hk_status;
typedef struct {
 uint16_t offset, count, usage_count;
 uint8_t size, variable;
 uint16_t logical_min, logical_max;
 uint8_t usages[HK_USAGE_TABLE_SIZE];
} hk_field;
typedef struct {
 uint8_t id;
 /* Independent payload lengths; bits is Input, excluding the report ID. */
 uint16_t bits, output_bits, feature_bits, field_count;
 hk_field fields[HK_MAX_FIELDS];
} hk_report;
typedef struct {
 uint32_t valid;
 uint8_t numbered, report_count;
 hk_report reports[HK_MAX_REPORTS];
} hk_descriptor;
typedef struct {
 uint8_t per_report[HK_MAX_REPORTS][HK_BITMAP_BYTES];
} hk_state;
typedef struct { uint8_t usage, pressed; } hk_event;
typedef struct {
 size_t count;
 hk_event events[HK_MAX_EVENTS];
 uint8_t modifiers;
} hk_result;
/* Descriptor is immutable after successful parse. Failure clears *out.
 * Large descriptor: allocate outside small firmware stacks. No heap used.
 * Public structs are not an untrusted serialized format: do not mutate them.
 */
hk_status hk_parse(const uint8_t *bytes, size_t length, hk_descriptor *out);
void hk_state_init(hk_state *state);
/* State must be initialized and paired with this immutable descriptor.
 * Exact report length required; numbered reports include the ID byte.
 * All failures leave state unchanged and clear result. */
hk_status hk_decode(const hk_descriptor *descriptor, hk_state *state,
 const uint8_t *bytes, size_t length, hk_result *result);
#endif
