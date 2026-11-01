/** @file
  J414s power / sleep button ACPI device.

  On Apple Silicon MacBooks the power button IS the Touch ID key, and it is
  delivered as an SMC RTKit notification (SMC_EV_BTN 0x7201, button 0x06 =
  BTN_TOUCHID), NOT by any ACPI-visible mechanism: the platform is
  hardware-reduced ACPI (no PM1 block, no PWRBTN_STS), and there is no
  PNP0C0C.  So this device carries NO hardware resources -- it is a plain
  companion devnode.  AppleSmcButtons (ACPI\NTAS0054) binds it and reaches
  the SMC through the mailbox owner's GUID_DEVINTERFACE_APPLE_SMC interface
  (exactly as AppleSmcBattery does), polling the firmware's `bHLD` key and
  raising a Windows HID System-Power-Down event; Windows then applies its own
  power-button policy (press -> sleep/menu, long hold -> shutdown).

  Empty _CRS by contract (no memory window, no interrupt): a second RTKit
  owner of the one SMC mailbox would corrupt the session, so the transport is
  the interface, never a claimed resource.  No FADT change is needed.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: MIT
**/

DefinitionBlock ("PBTN.aml", "SSDT", 0x02, "Apple", "J414PBTN", 0x00000001)
{
    Scope (\_SB)
    {
        Device (PBTN)
        {
            Name (_HID, "NTAS0054")
            Name (_UID, Zero)
            Name (_CRS, ResourceTemplate () { })
            Method (_STA)
            {
                Return (0x0F)
            }
        }
    }
}
