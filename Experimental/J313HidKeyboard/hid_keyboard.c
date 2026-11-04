/* SPDX-License-Identifier: MIT
 * Independent implementation of USB HID 1.11 short items and HUT keyboard page.
 * No code copied from kernel, firmware, or third-party HID parsers.
 */
#include "hid_keyboard.h"
#include <string.h>
#define HK_MAGIC UINT32_C(0x484b3031)
#define FAIL(s) do { memset(out, 0, sizeof(*out)); return (s); } while (0)

hk_status hk_parse(const uint8_t *b, size_t n, hk_descriptor *out)
{
 size_t pos = 0;
 uint32_t page = 0, size = 0, count = 0, lo = 0, hi = 0;
 unsigned have_lo = 0, have_hi = 0, opened = 0, closed = 0;
 unsigned id = 0, mode = 0, uc = 0, range = 0;
 uint32_t umin = 0;
 uint8_t usages[HK_MAX_USAGES];
 if (!out) return HK_INVALID;
 memset(out, 0, sizeof(*out));
 if (!b || !n) return HK_INVALID;
 if (n > HK_MAX_DESCRIPTOR) return HK_LIMIT;
 while (pos < n) {
  unsigned prefix = b[pos++], len = prefix & 3u;
  unsigned type = (prefix >> 2) & 3u, tag = prefix >> 4;
  uint32_t v = 0;
  if (prefix == 0xfe) FAIL(HK_UNSUPPORTED);
  if (len == 3) len = 4;
  if (len > n - pos) FAIL(HK_TRUNCATED);
  for (unsigned j = 0; j < len; ++j) v |= (uint32_t)b[pos++] << (8u*j);
  if (closed) FAIL(HK_UNSUPPORTED);
  if (type == 1) {
   if (!len) FAIL(HK_INVALID);
   switch (tag) {
   case 0:
    if (uc || range || (v != 1 && v != 7)) FAIL(HK_UNSUPPORTED);
    page = v; break;
   case 1:
    if (v & (UINT32_C(1) << (len*8u-1u))) FAIL(HK_UNSUPPORTED);
    if (v > 65535u) FAIL(HK_LIMIT);
    lo = v; have_lo = 1; break;
   case 2:
    if (v > 65535u) FAIL(HK_LIMIT);
    hi = v; have_hi = 1; break;
   case 7: if (!v || v > 16) FAIL(HK_LIMIT); size = v; break;
   case 8:
    if (len != 1 || !v) FAIL(HK_INVALID);
    if (mode == 1) FAIL(HK_UNSUPPORTED);
    mode = 2; id = v; out->numbered = 1; break;
   case 9: if (!v || v > HK_MAX_BITS) FAIL(HK_LIMIT); count = v; break;
   default: FAIL(HK_UNSUPPORTED);
   }
  } else if (type == 2) {
   if (!len || len == 4) FAIL(HK_UNSUPPORTED);
   if (v >= HK_MAX_USAGES) FAIL(HK_UNSUPPORTED);
   if (tag == 0) {
    if (range) FAIL(HK_UNSUPPORTED);
    if (uc == HK_MAX_USAGES) FAIL(HK_LIMIT);
    usages[uc++] = (uint8_t)v;
   } else if (tag == 1) {
    if (range || uc) FAIL(HK_UNSUPPORTED);
    range = 1; umin = v;
   } else if (tag == 2) {
    if (range != 1 || v < umin) FAIL(HK_INVALID);
    for (uint32_t u = umin; u <= v; ++u) usages[uc++] = (uint8_t)u;
    range = 2;
   } else FAIL(HK_UNSUPPORTED);
  } else if (type == 0) {
   if (range == 1) FAIL(HK_INVALID);
   if (tag == 10) {
    if (len != 1 || v != 1 || opened || page != 1 || uc != 1 || usages[0] != 6)
     FAIL(HK_UNSUPPORTED);
    opened = 1;
   } else if (tag == 12) {
    if (len || !opened || uc) FAIL(HK_INVALID);
    closed = 1;
   } else if (tag == 8) {
    hk_report *r = NULL;
    unsigned constant = v & 1u, variable = v & 2u;
    if (!opened || !len || !size || !count) FAIL(HK_INVALID);
    if (v != 0 && v != 1 && v != 2) FAIL(HK_UNSUPPORTED);
    if (!mode) mode = 1;
    for (unsigned j = 0; j < out->report_count; ++j)
     if (out->reports[j].id == id) r = &out->reports[j];
    if (!r) {
     if (out->report_count == HK_MAX_REPORTS) FAIL(HK_LIMIT);
     r = &out->reports[out->report_count++]; r->id = (uint8_t)id;
    }
    if (size * count > HK_MAX_BITS - r->bits) FAIL(HK_LIMIT);
    if (!constant) {
     hk_field *f;
     if (page != 7 || !have_lo || !have_hi || lo > hi || hi >= (UINT32_C(1) << size))
      FAIL(HK_UNSUPPORTED);
     if (!uc || count > HK_MAX_USAGES) FAIL(HK_UNSUPPORTED);
     if (variable) {
      if (lo != 0 || hi != 1 || uc != count) FAIL(HK_UNSUPPORTED);
      for (unsigned j = 0; j < uc; ++j) if (usages[j] < 4) FAIL(HK_UNSUPPORTED);
     } else if (hi - lo + 1 != uc) FAIL(HK_UNSUPPORTED);
     if (r->field_count == HK_MAX_FIELDS) FAIL(HK_LIMIT);
     f = &r->fields[r->field_count++];
     f->offset = r->bits; f->size = (uint8_t)size; f->count = (uint16_t)count;
     f->variable = variable != 0; f->logical_min = (uint16_t)lo;
     f->logical_max = (uint16_t)hi; f->usage_count = (uint16_t)uc;
     memcpy(f->usages, usages, uc);
    }
    r->bits = (uint16_t)(r->bits + size * count);
   } else FAIL(HK_UNSUPPORTED);
   uc = 0; range = 0;
  } else FAIL(HK_UNSUPPORTED);
 }
 if (!closed || !out->report_count || uc || range) FAIL(HK_INVALID);
 for (unsigned j = 0; j < out->report_count; ++j)
  if (!out->reports[j].field_count) FAIL(HK_UNSUPPORTED);
 out->valid = HK_MAGIC;
 return HK_OK;
}

void hk_state_init(hk_state *s) { if (s) memset(s, 0, sizeof(*s)); }
static unsigned bit(const uint8_t *p, unsigned u) { return (p[u/8] >> (u%8)) & 1u; }
static void set(uint8_t *p, unsigned u) { p[u/8] |= (uint8_t)(1u << (u%8)); }
/* Caller has verified exact payload size and immutable parsed field limits. */
static uint32_t extract(const uint8_t *b, unsigned offset, unsigned size)
{
 uint32_t v = 0;
 for (unsigned j = 0; j < size; ++j) v |= (uint32_t)bit(b, offset+j) << j;
 return v;
}
hk_status hk_decode(const hk_descriptor *d, hk_state *s,
 const uint8_t *b, size_t n, hk_result *out)
{
 unsigned index = HK_MAX_REPORTS;
 uint8_t next[HK_BITMAP_BYTES] = {0};
 const hk_report *r;
 if (!out) return HK_INVALID;
 memset(out, 0, sizeof(*out));
 if (!d || !s || !b || d->valid != HK_MAGIC) return HK_INVALID;
 if (!n) return HK_TRUNCATED;
 for (unsigned j = 0; j < d->report_count; ++j)
  if (d->reports[j].id == (d->numbered ? b[0] : 0)) index = j;
 if (index == HK_MAX_REPORTS) return HK_UNKNOWN_REPORT;
 r = &d->reports[index];
 if (d->numbered) { ++b; --n; }
 if (n < (r->bits + 7u)/8u) return HK_TRUNCATED;
 if (n != (r->bits + 7u)/8u) return HK_INVALID;
 for (unsigned j = 0; j < r->field_count; ++j) {
  const hk_field *f = &r->fields[j];
  for (unsigned k = 0; k < f->count; ++k) {
   uint32_t v = extract(b, f->offset + k*f->size, f->size);
   unsigned u;
   if (v < f->logical_min || v > f->logical_max) return HK_INVALID;
   if (f->variable) { if (!v) continue; u = f->usages[k]; }
   else u = f->usages[v - f->logical_min];
   if (!u) continue;
   if (u <= 3) return HK_ROLLOVER;
   set(next, u);
  }
 }
 for (unsigned u = 4; u < HK_MAX_USAGES; ++u) {
  unsigned before = 0, after = 0;
  for (unsigned j = 0; j < d->report_count; ++j) {
   before |= bit(s->per_report[j], u);
   after |= bit(j == index ? next : s->per_report[j], u);
  }
  if (before != after) {
   out->events[out->count].usage = (uint8_t)u;
   out->events[out->count++].pressed = (uint8_t)after;
  }
  if (u >= 0xe0 && after) out->modifiers |= (uint8_t)(1u << (u-0xe0));
 }
 memcpy(s->per_report[index], next, sizeof(next));
 return HK_OK;
}
