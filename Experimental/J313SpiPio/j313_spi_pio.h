/* SPDX-License-Identifier: GPL-2.0-only
 * Register definitions/sequence derived from spi-apple.c:
 * Copyright The Asahi Linux Contributors; based on spi-sifive.c,
 * Copyright 2018 SiFive, Inc. See README.md for pinned source.
 */
#ifndef J313_SPI_PIO_H
#define J313_SPI_PIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
 P_CTRL=0x000, P_CFG=0x004, P_PIN=0x00c, P_TXDATA=0x010,
 P_RXDATA=0x020, P_CLKDIV=0x030, P_RXCNT=0x034, P_WORD_DELAY=0x038,
 P_TXCNT=0x04c, P_FIFOSTAT=0x10c, P_IE_XFER=0x130, P_IF_XFER=0x134,
 P_IE_FIFO=0x138, P_IF_FIFO=0x13c, P_SHIFTCFG=0x150,
 P_PINCFG=0x154, P_DELAY_PRE=0x160, P_DELAY_POST=0x168,
 P_RUN=1, P_RESET=12, P_CS=2, P_MODE_IRQ=32,
 P_RX_DONE=1, P_TX_DONE=2, P_RX_UNDERRUN=1<<16, P_TX_OVERFLOW=1<<17
};
struct pio_io {
 void *ctx;
 uint32_t (*read32)(void *, uint32_t);
 void (*write32)(void *, uint32_t, uint32_t);
 uint64_t (*now_us)(void *);
 void (*delay_us)(void *, uint32_t);
};
/* These booleans are caller attestations, NOT hardware validation.
 * Exclusive controller/CS ownership; clocks, pinmux, power and mapping already
 * validated and held for the complete call; no concurrent interrupt/DMA user.
 * Divider is an explicitly validated raw register value, not a frequency.
 */
struct pio_config {
 bool exclusive_owner, resources_validated, bus_config_validated;
 bool cpol, cpha;
 uint16_t clock_divider;
 uint32_t setup_us, hold_us, inactive_us;
 uint64_t timeout_us, step_budget;
};
struct pio_segment {
 const uint8_t *tx;
 uint8_t *rx;
 size_t len;                 /* nonzero, <= UINT32_MAX; at least one buffer */
 uint32_t delay_after_us;    /* CS stays asserted, including after last segment */
};
enum pio_status {
 PIO_OK, PIO_INVALID, PIO_TIMEOUT, PIO_CLOCK_REVERSED, PIO_STEP_LIMIT,
 PIO_FIFO_ERROR, PIO_BAD_FIFO_LEVEL, PIO_PREMATURE_TX, PIO_EXTRA_RX
};
struct pio_result {
 enum pio_status status;
 size_t segments_completed;
 uint64_t tx_queued, tx_completed, rx_completed;
 uint32_t fifo_error_flags;
 uint64_t steps;
 bool started;
};
/* No backend supplied. Buffers/config/io/segments must remain valid and stable;
 * no concurrent call on this controller; no overlapping TX/RX buffers.
 * tx_completed counts FIFO-retired words (not independently observed SCK).
 * Success also requires complete flags, empty TX FIFO and exact RX length.
 * Invalid arguments perform no IO. Any started exit stops RUN, makes CS
 * inactive, disables both IRQ enables. Power is never touched/restored.
 */
struct pio_result pio_transaction(const struct pio_io *, const struct pio_config *,
                                  const struct pio_segment *, size_t);
#endif
