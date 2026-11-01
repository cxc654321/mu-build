/**
 * @file T8142FamilyVirtualMemoryMapDefines.h
 *
 * Virtual memory map defines for T8142 family chips (Apple M5).
 *
 * Unlike the earlier families in this tree, these ranges were not copied from a
 * previous SoC -- they are derived from the J704 Apple Device Tree read off real
 * hardware, clustered into 1GB-aligned regions that cover every MMIO `reg`
 * actually present. See docs/T8142-PLATFORM.md in the project root, and
 * tools/adt_extract.py to regenerate the source data.
 *
 * Note that T8142's map derives from T8132 (M4), *not* T8122 (M3), despite the
 * ADT reusing M3's core names. AIC/PMGR/WDT/GPIO sit at addresses identical to
 * M4; the SIO block (UART, I2C) moved down by 0x8000000.
 * See docs/T8132-VS-T8142.md.
 *
 * @copyright Copyright (c) 2026.
 *
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 */

#include <Base.h>

#ifndef APPLE_VIRTUAL_MEMORY_MAP_DEFINES_H_
#define APPLE_VIRTUAL_MEMORY_MAP_DEFINES_H_

//
// "core" MMIO devices - PMGR, AIC, UART, GPIO, display, etc.
// These mappings need to be nGnRnE.
//

//
// 0x210000000 - 0x26f008000: arm-io root, misc.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE 0x200000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_1_SIZE SIZE_2GB

//
// 0x280000000 - 0x287000000: AVE, disp0, DCP, associated DARTs.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE 0x280000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_2_SIZE SIZE_256MB

//
// 0x301000000 - 0x30a688000: JPEG, scaler, SEP, error-handler.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE 0x300000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_3_SIZE SIZE_256MB

//
// 0x380000000 - 0x3ab000000: the densest region, and the one that matters most
// for bringup -- PMGR (0x380700000), AIC (0x381000000), WDT (0x3882b0000), SPMI,
// SMC, AOP, GPIO (0x39a000000), I2C (0x3a5010000), UART0 (0x3a5200000), SIO,
// dispext1/dcpext1. 94 nodes.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE 0x380000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_4_SIZE SIZE_1GB

//
// 0x400000000 - 0x422000000: ACIO, usb-drd0/1/3, ATC PHYs, ANE.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_5_BASE 0x400000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_5_SIZE SIZE_1GB

//
// 0x481050000 - 0x4a4000000: ANS (NVMe), SART, PCIe DARTs, ISP.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_6_BASE 0x480000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_6_SIZE SIZE_1GB

//
// 0x4c5cc0000 - 0x4e28b0000: ISP exclave proxy.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_7_BASE 0x4C0000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_7_SIZE SIZE_1GB

//
// 0x500000000 - 0x507000000: AVD, dispext0, dcpext0.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_8_BASE 0x500000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_8_SIZE SIZE_256MB

//
// 0x55a67c000: exclave SEP manager.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_9_BASE 0x540000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_9_SIZE SIZE_512MB

//
// 0x580000000 - 0x5a3814210: SGX (GPU), GFX ASCs, AIC timebase (0x591180000),
// audio DMA channels.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_10_BASE 0x580000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_10_SIZE SIZE_1GB

//
// 0x5d82b0000 - 0x5e3234000: hibernation watchdog, AOP exclave mailbox,
// ADMAC proxies.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_11_BASE 0x5C0000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_11_SIZE SIZE_1GB

//
// 0x1900000000 - 0x1908010000: SPI NOR flash.
//
#define APPLE_CORE_SYSTEM_MMIO_RANGE_12_BASE 0x1900000000
#define APPLE_CORE_SYSTEM_MMIO_RANGE_12_SIZE SIZE_256MB

//
// PCIe MMIO (mappings need to be nGnRE)
//

//
// --- ECAM / configuration space ---
//
// 0x1c50000000 / 0x1c60000000: apciec0/1 (Thunderbolt/USB4), 0x10000000 each.
//
#define APPLE_PCIE_MMIO_RANGE_1_BASE 0x1C50000000
#define APPLE_PCIE_MMIO_RANGE_1_SIZE SIZE_512MB

//
// 0x1c80000000: apciec3. 0x1cb0000000: apcie, the internal root complex
// (NVMe, Wi-Fi/BT, SD reader).
//
#define APPLE_PCIE_MMIO_RANGE_2_BASE 0x1C80000000
#define APPLE_PCIE_MMIO_RANGE_2_SIZE SIZE_1GB

//
// --- BAR windows (the bridge `ranges`, i.e. where devices' BARs get placed) ---
//
// These are decoded from the `ranges` property of each PCIe controller node in
// the J704 ADT, not from any `reg` entry -- which is why an earlier coverage
// check over `reg` entries alone reported the map as complete while all 58GB of
// this was in fact unmapped. Enumerating PCIe and assigning a BAR in here would
// have faulted.
//
// Per controller the ADT describes one 8GB prefetchable MMIO64 window and ~2GB
// of non-prefetchable MMIO32. The layout matches T602XFamilyPkg's ranges 3-6
// exactly, which is a good independent check on the decode.
//
//   apciec0   MMIO64 0x800000000..0xa00000000   MMIO32 0xa00100000..0xa80000000
//   apcie     MMIO32 0xb80000000..0xbc0000000   MMIO64 0xbc0000000..0xbe0000000
//   apciec1   MMIO64 0xc00000000..0xe00000000   MMIO32 0xe00100000..0xe80000000
//   apciec3   MMIO64 0x1400000000..0x1600000000 MMIO32 0x1600100000..0x1680000000
//
#define APPLE_PCIE_MMIO_RANGE_3_BASE 0x800000000
#define APPLE_PCIE_MMIO_RANGE_3_SIZE SIZE_8GB

#define APPLE_PCIE_MMIO_RANGE_4_BASE 0xA00000000
#define APPLE_PCIE_MMIO_RANGE_4_SIZE SIZE_2GB

//
// Covers both of the internal root complex's windows (0xb80000000..0xbe0000000);
// the tail up to 0xc00000000 is unused but harmless as device memory.
//
#define APPLE_PCIE_MMIO_RANGE_5_BASE 0xB80000000
#define APPLE_PCIE_MMIO_RANGE_5_SIZE SIZE_2GB

#define APPLE_PCIE_MMIO_RANGE_6_BASE 0xC00000000
#define APPLE_PCIE_MMIO_RANGE_6_SIZE SIZE_8GB

#define APPLE_PCIE_MMIO_RANGE_7_BASE 0xE00000000
#define APPLE_PCIE_MMIO_RANGE_7_SIZE SIZE_2GB

#define APPLE_PCIE_MMIO_RANGE_8_BASE 0x1400000000
#define APPLE_PCIE_MMIO_RANGE_8_SIZE SIZE_8GB

#define APPLE_PCIE_MMIO_RANGE_9_BASE 0x1600000000
#define APPLE_PCIE_MMIO_RANGE_9_SIZE SIZE_2GB

//
// NOTE: some things in the ADT are deliberately NOT mapped here.
//
// `vram` (~88MB near 0x105d3aa4000) and `pram` (0x105da274000) are DRAM
// carveouts rather than MMIO and belong to the memory init path. `vram` in
// particular is the framebuffer iBoot hands over, which arrives via
// PcdFrameBufferAddress.
//
// Devices such as `dp855` (0x5300000000) and `mesa` (0x1f400000000) carry
// bus-encoded addresses inherited from their I2C/SPI parents rather than
// CPU-physical ones, so mapping them as MMIO would be wrong.
//

#endif // APPLE_VIRTUAL_MEMORY_MAP_DEFINES_H_
