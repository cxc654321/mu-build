/** @file
 *MsPlatformDevicesLib  - Device specific library.

Copyright (C) Microsoft Corporation. All rights reserved.
 * Copyright (c) 2026 Aurora Silicon
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Protocol/DevicePath.h>

#include <Guid/SerialPortLibVendor.h>
#include <Guid/TtyTerm.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MsPlatformDevicesLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

typedef struct {
  VENDOR_DEVICE_PATH       DisplayDevicePath;
  EFI_DEVICE_PATH_PROTOCOL EndDevicePath;
} EFI_DISPLAY_DEVICE_PATH;

EFI_DISPLAY_DEVICE_PATH DisplayDevicePath =
{
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};
//
// Serial console over m1n1's VUART.
//
// This is the ONLY console *input* path on J704. Before this entry,
// gPlatformConsoles held the GOP display alone as CONSOLE_OUT | STD_ERROR, so
// ConIn never existed and no interactive application could be driven -- the UEFI
// Shell would paint a prompt on the panel with no way to type at it.
//
// A USB keyboard via UsbKbDxe is the other candidate, but it depends on XHCI
// enumeration, which currently hangs. This path does not.
//
// The three nodes must match exactly what the driver stack produces, or
// ConPlatformDxe silently never activates the console:
//
//   1. HW_VENDOR_DP / EDKII_SERIAL_PORT_LIB_VENDOR_GUID
//        the handle SerialDxe installs (see mSerialDevicePath in
//        MdeModulePkg/Universal/SerialDxe/SerialIo.c). SerialDxe wraps our
//        AppleUartSerialPortLib, which is already the SerialPortLib for this
//        platform.
//   2. MSG_UART_DP with the PcdUartDefault* values
//        SerialDxe fills these in from the same PCDs at init, so they are read
//        from the PCDs here rather than hardcoded.
//   3. MSG_VENDOR_DP / EFI_TTY_TERM_GUID
//        appended by TerminalDxe. Index 4 of its mTerminalType[] table, which is
//        what PcdDefaultTerminalType|4 selects in AppleSiliconPkg.dsc.inc. If
//        that PCD changes, this GUID must change with it.
//
// The byte path underneath is already known good: m1n1's hv_vuart.c serves URXH
// reads from the host CDC-ACM pipe and reflects pending input in UTRSTAT_RXD, and
// AppleUartSerialPortLib's SerialPortPoll()/SerialPortRead() test and read exactly
// those. Register offsets agree on both sides (UTRSTAT/UART_TRANSFER_STATUS 0x010,
// URXH/UART_RX_BYTE 0x024, RXD bit 0).
//
typedef struct {
  VENDOR_DEVICE_PATH       SerialDxe;
  UART_DEVICE_PATH         Uart;
  VENDOR_DEVICE_PATH       TerminalType;
  EFI_DEVICE_PATH_PROTOCOL EndDevicePath;
} EFI_SERIAL_CONSOLE_DEVICE_PATH;

EFI_SERIAL_CONSOLE_DEVICE_PATH SerialConsoleDevicePath =
{
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    EDKII_SERIAL_PORT_LIB_VENDOR_GUID
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_UART_DP,
      {
        (UINT8)(sizeof(UART_DEVICE_PATH)),
        (UINT8)((sizeof(UART_DEVICE_PATH)) >> 8)
      }
    },
    0,                                       // Reserved
    FixedPcdGet64 (PcdUartDefaultBaudRate),
    FixedPcdGet8  (PcdUartDefaultDataBits),
    FixedPcdGet8  (PcdUartDefaultParity),
    FixedPcdGet8  (PcdUartDefaultStopBits)
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_VENDOR_DP,
      {
        (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    EFI_TTY_TERM_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

//
// Predefined platform default console device path
//
BDS_CONSOLE_CONNECT_ENTRY gPlatformConsoles[] =
{
  {
    (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath,
    CONSOLE_OUT | STD_ERROR
  },
  {
    //
    // CONSOLE_OUT as well as CONSOLE_IN: ConSplitter then mirrors output to both
    // the panel and the VUART, so shell output lands in the captured log instead
    // of only on a screen we have to photograph.
    //
    (EFI_DEVICE_PATH_PROTOCOL *)&SerialConsoleDevicePath,
    CONSOLE_IN | CONSOLE_OUT | STD_ERROR
  },
  {
    NULL,
    0
  }
};

//
// Devices connected on the "ConIn is needed now" event, i.e. the lazy-ConIn path
// taken when PcdConInConnectOnDemand is TRUE. Returned by
// GetPlatformConnectOnConInList() below.
//
// This was {NULL} -- an empty list -- and that caused a boot loop. ConSplitter
// signals ConnectConInEvent on the first ReadKeyStroke
// (ConSplitter.c:3654), BdsDxeOnConnectConInCallBack walks this list, finds
// nothing, and BdsEntry.c:84 reports:
//
//     Connect ConIn in first ReadKeyStoke in Lazy ConIn mode.
//     Connect List = 0
//     [Bds] Connect ConIn failed - Not Found!!!
//
// after which the machine resets. Observed on hardware as: the UEFI Shell starts
// and paints, then the box reboots the instant the Shell asks for a key -- three
// full boots in one capture.
//
// The serial console is the one input device that always exists here, and its
// device path is already described above for gPlatformConsoles[]. Listing it here
// makes the lazy connect succeed.
//
// Note this is deliberately a fix *within* lazy-ConIn mode rather than turning
// PcdConInConnectOnDemand off. Setting that PCD FALSE also works in principle and
// additionally enables the BDS hotkey service (BdsEntry.c:783), but it swaps the
// narrow ConOut/ErrOut connect at BdsEntry.c:957 for a full
// EfiBootManagerConnectAllDefaultConsoles() that runs *before* boot options are
// enumerated -- a much larger change to console bring-up ordering. Revisit that
// separately once input is known good.
//
EFI_DEVICE_PATH_PROTOCOL *gPlatformConInDeviceList[] = {
  (EFI_DEVICE_PATH_PROTOCOL *)&SerialConsoleDevicePath,
  NULL
};

/**
Library function used to provide the platform SD Card device path
**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
GetSdCardDevicePath (
  VOID
  )
{
  return NULL;
}

/**
  Library function used to determine if the DevicePath is a valid bootable 'USB' device.
  USB here indicates the port connection type not the device protocol.
  With TBT or USB4 support PCIe storage devices are valid 'USB' boot options.
**/
BOOLEAN
EFIAPI
PlatformIsDevicePathUsb (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  return FALSE;
}

/**
Library function used to provide the list of platform devices that MUST be
connected at the beginning of BDS
**/
EFI_DEVICE_PATH_PROTOCOL **
EFIAPI
GetPlatformConnectList (
  VOID
  )
{
  return NULL;
}

/**
 * Library function used to provide the list of platform console devices.
 */
BDS_CONSOLE_CONNECT_ENTRY *
EFIAPI
GetPlatformConsoleList (
  VOID
  )
{
  return (BDS_CONSOLE_CONNECT_ENTRY *)&gPlatformConsoles;
}

/**
Library function used to provide the list of platform devices that MUST be connected
to support ConsoleIn activity.  This call occurs on the ConIn connect event, and
allows platforms to do enable specific devices ConsoleIn support.
**/
EFI_DEVICE_PATH_PROTOCOL **
EFIAPI
GetPlatformConnectOnConInList (
  VOID
  )
{
  //
  // This returned a bare NULL, which is what produced
  //
  //     Connect List = 0
  //     [Bds] Connect ConIn failed - Not Found!!!
  //
  // in MsPlatform.c:PlatformBootManagerOnDemandConInConnect(), followed by a
  // reset -- the boot loop seen on hardware where the Shell starts, paints, and
  // reboots the instant it asks for a key.
  //
  // Note the caller distinguishes "no list" from "empty list": it checks
  // PlatformConnectDeviceList != NULL before walking it, so returning NULL means
  // nothing is ever connected for ConIn. Populating gPlatformConInDeviceList[]
  // alone was not enough while this still returned NULL -- the array was inert.
  //
  return (EFI_DEVICE_PATH_PROTOCOL **)&gPlatformConInDeviceList;
}

/**
Library function used to provide the console type.  For ConType == DisplayPath,
device path is filled in to the exact controller to use.  For other ConTypes, DisplayPath
must NULL. The device path must NOT be freed.
**/
EFI_HANDLE
EFIAPI
GetPlatformPreferredConsole (
  OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePath
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                Handle = NULL;
  EFI_DEVICE_PATH_PROTOCOL *TempDevicePath;

  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath;

  Status = gBS->LocateDevicePath(
      &gEfiGraphicsOutputProtocolGuid, &TempDevicePath, &Handle);
  if (!EFI_ERROR(Status) && IsDevicePathEnd(TempDevicePath)) {
  }
  else {
    DEBUG(
        (DEBUG_ERROR,
         "%a - Unable to locate platform preferred console. Code=%r\n",
         __FUNCTION__, Status));
    Status = EFI_DEVICE_ERROR;
  }

  if (Handle != NULL) {
    //
    // Connect the GOP driver
    //
    gBS->ConnectController(Handle, NULL, NULL, TRUE);

    //
    // Get the GOP device path
    // NOTE: We may get a device path that contains Controller node in it.
    //
    TempDevicePath = EfiBootManagerGetGopDevicePath(Handle);
    *DevicePath    = TempDevicePath;
  }

  return Handle;
}
