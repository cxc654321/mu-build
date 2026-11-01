/** @file
  Differentiated System Description Table for a machine whose board has not
  been measured.

  WHAT THIS DESCRIBES, AND WHY IT IS SHORT

  A board DSDT names the devices soldered to a particular logic board and the
  addresses and interrupt lines they answer on. Those numbers come from reading
  a live machine's ADT and correlating it with the alias table the AIC HAL
  extension uses. For every machine that has had that done, there is a board
  package carrying the result -- T602XBoardPkg for the T602X bring-up machine,
  T8142BoardPkg for T8142's.

  This is what a machine gets when that work has not been done for its board.
  It declares only what is true without measuring anything:

    * the PCIe root bridge, entirely from PCDs the SoC family package sets from
      Asahi's device tree
    * the processor container tree, included from the SoC family package

  It deliberately does NOT declare a keyboard, a trackpad, an audio complex, a
  camera, a display, an SMC or a USB host controller. Those all need a GPIO
  number, an interrupt line or a published-GSIV alias that nobody has read off
  this machine. Copying another machine's would produce firmware that looks
  measured and is not; a device that is absent from the DSDT is merely absent,
  which is the truth.

  What still works: the machine boots, enumerates PCIe, and attaches its
  internal NVMe -- AppleNANDStorageDxe takes every address from the live ADT --
  with serial through DBG2 and a framebuffer inherited from the bootloader.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

DefinitionBlock ("DSDT.aml", "DSDT", 0x02, "Apple", "GENBOARD", 0x00000001) {
    Scope (\_SB) {
        //
        // PCIe root bridge. Every number below is a PCD that
        // <SoC>FamilyPkg.dsc.inc sets from the apcie node's `reg` and `ranges`,
        // so this is the same description the hardware gives of itself.
        //
        // Declared only when the SoC family package actually found a root
        // complex. Asahi's tree describes none for T8132 (M4) -- its /soc has
        // fifteen children, none of them a pcie node -- so the PCDs are zero
        // there, and a root bridge whose window is zero-length is both a lie
        // and an iasl error. A machine without it boots and runs from its
        // internal NVMe, because AppleNANDStorageDxe reads the live ADT; what
        // it loses is PCIe enumeration, which there is nothing to enumerate
        // without an address for the config space.
        //
        // The test cannot be on the PCD itself: the ASL toolchain substitutes
        // PCD references after the preprocessor has already run, so a #if on
        // one is evaluated against the unsubstituted text and is always true.
        // NTASI_SOC_HAS_PCIE arrives through ASLPP_FLAGS, which is how
        // NTASI_ENABLE_XHC2 already reaches ASL.
        //
#if NTASI_SOC_HAS_PCIE
        Device (PCI0) {
            Name (_HID, EISAID ("PNP0A08"))
            Name (_CID, EISAID ("PNP0A03"))
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_UID, "PCI0")
            Name (_CCA, One)

            Method (_CBA, 0, NotSerialized) {
                Return (FixedPcdGet64 (PcdPciExpressBaseAddress))
            }

            Name (_CRS, ResourceTemplate () {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0,
                    FixedPcdGet32 (PcdPciBusMin),
                    FixedPcdGet32 (PcdPciBusMax),
                    0,
                    FixedPcdGet32 (PcdPciBusCount))

                //
                // 32-bit window, not prefetchable. Translation is what gets
                // added to a bus address to reach the CPU-physical one, which
                // is exactly the AddressTranslation field.
                //
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite, 0,
                    FixedPcdGet64 (PcdPciMmio32Base),
                    FixedPcdGet64 (PcdPciMmio32Limit),
                    FixedPcdGet64 (PcdPciMmio32Translation),
                    FixedPcdGet64 (PcdPciMmio32Size))

                //
                // 64-bit window, prefetchable, mapped 1:1.
                //
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Prefetchable, ReadWrite, 0,
                    FixedPcdGet64 (PcdPciMmio64Base),
                    FixedPcdGet64 (PcdPciMmio64Limit),
                    FixedPcdGet64 (PcdPciMmio64Translation),
                    FixedPcdGet64 (PcdPciMmio64Size))
            })

            Method (_STA) {
                Return (0xF)
            }
        }
#endif

        //
        // The processor containers are not here. They are the die's topology,
        // not the board's, so the SoC family package emits them as its own SSDT
        // (Cpus.asl, generated by Tools/add-soc.py). That is what lets this one
        // DSDT serve machines built on different silicon.
        //
    }
}
