/*
 * Copyright (c) 2026 Aurora Silicon
 */

/** @file
  J414s CPU cluster DVFS (P-state) ACPI device.

  The MacBook Pro 14" (M2 Pro, t6020) has three CPU clusters, each with a
  cluster power/frequency controller block whose 64-bit CLUSTER_PSTATE
  register (block base + 0x20020) selects the cluster's operating point.
  Firmware leaves the clusters wherever iBoot parked them (measured on
  hardware 2026-08-14: E-cluster p-state 5 = 2004 MHz, both P-clusters
  p-state 1 = 702 MHz), and nothing in Windows re-programs them, so without
  a driver the P-cores run at 1/5th of their 3504 MHz ceiling forever.

  AppleCpuFreq (ACPI\NTAS0031) consumes exactly three memory windows, in
  this fixed cluster order:

    0: ECPU0 cluster block, 0x210e00000 + 0x50000 (4x Blizzard,
       p-states 2..7 = 912/1284/1752/2004/2256/2424 MHz)
    1: PCPU0 cluster block, 0x211e00000 + 0x50000 (3x Avalanche,
       p-states 1..17 = 702..3504 MHz)
    2: PCPU1 cluster block, 0x212e00000 + 0x50000 (3x Avalanche,
       p-states 1..17 = 702..3504 MHz)

  Within each window the driver touches only: CLUSTER_PSTATE (+0x20020),
  the frequency status/readback registers (+0x20050 status, +0x200c0 /
  +0x200c8 PLL status/factor: MHz = 12 * mult / (div + 1)), and the two
  m1n1 cluster-init registers (+0x200f8 bit 40, +0x440f8) it re-applies at
  start.  The 0x50000 length covers all of these with room for the
  throttle-feature registers (+0x48400 family) it deliberately leaves
  alone.  No interrupt: p-state switches are busy-polled (<= 400 us), as in
  m1n1 and Linux.

  The p-state/frequency tables themselves are compiled into the driver
  (with provenance) rather than read from _DSD -- the _DSD entries here
  mirror the compiled-in contract for verification tooling only.

  Provenance:
    m1n1 src/cpufreq.c t6020_clusters[] (cluster bases, CLUSTER_PSTATE
      layout, SET/BUSY protocol, init register writes)
    Linux arch/arm64/boot/dts/apple/t602x-common.dtsi (blizzard_opp levels
      2..7, avalanche_opp levels 1..17)
    live Mac ADT /arm-io/pmgr voltage-states1 / voltage-states5 (period
      ticks: MHz = 65536e9 / value / 1e6 -- decoded 2026-08-14, matches
      the DT tables exactly)
    live hardware register reads 2026-08-14: ECPU0 CLUSTER_PSTATE
      0x400105 / STATUS 0xa5 / PLL_FACTOR 0x014e0001 (= 2004 MHz),
      PCPU0/1 0x400101 / 0x21 / 0x00ea0003 (= 702 MHz)

  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("CPUF.aml", "SSDT", 0x02, "Apple", "J414CPUF", 0x00000001)
{
    Scope (\_SB)
    {
        Device (CPUF)
        {
            Name (_HID, "NTAS0031")
            Name (_UID, Zero)
            Name (_CCA, One)

            //
            // Keep the ordering in lockstep with AppleCpuFreqEvtPrepareHardware:
            //   0: ECPU0  1: PCPU0  2: PCPU1
            // (m1n1 src/cpufreq.c t6020_clusters[] order.)
            //
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000210E00000,
                    0x0000000210E4FFFF,
                    0x0000000000000000,
                    0x0000000000050000
                    )
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000211E00000,
                    0x0000000211E4FFFF,
                    0x0000000000000000,
                    0x0000000000050000
                    )
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000212E00000,
                    0x0000000212E4FFFF,
                    0x0000000000000000,
                    0x0000000000050000
                    )
            })

            Method (_STA)
            {
                Return (0x0F)
            }

            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "ntasp,cpufreq-chip", "t6020" },
                    Package () { "ntasp,cpufreq-cluster-count", 3 },
                    Package () { "ntasp,cpufreq-cluster-layout", "E4,P3,P3" },
                    Package () { "ntasp,cpufreq-e-pstate-min", 2 },
                    Package () { "ntasp,cpufreq-e-pstate-max", 7 },
                    Package () { "ntasp,cpufreq-p-pstate-min", 1 },
                    Package () { "ntasp,cpufreq-p-pstate-max", 17 },
                    Package () { "ntasp,cpufreq-e-max-mhz", 2424 },
                    Package () { "ntasp,cpufreq-p-max-mhz", 3504 },
                }
            })
        }
    }
}
