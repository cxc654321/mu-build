/*
 * Copyright (c) 2026 Aurora Silicon
 */

/** @file
  J813 MTP/DockChannel input transport ACPI device.

  Port of the J414s table.  AppleMtpHid consumes the three DockChannel
  apertures, one firmware-staging memory window and one AIC interrupt from
  _CRS -- it reads no _DSD property, so the _CRS shape and ordering below are
  the actual contract and the _DSD package is documentation plus a cross-check
  surface.  Unlike the J414s table this one declares no GpioIo connections;
  see the note in _CRS for the hardware measurement that removed them.

  Addresses are from this machine's own device tree
  (DeviceTree.j813ap.adt, IPSW 25G76); ADT reg values under arm-io are
  arm-io relative and absolute is +0x210000000, verified against uart0
  (0x195200000 -> 0x3a5200000).

    /arm-io/dockchannel-mtp   reg[1] 0x394b14000  irq/status
                              reg[2] 0x394b30000  remote FIFO-1 config
                              reg[3] 0x394b34000  remote FIFO-1 data
                              interrupts = <1277>
    /arm-io/dart-mtp          interrupts = <1275>   (fault line)
    /arm-io/mtp               reg[0] 0x394600000/+0x88000  ASC wrap
                              mailbox at +0x8000 -> 0x394608000
    /arm-io/dart-mtp/mapper-mtp  reg = 0

  That last one is the entire reason this platform needed a port rather than
  a copy: mapper-mtp's "reg" is the DART stream id, and both /arm-io/mtp and
  /arm-io/dockchannel-mtp/mtp-transport name it via iommu-parent.  J414s uses
  stream 1; J813 uses stream 0.  m1n1's preboot handoff reads the same value
  and programs the same stream, so the two sides agree by construction.

  The fourth memory resource is the multitouch firmware staging window.
  Measured on J813: Windows does NOT apply AddressTranslation -- it maps the
  raw AddressMinimum 0x1800000, so the driver's writes land at that guest
  physical address.  The operative mechanism is therefore m1n1's, in two
  halves: the DART maps bus 0x1800000 -> 0x10020000000 (1 MiB) for the IOP,
  and the hypervisor identity-backs guest IPA 0x1800000 with the same
  carveout (mtp_handoff_map_guest_staging()), so the address the driver
  writes to and the bus address it sends in command 0x95 are the same
  number and both name the same memory.  _TRA is retained as documentation
  of the CPU-side carveout only: 0x10020000000 - 0x1800000 = 0x1001E800000.
  0x10020000000 is DRAM base + 512 MiB, below the Mu FD at 0x10030000000,
  and reserved from the UEFI memory map by the matching MemoryInitPeiLib
  overlay so Windows never allocates it.

  The LAST 4 KiB of the window (0x18FF000 / phys 0x100200FF000) is the
  AppleMtpHid telemetry page (drivers/AppleMtpHid/AppleMtpTelemetry.h); the
  driver caps the firmware payload at 0xFF000 so the two never overlap.
  Decode with tools/decode-apple-mtp-hid-telemetry.py.

  Copyright (c) Apple Silicon NT Drivers contributors
  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("MTP.aml", "SSDT", 0x02, "Apple", "J813MTP", 0x00000001)
{
    Scope (\_SB)
    {
        //
        // The built-in keyboard and trackpad are served by the MTP ASC over
        // DockChannel.  This is not the older AppleSPI trackpad topology.
        //
        Device (MTP0)
        {
            Name (_HID, "NTAS0050")
            Name (_UID, Zero)
            Name (_CCA, One)

            //
            // Keep the ordering in lockstep with AppleMtpEvtPrepareHardware:
            //   0: parent interrupt/status registers
            //   1: remote FIFO-1 configuration window
            //   2: remote FIFO-1 data window
            //   3: multitouch firmware staging window (raw = DART bus
            //      address for command 0x95, translated = reserved CPU
            //      carveout)
            //   4: physical AIC line 1277
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
                    0x0000000394B14000,
                    0x0000000394B14FFF,
                    0x0000000000000000,
                    0x0000000000001000
                    )
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000394B30000,
                    0x0000000394B30FFF,
                    0x0000000000000000,
                    0x0000000000001000
                    )
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000394B34000,
                    0x0000000394B34FFF,
                    0x0000000000000000,
                    0x0000000000001000
                    )
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x0000000001800000,
                    0x00000000018FFFFF,
                    0x000001001E800000,
                    0x0000000000100000
                    )
                //
                // Published GSIV, not the physical AIC line.
                //
                // The physical line is 1277 (ADT /arm-io/dockchannel-mtp
                // `interrupts`).  Publishing that directly does not work:
                // Windows' PnP interrupt arbiter only accepts GSIVs inside the
                // GIC architectural SPI range, and its live controller on this
                // machine reports lines=[32,1024) with "GSIV 1277: unmapped",
                // which parks the devnode at problem=12
                // (CM_PROB_NO_VALID_LOG_CONFIG) and never loads the driver.
                // 1020..1023 are architecturally special and 1024..4095 are
                // reserved, so the line has to be renumbered rather than the
                // range widened.
                //
                // m1n1 aliases published 995 <-> physical 1277 on T8142 while
                // the guest is on its emulated GICv3 carrier; see
                // src/hv_aic_alias.c, which is the authority for this pairing.
                // 995 was picked because it is free: no ADT node on this SoC
                // declares it, and the whole 990..999 block is unused.  J414s
                // publishes 37..46 for the same purpose, but 37 is taken here.
                //
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                    995
                }
                //
                // There are deliberately no GpioIo connections here.
                //
                // afe-reset and stm-reset used to be declared as two GpioIo
                // descriptors against \_SB.SMCG, purely so that the pins the
                // J414s driver knows about would resolve.  On Windows that is
                // not free: a GpioIo descriptor in _CRS is a hard PnP
                // dependency on the controller device, and \_SB.SMCG has no
                // driver and can never start.  Measured on hardware, the two
                // descriptors parked this device at
                //
                //     ACPI\NTAS0050\0 service=AppleMtpHid problem=51
                //
                // CM_PROB_WAITING_ON_DEPENDENCY -- bound to AppleMtpHid and
                // permanently waiting for a GPIO controller that will never
                // arrive, so AppleMtpHid.sys never loaded at all.
                //
                // Nothing is lost.  A full bring-up on this machine, up to and
                // including "Touch MT ready" with the trackpad streaming
                // frames, emits no GPIO traffic whatsoever -- the IOP never
                // asks for a reset pulse and the SMC is never touched.  What
                // multi-touch actually needs is the CBOR firmware bootload and
                // the nine-byte power-method-2 exchange described below.  The
                // ADT's real reset lines are the smc-pmu 'pKW4' keys gP1c and
                // gP1d, not the pins named here; both accept a pulse and
                // pulsing them changes nothing.  See SMCG.asl.
                //
                // AppleMtpEvtPrepareHardware agrees: it requires 3 or 4 memory
                // resources and exactly one interrupt, and never checks how
                // many GPIO connections it found.
            })

            //
            // None of these is a _CRS resource.  They describe the preboot
            // handoff m1n1 owns and must not become extra translated
            // resources for the Windows driver.
            //
            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "ntasp,dockchannel-fifo-index", One },
                    Package () { "ntasp,dockchannel-fifo-size", 0x800 },
                    Package () { "ntasp,mtp-asc-base", 0x0000000394600000 },
                    Package () { "ntasp,mtp-asc-size", 0x4000 },
                    Package () { "ntasp,mtp-mailbox-base", 0x0000000394608000 },
                    Package () { "ntasp,mtp-mailbox-size", 0x4000 },
                    Package () { "ntasp,mtp-dart-base", 0x0000000394800000 },
                    Package () { "ntasp,mtp-dart-size", 0x4000 },
                    Package () { "ntasp,mtp-dart-stream-id", Zero },
                    Package () { "ntasp,mtp-dart-fault-gsiv", 1275 },
                    Package () { "ntasp,mtp-afe-reset-gpio", 25 },
                    Package () { "ntasp,mtp-stm-reset-gpio", 26 },
                    Package () { "ntasp,mtp-firmware-name", "apple/tpmtfw-j813.bin" },
                    Package () { "ntasp,mtp-fw-staging-dva", 0x0000000001800000 },
                    Package () { "ntasp,mtp-fw-staging-phys", 0x0000010020000000 },
                    Package () { "ntasp,mtp-fw-staging-size", 0x100000 },
                    Package () { "ntasp,preboot-owner", "m1n1" },
                    Package () { "ntasp,preboot-handoff-required", One }
                }
            })

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
    }
}
