/* SPDX-License-Identifier: GPL-2.0-only
 * Integration of Asahi-derived framing/handshake and independently MIT HID core.
 * No fixture descriptors, no retry/reset into a new uncorrelated session.
 */
#include "j313_session.h"
static int failed(struct j313_session *s, int detail, unsigned domain)
{
 if (!s->failure_domain) {
  s->failure_domain = domain; s->failure_detail = detail;
  s->failure_phase = s->init.failure_detail ? s->init.failure_phase : s->init.phase;
 }
 s->phase = JS_FAILED; s->detail = detail;
 sh_reset(&s->rx); hk_state_init(&s->keys);
 return JS_ERROR;
}
void js_start(struct j313_session *s, uint64_t now)
{
 struct sh_init_action a;
 memset(s, 0, sizeof(*s)); s->last_ms = now;
 if (sh_init_restart(&s->init, now, &a) != SH_INIT_WAIT) (void)failed(s, SH_INIT_STATE, JS_DOMAIN_INIT);
}
void js_stop(struct j313_session *s)
{
 s->phase = JS_STOPPED; sh_reset(&s->rx); hk_state_init(&s->keys);
}
int js_tick(struct j313_session *s, uint64_t now)
{
 struct sh_init_action a;
 int r;
 if (s->phase == JS_FAILED || s->phase == JS_STOPPED) return JS_ERROR;
 if (now < s->last_ms) return failed(s, SH_INIT_CLOCK, JS_DOMAIN_CLOCK);
 s->last_ms = now;
 if (s->rx.used && now - s->fragment_ms >= 100) return failed(s, SH_INIT_TIMEOUT, JS_DOMAIN_FRAGMENT);
 if (s->phase == JS_INIT) {
  r = sh_init_poll(&s->init, now, &a);
  if (r < 0) return failed(s, r, JS_DOMAIN_INIT);
 }
 return JS_WAIT;
}
static int init_result(struct j313_session *s, int r)
{
 size_t n;
 const uint8_t *d;
 /* A CRC-valid unrelated reply is not a terminal handshake failure.
  * Keep the existing deadline and event budget; never accept its descriptor. */
 if (r == SH_INIT_REJECT) { s->detail = r; return JS_DROP; }
 if (r < 0) return failed(s, r, JS_DOMAIN_INIT);
 if (r == SH_INIT_REQUEST) return JS_TX;
 if (r != SH_INIT_COMPLETE) return JS_WAIT;
 d = sh_init_descriptor(&s->init, 1, &n);
 r = hk_parse(d, n, &s->descriptor);
 if (r != HK_OK) return failed(s, r, JS_DOMAIN_DESCRIPTOR);
 hk_state_init(&s->keys);
 s->phase = JS_READY;
 return JS_PUBLISH;
}
int js_packet(struct j313_session *s, uint64_t now, const uint8_t *p,
              struct sh_init_action *a, hk_result *keys)
{
 struct sh_message m;
 int r;
 memset(a, 0, sizeof(*a)); memset(keys, 0, sizeof(*keys));
 if (js_tick(s, now) == JS_ERROR) return JS_ERROR;
 if (s->phase == JS_INIT && s->init.phase == SH_INIT_BOOT)
  return init_result(s, sh_init_boot_packet(&s->init, now, p, SH_PACKET_SIZE, a));
 if (!s->rx.used) s->fragment_ms = now;
 r = sh_feed(&s->rx, p, SH_PACKET_SIZE, &m);
 if (r < 0) {
  s->detail = r;
  if (s->phase == JS_INIT || ++s->errors >= 8) return failed(s, r, JS_DOMAIN_PACKET);
  return JS_DROP;
 }
 if (r != SH_MESSAGE) return JS_WAIT;
 if (s->phase == JS_INIT) return init_result(s, sh_init_message(&s->init, now, &m, a));
 /* Pinned Asahi routing: unknown1/2 are deliberately not invented selectors. */
 if (m.flags != 0x20 || m.device != 1 || m.unknown0 != 0x10 || !m.length) return JS_WAIT;
 r = hk_decode(&s->descriptor, &s->keys, m.payload, m.length, keys);
 if (r != HK_OK) {
  s->detail = r;
  if (++s->errors >= 8) return failed(s, r, JS_DOMAIN_INPUT);
  return JS_DROP;
 }
 s->errors = 0;
 return JS_KEYS;
}
