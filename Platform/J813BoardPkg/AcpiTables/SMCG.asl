/*
 * Copyright (c) 2026 Aurora Silicon
 */

/** @file
  J813 SMC GPIO controller (NTAS0052).

  NOT REQUIRED FOR INPUT.  Keep reading before wiring anything to it.

  This device was added on the theory that the MTP multitouch endpoint asks the
  host to pulse two SMC-backed reset lines during its INIT sequence, as on
  J414s.  A full bring-up on this machine has since shown that theory to be
  wrong in both halves:

    - The IOP emits NO GPIO traffic at all.  Across a complete bring-up that
      ends with "Touch MT ready" it sends neither a GPIOInit block nor a
      GPIORequestMsg, so there is no reset for a host to service.
    - Multi-touch was brought fully online -- streaming HID frames -- with the
      SMC untouched.  What it actually needs is a CBOR firmware bootload and
      Apple's nine-byte power-method-2 exchange, neither of which involves the
      SMC.  The same conclusion was reached independently on T6050/J714s.

  The reset lines are real, they are just not the missing step.  For the record,
  and because an earlier version of this comment had the FourCCs wrong:

    /arm-io/dockchannel-mtp/mtp-transport/multi-touch  function-afe-reset
        phandle 217, function 'pKW4', args [1733308771, 65536]  -> "gP1c"
    /arm-io/dockchannel-mtp/mtp-transport/stm          function-stm-reset
        phandle 217, function 'pKW4', args [1733308772, 65536]  -> "gP1d"
    phandle 217 = /arm-io/smc/iop-smc-nub/smc-pmu

  gP1c and gP1d both exist on the live SMC (size 4, type 'ui32') and accept the
  0x10001/0x10000 pulse.  Doing so changes nothing observable.  gPW1/gPW2 --
  which an earlier reading of the ADT reported here -- are different numbers
  entirely (1733318449/1733318450) and are not SMC keys.

  The device is retained because it is correct as far as it goes and costs
  nothing: AppleSmcGpio binds to it, MTP.asl's connections resolve, and if a
  future INIT sequence does request a pulse the path exists.  It is not on the
  critical path for keyboard or trackpad.

  Addresses from DeviceTree.j813ap.adt (IPSW 25G76), arm-io relative
  +0x210000000:

    /arm-io/smc               reg[0] 0x38c600000/+0x88000  ASC wrap
                              mailbox at +0x8000 -> 0x38c608000
                              interrupts = <634 633 636 635>
    /arm-io/smc/iop-smc-nub   region-base 0x38de00000 region-size 0x120000

  The nub carveout is the SMC's SRAM, and it is the same region m1n1's
  rtkit_adopt_nub_carveout() already reads on this machine.

  No interrupt is published, deliberately -- same reasoning as J414s: the SMC
  transport is strictly polled, and connecting the level-triggered
  recv-not-empty line with nothing draining it is an interrupt storm.  All
  four mailbox lines are below 1019, so they would need no CSRT alias if a
  future interrupt-driven transport wants them.

  Copyright (c) Apple Silicon NT Drivers contributors
  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("SMCG.aml", "SSDT", 2, "NTASI ", "SMCGPIO ", 0x00000001)
{
    Scope (\_SB)
    {
        Device (SMCG)
        {
            Name (_HID, "NTAS0052")
            Name (_UID, Zero)
            Name (_CCA, Zero)

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Name (_CRS, ResourceTemplate ()
            {
                //
                // SMC ASC CPU block plus its mailbox at +0x8000.
                //
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x000000038C600000,
                    0x000000038C60BFFF,
                    0x0000000000000000,
                    0x000000000000C000
                    )
                //
                // SMC coprocessor SRAM.  Key values too large to ride inline
                // in the 64-bit command/result message are exchanged here.
                //
                QWordMemory (
                    ResourceConsumer,
                    PosDecode,
                    MinFixed,
                    MaxFixed,
                    NonCacheable,
                    ReadWrite,
                    0x0000000000000000,
                    0x000000038DE00000,
                    0x000000038DF1FFFF,
                    0x0000000000000000,
                    0x0000000000120000
                    )
            })

            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "ntasp,smc-asc-base", 0x000000038C600000 },
                    Package () { "ntasp,smc-asc-size", 0x4000 },
                    Package () { "ntasp,smc-mailbox-base", 0x000000038C608000 },
                    Package () { "ntasp,smc-mailbox-size", 0x4000 },
                    Package () { "ntasp,smc-sram-base", 0x000000038DE00000 },
                    Package () { "ntasp,smc-sram-size", 0x120000 },
                    Package () { "ntasp,smc-rtkit-endpoint", 0x20 },
                    Package () { "ntasp,smc-gpio-key-format", "gP%02x" },
                    Package () { "ntasp,smc-gpio-cmd-output", 0x01000000 },
                    Package () { "ntasp,smc-gpio-pin-afe-reset", 25 },
                    Package () { "ntasp,smc-gpio-pin-stm-reset", 26 },
                    Package () { "ntasp,preboot-owner", "iBoot" }
                }
            })
        }
    }
}
