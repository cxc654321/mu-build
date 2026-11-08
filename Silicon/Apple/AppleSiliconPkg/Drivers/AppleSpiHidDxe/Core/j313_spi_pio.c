/* SPDX-License-Identifier: GPL-2.0-only
 * Derived register programming sequence: Copyright The Asahi Linux Contributors;
 * based on spi-sifive.c, Copyright 2018 SiFive, Inc. See README.md.
 * Independent host-only polling adaptation with whole-transaction bounds.
 */
#include "j313_spi_pio.h"
struct engine {
 const struct pio_io *io;
 const struct pio_config *cfg;
 struct pio_result r;
 uint64_t start, last;
};
static uint32_t rd(struct engine *e, uint32_t reg) {
 return e->io->read32(e->io->ctx, reg);
}
static void wr(struct engine *e, uint32_t reg, uint32_t v) {
 e->io->write32(e->io->ctx, reg, v);
}
static void mask(struct engine *e, uint32_t reg, uint32_t clear, uint32_t set) {
 wr(e, reg, (rd(e, reg) & ~clear) | set);
}
static bool check(struct engine *e) {
 uint64_t now = e->io->now_us(e->io->ctx);
 if (now < e->last) e->r.status = PIO_CLOCK_REVERSED;
 else if (now - e->start >= e->cfg->timeout_us) e->r.status = PIO_TIMEOUT;
 else if (e->r.steps >= e->cfg->step_budget) e->r.status = PIO_STEP_LIMIT;
 e->last = now;
 if (e->r.status != PIO_OK) return false;
 ++e->r.steps;
 return true;
}
static bool pause_us(struct engine *e, uint32_t us) {
 uint64_t begin;
 if (!check(e)) return false;
 begin = e->last;
 while (e->last - begin < us) {
  e->io->delay_us(e->io->ctx, 1);
  if (!check(e)) return false;
 }
 return true;
}
static bool errors(struct engine *e) {
 e->r.fifo_error_flags |= rd(e, P_IF_FIFO) & (P_RX_UNDERRUN | P_TX_OVERFLOW);
 if (e->r.fifo_error_flags) { e->r.status = PIO_FIFO_ERROR; return false; }
 return true;
}
static bool transfer(struct engine *e, const struct pio_segment *s) {
 size_t queued = 0, received = 0, retired = 0;
 uint32_t needed = (s->tx ? P_TX_DONE : 0) | (s->rx ? P_RX_DONE : 0);
 bool running = false;
 wr(e, P_CTRL, P_RESET);
 wr(e, P_IF_XFER, UINT32_MAX);
 wr(e, P_IF_FIFO, UINT32_MAX);
 wr(e, P_TXCNT, s->tx ? (uint32_t)s->len : 0);
 wr(e, P_RXCNT, s->rx ? (uint32_t)s->len : 0);
 for (;;) {
  uint32_t fs, txlevel, rxlevel, flags;
  if (!check(e) || !errors(e)) return false;
  /* Sample done before occupancy: the last TX byte may retire between reads.
   * A newly asserted done flag is safely observed on the next iteration. */
  flags = rd(e, P_IF_XFER);
  fs = rd(e, P_FIFOSTAT);
  txlevel = (fs >> 8) & 255;
  rxlevel = fs >> 24;
  if (txlevel > 16 || rxlevel > 16 || txlevel > queued - retired) {
   e->r.status = PIO_BAD_FIFO_LEVEL; return false;
  }
  e->r.tx_completed += queued - txlevel - retired;
  retired = queued - txlevel;
  if (s->tx && (flags & P_TX_DONE) && (queued != s->len || txlevel)) {
   e->r.status = PIO_PREMATURE_TX; return false;
  }
  if (rxlevel > (s->rx ? s->len - received : 0)) {
   e->r.status = PIO_EXTRA_RX; return false;
  }
  /* Only occupancy, never threshold/done flags, authorizes RXDATA reads. */
  while (rxlevel--) {
   if (!check(e)) return false;
   s->rx[received++] = (uint8_t)rd(e, P_RXDATA);
   ++e->r.rx_completed;
  }
  if (running && (flags & needed) == needed &&
      (!s->tx || retired == s->len) && (!s->rx || received == s->len)) {
   if (!check(e) || !errors(e)) return false;
   wr(e, P_CTRL, 0);
   return true;
  }
  while (s->tx && queued < s->len && txlevel < 16) {
   if (!check(e)) return false;
   wr(e, P_TXDATA, s->tx[queued++]);
   ++e->r.tx_queued;
   ++txlevel;
  }
  if (!running) { wr(e, P_CTRL, P_RUN); running = true; }
  /* Same deadline even if sticky level flags or slow progress never stop. */
  if (!pause_us(e, 1)) return false;
 }
}
struct pio_result pio_transaction(const struct pio_io *io, const struct pio_config *c,
                                  const struct pio_segment *s, size_t n) {
 struct engine e = {0};
 size_t i;
 uint64_t total = 0;
 e.r.status = PIO_INVALID;
 if (!io || !c || !s || !n || !io->read32 || !io->write32 ||
     !io->now_us || !io->delay_us || !c->exclusive_owner ||
     !c->resources_validated || !c->bus_config_validated ||
     !c->clock_divider || c->clock_divider > 0x7ff ||
     !c->timeout_us || !c->step_budget) return e.r;
 for (i = 0; i < n; ++i) {
  if (!s[i].len || s[i].len > UINT32_MAX || (!s[i].tx && !s[i].rx) ||
      UINT64_MAX - total < s[i].len) return e.r;
  total += s[i].len;
 }
 e.io = io; e.cfg = c; e.r.status = PIO_OK;
 e.start = e.last = io->now_us(io->ctx);
 e.r.started = true;
 wr(&e, P_CTRL, 0);
 wr(&e, P_IE_XFER, 0); wr(&e, P_IE_FIFO, 0);
 mask(&e, P_PIN, P_CS, P_CS);
 mask(&e, P_SHIFTCFG, (1u << 24) | (63u << 16), 8u << 16);
 mask(&e, P_PINCFG, 1u << 9, 1u << 1);
 wr(&e, P_CFG, P_MODE_IRQ | (c->cpol ? 4u : 0u) | (c->cpha ? 2u : 0u));
 wr(&e, P_CLKDIV, c->clock_divider);
 wr(&e, P_WORD_DELAY, 0); wr(&e, P_DELAY_PRE, 0); wr(&e, P_DELAY_POST, 0);
 /* Conservatively establish inactive time even on the first transaction. */
 if (!pause_us(&e, c->inactive_us)) goto out;
 mask(&e, P_PIN, P_CS, 0);
 if (!pause_us(&e, c->setup_us)) goto out;
 for (i = 0; i < n; ++i) {
  if (!transfer(&e, &s[i])) goto out;
  ++e.r.segments_completed;
  if (!pause_us(&e, s[i].delay_after_us)) goto out;
 }
 if (!pause_us(&e, c->hold_us)) goto out;
 mask(&e, P_PIN, P_CS, P_CS);
 if (!pause_us(&e, c->inactive_us)) goto out;
 if (check(&e)) (void)errors(&e);
out:
 /* Cleanup is unconditional and deliberately not blocked by exhausted budget.
  * Callbacks must return: software cannot bound blocking MMIO or cleanup.
  * Failed calls release CS immediately; timing minima are not promised on error.
  */
 wr(&e, P_CTRL, 0);
 mask(&e, P_PIN, P_CS, P_CS);
 wr(&e, P_IE_XFER, 0); wr(&e, P_IE_FIFO, 0);
 return e.r;
}
