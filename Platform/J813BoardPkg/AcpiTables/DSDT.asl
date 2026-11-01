/**
 * Copyright (c) 2023, amarioguy (AppleWOA authors).
 *
 * Module Name:
 *     DSDT.asl
 *
 * Abstract:
 *     Differentiated System Description Table. This source file implements the DSDT table
 *     for the MacBook Air (M5, 2026) platform.
 *
 * Environment:
 *     UEFI firmware/runtime services.
 *
 * License:
 *     Copyright (c) 2026 Aurora Silicon
 *     SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 *
 **/
#include <IndustryStandard/Acpi65.h>

 DefinitionBlock("DSDT.aml", "DSDT", 0x02, "Apple", "J813", 0x8142) {
    Scope(\_SB) {
        //
        // Cluster low power states. On T8101/T8103, there are only 2 clusters, the P and E core clusters,
        // so should be easier to track
        //
        Name (CLPI, Package() {
            0, // Version
            0, // Level Index
            1, // Count
            Package() { // Power Gating state for Cluster
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No Parent State
            0x00000000, // Integer Entry method (currently NULL, TODO actually add an entry method)
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "ClusterRetention"
            },
        })

        //
        // Per processor low power states.
        //
        Name(PLPI, Package() {
            0, // Version
            0, // Level Index
            2, // Count
            Package() { // WFI for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            0, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No parent state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "WFI",
            },
            Package() { // Power Gating state for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            1, // Parent node can be in any state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00000000,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "CorePwrDn"
            },
        })

        //
        // XHC0 -- the LEFT Type-C port's xHCI host controller.
        //
        // Both the address and the interrupt below were read from J813's own
        // ADT (/arm-io/usb-drd0) on 2026-08-15.  The previous values were
        // 0x382280000 and GSIV 777, which are T8103's -- inherited with the
        // rest of this table exactly like COM0's 1097 and the old UART base.
        // Note 0x382280000 and the real 0x402280000 share the low bits
        // 2280000: same register offset, wrong base.
        //
        //   ADT /arm-io/usb-drd0  reg[0] = 0x402280000 size 0x11800
        //                         interrupts = <1511 1512 1513 1514 1489>
        //                         compatible = usb-drd,t8142
        //
        // LENGTH IS 0x11800, NOT 0x100000.  The old 1 MiB window was invented
        // along with the address; the DWC3 register block really is 0x11800.
        //
        // WHY THE LEFT PORT AND ONLY THE LEFT PORT.  m1n1 brings every DRD up
        // in device mode so any one of them can carry the proxy, then releases
        // the unused one to the guest in USB2 host mode -- the boot log says
        // "USB0: releasing controller for guest / USB2 PHY off in guest host
        // mode; DWC3 held for Mu DART handoff".  The proxy's own cable is
        // usb-drd1 (0x40a280000, atc-phy1, port-number 2 = right), and m1n1
        // deletes /arm-io/usb-drd1 and /arm-io/dart-usb1 from the guest device
        // tree so nothing downstream can take the console away.  Publishing a
        // second controller here would hand Windows the port you are debugging
        // over.  XHC1 below stays absent for that reason as well as its own.
        //
        // GSIV 997 IS AN ALIAS, NOT THE PHYSICAL LINE.  1511 is interrupts[0],
        // the DWC3/xHCI controller interrupt -- the same index T8103 published
        // as its single 777.  Windows' PnP interrupt arbiter only accepts
        // GSIVs inside the GICv3 SPI range [32, 1024), and every line this
        // controller owns is above it, so m1n1 aliases published 997 <->
        // physical 1511 on T8142 (src/hv_aic_alias.c, which is the authority
        // for this pairing; 995/1277 and 996/1155 are the same mechanism for
        // MTP and ANS).  997 was picked because an ADT walk shows it free.
        //
        Device (XHC0) {
            Name (_HID, "PNP0D15")
            Name (_UID, One)
            Name (_CCA, One)

            Method (_CRS, 0, Serialized) {
                Name (RBUF, ResourceTemplate () {
                    QWordMemory (
                        ResourceConsumer,     // ResourceUsage
                        PosDecode,            // Decode
                        MinFixed,             // IsMinFixed
                        MaxFixed,             // IsMaxFixed
                        NonCacheable,         // Cacheable
                        ReadWrite,            // ReadAndWrite
                        0,                    // AddressGranularity - GRA
                        0x402280000,          // AddressMinimum - MIN
                        0x4022917FF,          // AddressMaximum - MAX
                        0,                    // AddressTranslation - TRA
                        0x11800               // RangeLength - LEN
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                        997
                    }
                })
                Return (RBUF)
            }

            Method (_STA) {
                Return (0xF)
            }
        }

        Device (XHC1) {
            Name (_HID, "PNP0D15")
            Name (_UID, One)
            Name (_CCA, One)

            Method (_CRS, 0, Serialized) {
                Name (RBUF, ResourceTemplate () {
                    QWordMemory (
                        ResourceConsumer,     // ResourceUsage
                        PosDecode,            // Decode
                        MinFixed,             // IsMinFixed
                        MaxFixed,             // IsMaxFixed
                        NonCacheable,         // Cacheable
                        ReadWrite,            // ReadAndWrite
                        0,                    // AddressGranularity - GRA
                        0x502280000,          // AddressMinimum - MIN
                        0x50237FFFF,          // AddressMaximum - MAX
                        0,                    // AddressTranslation - TRA
                        0x100000              // RangeLength - LEN
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                        857
                    }
                })
                Return (RBUF)
            }

            //
            // Reported absent on this platform.
            //
            // The address above is an M1 one -- MacBookProLate2020Pkg,
            // MacMini2020Pkg and MacBookProLate2025Pkg all carry the identical
            // 0x502280000, and nothing here was ever read off J813.  There is
            // no evidence T8142 has an xHCI controller there, and the measured
            // result of claiming otherwise is a synchronous external abort:
            // Windows enumerated XHC1, mapped it, read a register on cpu8 and
            // took ESR 0x92000410 with DFSC 0x10.  The region is mapped
            // passthrough in stage 2, so the abort came from the hardware
            // itself -- nothing is decoding at that address.
            //
            // Its interrupt could not be delivered either: GSIV 857 would have
            // to arrive through AIC, and no AIC-to-GIC bridge exists yet.
            //
            // XHC0 immediately above is commented out for the same reason.
            // Restore this once a real T8142 xHCI base is read from the ADT and
            // its interrupt can actually be routed.  WinPE boots from a ramdisk
            // and does not need USB to reach the installer.
            //
            Method (_STA) {
                Return (0x0)
            }
        }

        Device(COM0) {
            Name(_HID, "APPL8900") // naming it APPL8900 since the Samsung based UART was used since the 8900
            Name(_UID, Zero)
            Name (_CRS, ResourceTemplate () {
                QWordMemory (
                ResourceConsumer,     // ResourceUsage
                PosDecode,            // Decode
                MinFixed,             // IsMinFixed
                MaxFixed,             // IsMaxFixed
                NonCacheable,         // Cacheable
                ReadWrite,            // ReadAndWrite
                0x0000000000000000,   // AddressGranularity - GRA
                // FixedPcdGet64(PcdAppleUartBase),   // AddressMinimum - MIN
                // (FixedPcdGet64(PcdAppleUartBase) + 0xFFFF),   // AddressMaximum - MAX
                //
                // J813/T8142 uart0.  Was 0x235200000, which is T8103's UART --
                // an M1 address inherited with the rest of this table, backed
                // by nothing on this SoC.  XHC1 above shows what that costs
                // when a driver actually reads it: a synchronous external
                // abort.  This value matches PcdAppleUartBase in
                // T8142FamilyPkg.dsc.inc, which is what the commented-out
                // FixedPcdGet64 above was reaching for before it was hardcoded.
                //
                // It is also the address m1n1 traps as its VUART, so a guest
                // access lands in emulation rather than on the bare device.
                //
                0x3a5200000,   // AddressMinimum - MIN
                0x3a5200fff,   // AddressMaximum - MAX
                0x0000000000000000,   // AddressTranslation - TRA
                0x0000000000001000    // RangeLength - LEN
                )
                //
                // NO Interrupt descriptor -- deliberate, and the same fix
                // MacBookProEarly2023Pkg already applied to its own COM0.
                //
                // This used to publish Interrupt(..., Exclusive) { 1097 }.  Two
                // separate defects, both measured against J813's own ADT:
                //
                //   1. 1097 is not this machine's UART line at all.  It is
                //      T8103's, inherited with the rest of this table exactly
                //      like the 0x235200000 base above -- MacBookAirMid2020Pkg,
                //      MacMini2020Pkg, MacBookProLate2020/2025Pkg and
                //      MacStudio2022Pkg all carry the identical 1097.  J813's
                //      real uart0 line is 1231 (ADT /arm-io/uart0 interrupts),
                //      and 1097 appears nowhere in J813's device tree.
                //
                //   2. 1231 would not be publishable either.  The GICv3 carrier
                //      registers SPIs [32, 1024) and the architecture caps SPI
                //      at 1019, so 1231 lands in the reserved 1024..4095 gap.
                //      The identical situation on XHC1 (GSIV 1274) was
                //      A/B-proven to produce CM_PROB_NORMAL_CONFLICT, and
                //      removing the Interrupt descriptor was what cleared it to
                //      problem=0 with resources assigned.  One unsatisfiable
                //      descriptor fails the whole requirement list.
                //
                // Aliasing it low (the mtp/ans treatment in m1n1's
                // src/hv_aic_alias.c) would also work and is NOT needed here:
                // AppleSerial does not want the interrupt.  TX is synchronous
                // polled and RX runs off its own poll timer; the driver accepts
                // zero interrupt descriptors and rejects only MORE than one
                // (AppleSerial.c, `interruptResources > 1`).  That half already
                // shipped -- the two changes are a matched pair.
                //
            })
            Method (_STA) {
                Return (0xF)
            }
        }

        //
        // T8101/T8103 *only* have 1 CPU die, ever, so everything in this node will comprise
        // most of the SoC.
        //
        Device(SOC) {
            Name(_HID, "ACPI0010") // all "processor containers" must have this HID
            Name(_UID, Zero) // unique identifier of the container

            //
            // E-core cluster, typically bootstrap core is here
            //
            //
            // Processor objects for T8142/M5: 6 E-cores (UID 0..5) then
            // 4 P-cores (UID 6..9).
            //
            // This block previously described M1's topology -- CLU0 holding
            // UIDs 0..3 and CLU1 holding 4..7 -- because it was inherited from
            // the M1 platform packages along with the rest of this table.  On a
            // ten-core part that is not merely inaccurate: UIDs 8 and 9 had no
            // ACPI0007 device at all, and UIDs 4 and 5 are E-cores described
            // inside the P-core container.
            //
            // Windows builds its per-processor power management state from the
            // MADT and expects to find a matching processor object for each
            // enabled entry.  The measured consequence of the missing ones was
            // a reproducible bugcheck 0xA -- IRQL_NOT_LESS_OR_EQUAL, a NULL
            // dereference at DISPATCH_LEVEL -- at ntoskrnl RVA 0x2C3D8C, inside
            // an internal Power Manager routine that sits immediately after
            // PoCpuIdledSinceLastCallImprecise in the export table and calls a
            // per-processor handler through a function pointer.
            //
            // UIDs here match MADT_Static.aslc and PPTT.aslc, which already use
            // E = 0..5 and P = 6..9.
            //
            // CPU0 reports _STA 0xF like every other core.  It briefly
            // reported 0 to match a MADT entry that marked E-core 0 not
            // enabled while m1n1 could not start it; both were restored
            // together once cpu0 came up.  They must always agree.
            //
            Device(CLU0) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x1) // unique identifier of the container

                Device(CPU0) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 0)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU1) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 1)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU2) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 2)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU3) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 3)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU4) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 4)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU5) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 5)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }

            //
            // P-core cluster.
            //
            Device(CLU1) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x2) // unique identifier of the container

                Device(CPU6) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 6)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU7) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 7)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU8) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 8)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU9) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 9)
                    Method (_LPI, 0, NotSerialized) {
                        Return (PLPI)
                    }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }
        }
    }
 }