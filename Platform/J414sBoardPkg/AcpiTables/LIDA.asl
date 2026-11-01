/** @file
  J414s lid angle sensor ACPI device.

  The MacBook Pro 14" (M2 Pro, t6020) hinge angle is an AOP (always-on
  processor) EPIC service that announces itself as "las".  AppleLidAngle
  (ACPI\NTAS0082) brings up the AOP over RTKit + AFK, subscribes to the EPIC
  service, decodes the angle byte, and republishes it as a Windows HID
  hinge-angle sensor via VHF.

  The driver consumes EXACTLY three memory windows, in this order (it rejects
  a short or long list):

    0: AOP ASC block, 0x2A6400000 + 0x6C000  (RTKit mailbox at +0x8000)
    1: AOP SRAM,      0x2A6C00000 + 0x250000
    2: AOP DART (t8110), 0x2A6808000 + 0x4000

  These are the AOP audio device's windows 0-2 (AOPA / NTAS0081) with the
  ADMAC audio-ring window omitted -- the lid sensor has no audio ring.  No
  interrupt: the driver is fully polled.

  MUTUAL EXCLUSION: NTAS0081 (AOP audio) and NTAS0082 (this device) drive the
  SAME AOP mailbox, so at most one may be published in a given firmware build.
  The feature sets this ships in do NOT publish AOP
  audio, so LIDA owns the AOP here; it must NEVER be combined with the
  aop feature (which publishes NTAS0081).

  Provenance:
    Asahi linux-asahi 030248d39b401c94695c9f7df2fed630d35120cd:
      drivers/soc/apple/aop.rs (EPIC/fakehid split),
      drivers/iio/common/aop_sensors/aop_las.rs ("apple,aop-las", angle byte).
    AOP MMIO windows from AOPA.asl (windows 0-2).

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("LIDA.aml", "SSDT", 0x02, "Apple", "J414LIDA", 0x00000001)
{
    Scope (\_SB)
    {
        Device (LIDA)
        {
            Name (_HID, "NTAS0082")
            Name (_UID, Zero)
            Name (_CCA, One)

            //
            // Order is load-bearing: AppleLidAngle requires window 0 = ASC,
            // 1 = SRAM, 2 = DART.
            //
            Name (_CRS, ResourceTemplate ()
            {
                QWordMemory (
                    ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,
                    0x00000002A6400000,
                    0x00000002A646BFFF,
                    0x0000000000000000,
                    0x000000000006C000
                    )
                QWordMemory (
                    ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,
                    0x00000002A6C00000,
                    0x00000002A6E4FFFF,
                    0x0000000000000000,
                    0x0000000000250000
                    )
                QWordMemory (
                    ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,
                    0x00000002A6808000,
                    0x00000002A680BFFF,
                    0x0000000000000000,
                    0x0000000000004000
                    )
            })

            Method (_STA)
            {
                Return (0x0F)
            }
        }
    }
}
