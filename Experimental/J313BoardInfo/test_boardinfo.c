/* SPDX-License-Identifier: MIT */
#include "j313_boardinfo.h"
#include <assert.h>
#include <stdio.h>

/* SYNTHETIC evidence: these references never attest to real hardware. */
static struct bi_request fixture(void) {
 struct bi_request q = {0};
 unsigned i;
 q.board = BI_J313; q.soc = BI_T8103;
 q.spi = (struct bi_mmio){UINT64_C(0x23510c000), 0x100000, 0x4000};
 q.ap = (struct bi_mmio){UINT64_C(0x23c100000), 0x200000, 784};
 q.nub = (struct bi_mmio){UINT64_C(0x23d1f0000), 0x300000, 56};
 q.clock_hz = 120000000; q.sck_hz = 8000000;
 q.divider = 15; q.divisor_rule = BI_DIV_RAW;
 q.cs = 0; q.controller_irq = 617; q.controller_irq_level = BI_LEVEL_HIGH;
 q.enable = (struct bi_gpio){BI_GPIO_AP, 195, BI_LEVEL_HIGH};
 q.hid_irq = (struct bi_gpio){BI_GPIO_NUB, 13, BI_LEVEL_LOW};
 for (i = 0; i < 4; ++i) q.mux_pins[i] = 46 + i;
 q.mux_function = 1; q.bits_per_word = 8; q.msb_first = 1;
 q.setup_us = 65; q.hold_us = 65; q.inactive_us = 250;
 for (i = 0; i < BI_FACT_COUNT; ++i)
  q.evidence[i] = (struct bi_evidence){BI_CONFIRMED, 100 + i};
 return q;
}
static unsigned cases;
static void expect(const struct bi_request *q, enum bi_error e) {
 struct bi_result r = bi_validate(q);
 if (r.error != e) fprintf(stderr, "case %u: got %d expected %d\n", cases, r.error, e);
 assert(r.error == e);
 if (e != BI_OK) {
  unsigned i;
  assert(r.resource.divisor == 0);
  assert(r.resource.snapshot.spi.physical_base == 0);
  assert(r.resource.snapshot.clock_hz == 0);
  for (i = 0; i < BI_FACT_COUNT; ++i)
   assert(r.resource.snapshot.evidence[i].state == BI_UNKNOWN);
 }
 ++cases;
}
#define BAD(change, error) do { q = fixture(); change; expect(&q, error); } while (0)
int main(void) {
 struct bi_request q = fixture();
 struct bi_result r;
 struct bi_profile p = bi_expected_profile();
 unsigned i, state;
 expect(&q, BI_OK); expect(NULL, BI_NULL_REQUEST);
 BAD(q.board = BI_BOARD_UNKNOWN, BI_WRONG_IDENTITY);
 BAD(q.soc = (enum bi_soc)999, BI_WRONG_IDENTITY);
 BAD(q.spi.physical_base += 4, BI_BAD_MMIO);
 BAD(q.spi.physical_base += 1, BI_BAD_MMIO);
 BAD(q.spi.mapped_base += 1, BI_BAD_MMIO);
 BAD(q.spi.size = 0x168, BI_BAD_MMIO);
 BAD(q.spi.size = 0x4004, BI_BAD_MMIO);
 BAD(q.spi.size = 0, BI_BAD_MMIO);
 BAD(q.spi.mapped_base = 0, BI_BAD_MMIO);
 BAD(q.spi.physical_base = UINT64_MAX - 3, BI_ADDRESS_OVERFLOW);
 BAD(q.spi.mapped_base = UINT64_MAX - 3, BI_ADDRESS_OVERFLOW);
 BAD(q.ap.mapped_base = q.spi.mapped_base, BI_BAD_MMIO);
 BAD(q.ap.size = 780, BI_BAD_MMIO);
 BAD(q.nub.size = 52, BI_BAD_MMIO);
 BAD(q.ap.physical_base = q.nub.physical_base, BI_BAD_MMIO);
 BAD(q.enable.controller = BI_GPIO_NUB, BI_BAD_ROUTE);
 BAD(q.hid_irq.controller = BI_GPIO_AP, BI_BAD_ROUTE);
 BAD(q.hid_irq.active = BI_LEVEL_HIGH, BI_BAD_ROUTE);
 BAD(q.controller_irq = 13, BI_BAD_ROUTE);
 BAD(q.cs = 1, BI_BAD_ROUTE);
 BAD(q.mux_pins[0] = 49, BI_BAD_PINMUX);
 BAD(q.mux_function = 0, BI_BAD_PINMUX);
 BAD(q.clock_hz = 0, BI_BAD_CLOCK);
 BAD(q.clock_hz = 200000000, BI_BAD_CLOCK);
 BAD(q.sck_hz = 0, BI_BAD_CLOCK);
 BAD(q.sck_hz = 8000001, BI_BAD_CLOCK);
 BAD(q.divider = 0, BI_BAD_DIVIDER);
 BAD(q.divider = 0x800, BI_BAD_DIVIDER);
 BAD(q.divider = 14, BI_BAD_DIVIDER);
 BAD(q.divisor_rule = BI_DIV_UNKNOWN, BI_BAD_DIVIDER);
 BAD(q.cpha = 1, BI_BAD_BUS);
 BAD(q.bits_per_word = 0, BI_BAD_BUS);
 BAD(q.inactive_us = 0, BI_BAD_BUS);
 /* Every individual missing, expected-only, rejected, invalid enum or
  * unauditable evidence must fail even with every numeric value matching. */
 for (i = 0; i < BI_FACT_COUNT; ++i) {
  for (state = 0; state < 5; ++state) {
   if (state == BI_CONFIRMED) continue;
   q = fixture(); q.evidence[i].state = (enum bi_state)state;
   expect(&q, BI_UNCONFIRMED);
   assert(bi_validate(&q).missing_fact == (enum bi_fact)i);
  }
  BAD(q.evidence[i].reference = 0, BI_UNCONFIRMED);
 }
 q = fixture();
 for (i = 0; i < BI_FACT_COUNT; ++i) q.evidence[i] = (struct bi_evidence){0};
 assert(q.clock_hz == p.clock_hz && q.spi.physical_base == p.spi_base);
 expect(&q, BI_UNCONFIRMED); /* Exact profile is not runtime proof. */
 q = fixture(); q.spi.size = 0x16c; expect(&q, BI_OK);
 q = fixture(); q.divider = 0x7ff; q.sck_hz = 58623; expect(&q, BI_OK);
 q.sck_hz = 58622; expect(&q, BI_BAD_DIVIDER);
 q = fixture(); q.divider = 14; q.divisor_rule = BI_DIV_RAW_PLUS_ONE;
 expect(&q, BI_OK); r = bi_validate(&q); assert(r.resource.divisor == 15);
 q.clock_hz = 0; assert(r.resource.snapshot.clock_hz == 120000000);
 printf("PASS %u boardinfo host cases; no hardware/backend linked\n", cases);
 return 0;
}
