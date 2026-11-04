/* SPDX-License-Identifier: MIT */
#include "j313_boardinfo.h"
#include <stddef.h>

struct bi_profile bi_expected_profile(void) {
 const struct bi_profile p = {
  BI_J313, BI_T8103, UINT64_C(0x23510c000), 0x4000,
  UINT64_C(0x23c100000), UINT64_C(0x23d1f0000), 120000000,
  8000000, 0, 617, BI_LEVEL_HIGH,
  {BI_GPIO_AP, 195, BI_LEVEL_HIGH}, {BI_GPIO_NUB, 13, BI_LEVEL_LOW},
  {46, 47, 48, 49}, 1, 65, 65, 250
 };
 return p;
}
static enum bi_error mmio(struct bi_mmio m, uint64_t expected,
                          uint64_t minimum, uint64_t maximum) {
 if (!m.physical_base || !m.mapped_base || !m.size ||
     (m.physical_base & 3) || (m.mapped_base & 3) || (m.size & 3))
  return BI_BAD_MMIO;
 /* Require representable exclusive end, not merely representable last byte. */
 if (m.size > UINT64_MAX - m.physical_base ||
     m.size > UINT64_MAX - m.mapped_base) return BI_ADDRESS_OVERFLOW;
 if (m.physical_base != expected || m.size < minimum || m.size > maximum)
  return BI_BAD_MMIO;
 return BI_OK;
}
/* Called only after each end has passed overflow checks. */
static int overlap(struct bi_mmio a, struct bi_mmio b) {
 return (a.mapped_base < b.mapped_base + b.size &&
         b.mapped_base < a.mapped_base + a.size) ||
        (a.physical_base < b.physical_base + b.size &&
         b.physical_base < a.physical_base + a.size);
}
static int gpio_equal(struct bi_gpio a, struct bi_gpio b) {
 return a.controller == b.controller && a.pin == b.pin && a.active == b.active;
}
struct bi_result bi_validate(const struct bi_request *q) {
 struct bi_result r = {0};
 const struct bi_profile p = bi_expected_profile();
 uint64_t divisor;
 unsigned i;
 if (!q) { r.error = BI_NULL_REQUEST; return r; }
 if (q->board != BI_J313 || q->soc != BI_T8103) {
  r.error = BI_WRONG_IDENTITY; return r;
 }
 r.error = mmio(q->spi, p.spi_base, 0x16c, p.spi_size);
 if (r.error) return r;
 /* GPIO per-pin word coverage only; no claim about IRQ register coverage. */
 r.error = mmio(q->ap, p.ap_base, (195 + 1) * 4, UINT64_MAX);
 if (r.error) return r;
 r.error = mmio(q->nub, p.nub_base, (13 + 1) * 4, UINT64_MAX);
 if (r.error) return r;
 if (overlap(q->spi, q->ap) || overlap(q->spi, q->nub) || overlap(q->ap, q->nub)) {
  r.error = BI_BAD_MMIO; return r;
 }
 if (q->cs != p.cs || q->controller_irq != p.controller_irq ||
     q->controller_irq_level != p.controller_irq_level ||
     !gpio_equal(q->enable, p.enable) || !gpio_equal(q->hid_irq, p.hid_irq)) {
  r.error = BI_BAD_ROUTE; return r;
 }
 for (i = 0; i < 4; ++i) {
  if (q->mux_pins[i] != p.mux_pins[i]) {
   r.error = BI_BAD_PINMUX; return r;
  }
 }
 if (q->mux_function != p.mux_function) {
  r.error = BI_BAD_PINMUX; return r;
 }
 /* Deliberately only the documented profile, not arbitrary clock topologies. */
 if (q->clock_hz != p.clock_hz || !q->sck_hz || q->sck_hz > p.max_sck_hz) {
  r.error = BI_BAD_CLOCK; return r;
 }
 if (!q->divider || q->divider > 0x7ff ||
     (q->divisor_rule != BI_DIV_RAW && q->divisor_rule != BI_DIV_RAW_PLUS_ONE)) {
  r.error = BI_BAD_DIVIDER; return r;
 }
 divisor = q->divider + (q->divisor_rule == BI_DIV_RAW_PLUS_ONE ? UINT64_C(1) : 0);
 /* sck_hz is a requested ceiling, not a rounded/measured frequency.
  * Operands bounded above: 8MHz * 2048 cannot overflow uint64_t. */
 if (q->clock_hz > q->sck_hz * divisor) {
  r.error = BI_BAD_DIVIDER; return r;
 }
 if (q->cpol || q->cpha || q->bits_per_word != 8 || q->msb_first != 1 ||
     q->setup_us != p.setup_us || q->hold_us != p.hold_us ||
     q->inactive_us != p.inactive_us) {
  r.error = BI_BAD_BUS; return r;
 }
 for (i = 0; i < BI_FACT_COUNT; ++i) {
  if (q->evidence[i].state != BI_CONFIRMED || !q->evidence[i].reference) {
   r.error = BI_UNCONFIRMED; r.missing_fact = (enum bi_fact)i; return r;
  }
 }
 r.resource.snapshot = *q;
 r.resource.divisor = divisor;
 return r;
}
