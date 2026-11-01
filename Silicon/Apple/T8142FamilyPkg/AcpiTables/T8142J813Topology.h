/** @file
  Measured T8142 / J813 (M5 MacBook Air) topology and native AIC3 geometry.

  WHAT THIS FILE IS FOR
  ---------------------
  Until now the AIC3 numbers below existed only as prose: a parenthesis in
  T8142FamilyVirtualMemoryMapDefines.h ("AIC (0x381000000)") and a table in
  Readme.md.  Prose cannot be checked by a compiler.  CSRT.aslc carries the
  AIC3 payload as transcribed bytes -- as T602XFamilyPkg's does, and for the
  same reason: a byte stream whose length, checksum and three nested
  sub-lengths all move together is safer transcribed whole than assembled from
  correlated edits.  These #defines therefore do NOT build the table.  They
  exist so that CSRT.aslc can STATIC_ASSERT the bytes it carries against named
  constants, turning "someone regenerated the table for a different SoC" from
  a silent boot failure into a build failure.

  PROVENANCE
  ----------
  Every AIC3 value was read off this machine by m1n1's src/aic.c, which takes
  them from the live Apple Device Tree and from CAP0 / MAXNUMIRQ on the
  controller itself.  m1n1 prints the summary line:

    AIC3 with 1/2 dies, 2400/4096 IRQs, reg_size:40004, config:10000,
    extintrcfg_stride:04a00, intmaskset_stride:04a00, intmaskclear_stride:04a00

  Nothing here is inherited from T6020/J414s or from T8132.  The same values
  are pinned driver-side by ntasi_aic3_t8142_fixture()
  (AuroraSilicon/drivers/AppleAic/aic3_platform.c) and asserted by
  drivers/AppleAic/tests/test_aic3_layout.c.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#ifndef T8142_J813_TOPOLOGY_H_
#define T8142_J813_TOPOLOGY_H_

//
// Processor topology.  Two asymmetric clusters, ten cores.  MADT_Static.aslc
// and PPTT.aslc still spell these out independently -- this header does not
// yet feed them, and deliberately does not change them, because rewiring the
// two tables that decide whether the machine boots is not part of bringing up
// the interrupt controller.  The one define with a consumer today is
// T8142_J813_CPU_COUNT, which CSRT.aslc checks against the CPU count encoded
// in the AIC3 payload (see below).
//
#define T8142_J813_CPU_COUNT                10
#define T8142_J813_CLUSTER_COUNT             2
#define T8142_J813_E_CORE_COUNT              6
#define T8142_J813_P_CORE_COUNT              4

#define T8142_J813_E_UID_BASE                0
#define T8142_J813_P_UID_BASE                6

//
// ADT cpu6 -- GET_MPIDR(0, 1, 1, 0) -- is the core firmware and Windows
// actually run on, so it is the MADT's mandatory-first bootstrap entry.
// m1n1 reports "CPU init (MIDR: 0x612f0630 smp_id:0x6)" and tags every guest
// exception [cpu6].
//
#define T8142_J813_BOOT_CPU_UID              6

//
// NOTE, NOT A TYPO, AND NOT FIXED HERE.  ACPI 6.3 defines a LOWER Processor
// Power Efficiency Class as MORE efficient, so E-cores should be 0 and
// P-cores 1 -- which is what T6020J414sTopology.h does.  J813's MADT emits
// the opposite (E = 1, P = 0), justifying it as "lower value == higher
// performance".  These defines describe what the MADT actually emits, because
// a header that disagreed with the table would be worse than the
// inconsistency it papers over.  Correcting the polarity is a scheduler-
// visible change and must be its own experiment, not a side effect of the
// AIC3 work.
//
#define T8142_J813_E_EFFICIENCY_CLASS        1
#define T8142_J813_P_EFFICIENCY_CLASS        0

//
// Architected timer GSIVs.  Unlike T6020's 26/27/29/30 these are arbitrary:
// on Apple silicon the timers are per-CPU FIQs, not GIC PPIs, so the numbers
// only have to agree between GTDT and whatever presents the interrupt.  Kept
// in step with T8142FamilyPkg.dsc.inc:33-36.
//
#define T8142_J813_GTDT_SECURE_EL1_GSIV      19
#define T8142_J813_GTDT_NONSECURE_EL1_GSIV   17
#define T8142_J813_GTDT_VIRTUAL_EL1_GSIV     18
#define T8142_J813_GTDT_NONSECURE_EL2_GSIV   20

//
// ---------------------------------------------------------------------------
// Native AIC3 geometry.
// ---------------------------------------------------------------------------
//
#define T8142_AIC3_CHIP_ID           0x8142U
#define T8142_J813_BOARD_ID          0x24U    // m1n1: "Board-ID: 0x24"

#define T8142_AIC3_CONTROLLER_BASE   0x0000000381000000ULL
#define T8142_AIC3_CONTROLLER_SIZE   0x00000000001CC000ULL

//
// EVENT is CONTROLLER_BASE + the ADT's aic-iack-offset.  Keep the offset and
// the absolute address together: the CSRT carries the absolute address, and
// deriving one from the other by hand is exactly the arithmetic this header
// exists to make checkable.
//
#define T8142_AIC3_EVENT_OFFSET      0x00040000U
#define T8142_AIC3_EVENT_BASE        0x0000000381040000ULL
#define T8142_AIC3_EVENT_SIZE        0x0000000000004000ULL

#define T8142_AIC3_CAP0_OFFSET       4U
#define T8142_AIC3_MAXNUMIRQ_OFFSET  12U
#define T8142_AIC3_CONFIG_OFFSET     0x00010000U   // ADT extint-baseaddress

#define T8142_AIC3_NR_IRQ            2400U
#define T8142_AIC3_MAX_IRQ           4096U
#define T8142_AIC3_NR_DIE            1U
#define T8142_AIC3_MAX_DIE           2U

//
// THE STRIDES COME FROM THE ADT AND MUST NOT BE RECOMPUTED.  m1n1's
// aic23_init() derives a fallback of 0x4800 from the register layout and the
// ADT then overrides it with 0x4a00.  A CSRT that recomputes instead of
// copying is 0x200 wrong per die -- harmless while NR_DIE == 1, which is
// precisely what would let the mistake survive review and bite on a two-die
// part.  The Windows driver independently derives 0x4a00 (0x4000 of config
// plus five (MAX_IRQ >> 5) * 4 bitmaps) and agrees, so on this part the two
// paths corroborate each other.
//
#define T8142_AIC3_EXTINTRCFG_STRIDE   0x00004A00U
#define T8142_AIC3_INTMASKSET_STRIDE   0x00004A00U
#define T8142_AIC3_INTMASKCLEAR_STRIDE 0x00004A00U
#define T8142_AIC3_HWSTATE_STRIDE      0x00004A00U

//
// Global enable: controller_base + 0x14, bit 0.  The HAL extension hardcodes
// the +0x14 offset for AIC3, and T8142 matches it; recorded here so a future
// SoC that moves it fails a build assertion rather than writing bit 0 of an
// arbitrary register.
//
#define T8142_AIC3_GLOBAL_CONFIG_OFFSET  0x14U
#define T8142_AIC3_GLOBAL_CONFIG_ENABLE  0x1U

//
// m1n1 reserves the top 2 * MAX_CPUS lines for its own timer software IRQs
// (hv_exc.c: HV_TIMER_SWIRQ_BASE), i.e. [2352, 2400).  Firmware must not hand
// these to Windows.
//
// MAX_CPUS here is m1n1's COMPILE-TIME maximum of 24 (src/smp.h), not this
// machine's ten cores.  The reserved block is 48 lines wide on J813 and would
// be 48 lines wide on a four-core part too.  Writing it in terms of
// T8142_J813_CPU_COUNT looks natural, gives 2360, and is wrong.
//
#define T8142_M1N1_MAX_CPUS          24U
#define T8142_AIC3_M1N1_SWIRQ_BASE   (T8142_AIC3_NR_IRQ - 2U * T8142_M1N1_MAX_CPUS)
#define T8142_AIC3_M1N1_SWIRQ_LIMIT  T8142_AIC3_NR_IRQ

//
// ---------------------------------------------------------------------------
// Published-GSIV <-> physical-AIC-line aliases (the CSRT "ALI2" tail).
// ---------------------------------------------------------------------------
//
// Windows' ARM64 PnP interrupt arbiter only accepts GSIVs in [32, 1024).
// Both lines below sit in the GIC-reserved 1024..4095 gap and are refused
// outright (problem=12, CM_PROB_NO_VALID_LOG_CONFIG), so firmware publishes a
// low legal number and records the true line here for the HAL to translate.
//
// These pairs MUST stay bit-identical to hv_aic_aliases_t8142[] in m1n1's
// src/hv_aic_alias.c for as long as both exist -- the two were designed to the
// same shape precisely so ownership could hand over from m1n1 to the CSRT
// without the numbers moving.  The published values were not chosen freely:
// a walk of every ADT node carrying an `interrupts` property found 249
// distinct lines inside [32, 1024), and confirmed 995 and 996 are claimed by
// nothing.  Publishing a number that is also a live physical line would
// deliver some other device's interrupts under this device's INTID.
//
// J414s publishes 37..46 for its aliases.  Those numbers are NOT portable
// here: 37 is a real line on this SoC.
//
#define T8142_J813_GSIV_MTP          995U   // -> 1277, dockchannel-mtp
#define T8142_J813_AIC_LINE_MTP     1277U   // keyboard + trackpad

#define T8142_J813_GSIV_ANS          996U   // -> 1155, internal NVMe
#define T8142_J813_AIC_LINE_ANS     1155U   // /arm-io/ans, interrupts[4]

// The LEFT Type-C port.  1511 is interrupts[0] of /arm-io/usb-drd0, the
// DWC3/xHCI controller line -- the same index T8103 published as its lone 777.
// Only this port is aliased: usb-drd1 carries the debug proxy's own cable and
// m1n1 removes its ADT node from the guest device tree entirely.  997 was
// confirmed free by re-running the interrupts walk described above; of
// 980..999 only 984 is claimed.
#define T8142_J813_GSIV_USB          997U   // -> 1511, usb-drd0 xHCI
#define T8142_J813_AIC_LINE_USB     1511U   // /arm-io/usb-drd0, interrupts[0]

#define T8142_J813_ALIAS_COUNT         3U

#endif /* T8142_J813_TOPOLOGY_H_ */
