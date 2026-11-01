/** @file
  MacBook Pro (14-inch, M2 Pro, 2023) MMIO windows, generated from Asahi's device trees.

  DO NOT EDIT. Regenerate with Tools/dtwindows.py generate.

  Source: https://github.com/aurora-silicon/linux @ e8efe09d4f37
          arch/arm64/boot/dts/apple
  Those files are GPL-2.0+ OR MIT; the addresses below are taken under MIT.

  Only `reg` comes from the tree. Interrupts do not: the GSIVs we publish are
  a renumbering of the physical AIC lines that must agree with m1n1's alias
  table and the CSRT, and exists in no device tree. See Docs/PLATFORMS.md.

  Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: MIT
**/

#ifndef NTASI_J414S_WINDOWS_H_
#define NTASI_J414S_WINDOWS_H_

//
// Mca. One macro per window so a table can list them in _CRS order
// and interleave the entries that no device-tree node describes.
//
/* MCA cluster registers -- mca@39b600000 */
#define NTASI_J414S_W_MCA_CLUSTER   { 0x39B600000ULL, 0x10000ULL }
/* MCA switch / DMA glue -- mca@39b600000 reg[1] */
#define NTASI_J414S_W_MCA_SWITCH    { 0x39B500000ULL, 0x20000ULL }
/* ADMAC (audio DMA) -- dma-controller@39b400000 */
#define NTASI_J414S_W_ADMAC         { 0x39B400000ULL, 0x34000ULL }
/* NCO clock generator -- clock-controller@28e03c000 */
#define NTASI_J414S_W_NCO           { 0x28E03C000ULL, 0x14000ULL }
/* i2c1 -- left amps -- i2c@39b044000 */
#define NTASI_J414S_W_I2C1          { 0x39B044000ULL, 0x4000ULL }
/* i2c3 -- right amps -- i2c@39b04c000 */
#define NTASI_J414S_W_I2C3          { 0x39B04C000ULL, 0x4000ULL }
/* pinctrl_ap -- speaker SDZ is pin 57 -- pinctrl@39b028000 */
#define NTASI_J414S_W_PINCTRL_AP    { 0x39B028000ULL, 0x4000ULL }

#endif // NTASI_J414S_WINDOWS_H_
