/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef J313_SESSION_H
#define J313_SESSION_H
#include "sh_init.h"
#include "hid_keyboard.h"
enum js_phase { JS_INIT, JS_READY, JS_FAILED, JS_STOPPED };
enum js_result { JS_WAIT, JS_TX, JS_PUBLISH, JS_KEYS, JS_DROP, JS_ERROR = -1 };
/* Error domains disambiguate overlapping negative result enums. */
enum js_domain { JS_DOMAIN_NONE, JS_DOMAIN_INIT, JS_DOMAIN_PACKET,
 JS_DOMAIN_DESCRIPTOR, JS_DOMAIN_FRAGMENT, JS_DOMAIN_CLOCK, JS_DOMAIN_INPUT };
struct j313_session {
 enum js_phase phase;
 struct sh_init init;
 struct sh_rx rx;
 hk_descriptor descriptor; /* Allocate context from pool, NOT firmware stack. */
 hk_state keys;
 uint64_t last_ms, fragment_ms;
 unsigned errors;
 int detail;
 unsigned failure_domain, failure_phase;
 int failure_detail;
};
void js_start(struct j313_session *s, uint64_t now);
void js_stop(struct j313_session *s);
int js_tick(struct j313_session *s, uint64_t now);
/* action/result always cleared; borrowed payload consumed before return. */
int js_packet(struct j313_session *s, uint64_t now, const uint8_t *packet,
              struct sh_init_action *action, hk_result *keys);
#endif
