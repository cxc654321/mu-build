/** @file
 *DeviceBootManager  - Ms Device specific extensions to BdsDxe.

 Based on version from SurfaceDuoPkg

Copyright (C) Microsoft Corporation. All rights reserved.
 * Copyright (c) 2026 Aurora Silicon
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <DfciSystemSettingTypes.h>

#include <Guid/EventGroup.h>
#include <Guid/GlobalVariable.h>
#include <Guid/MsBootMenuGuid.h>
#include <Guid/DfciPacketHeader.h>

#include <Protocol/OnScreenKeyboard.h>
#include <Protocol/TpmPpProtocol.h>

#include <Library/BaseMemoryLib.h>
#include <Library/ConsoleMsgLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MsBootManagerSettingsLib.h>
#include <Library/MsBootOptionsLib.h>
//#include <Library/SdBootOptionsLib.h>
#include <Library/MsBootPolicyLib.h>
//#include <Library/SdBootPolicyLib.h>
#include <Library/BootGraphicsLib.h>
#include <Library/GraphicsConsoleHelperLib.h>
#include <Library/MsPlatformDevicesLib.h>
#include <Library/MsPlatformPowerCheckLib.h>
#include <Library/MsNetworkDependencyLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/PrintLib.h>
#include <Library/PowerServicesLib.h>
#include <Library/ThermalServicesLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MsNVBootReasonLib.h>
#include <Library/MuTelemetryHelperLib.h>
#include <Library/VariablePolicyHelperLib.h>
#include <Library/UpdateFacsHardwareSignatureLib.h>

#include <Settings/BootMenuSettings.h>
#include <Settings/DfciSettings.h>

static EFI_EVENT  mPreReadyToBootEvent;
static EFI_EVENT  mPostReadyToBootEvent;

CHAR8  mMemoryType[][30] = {
  // Value for PcdMemoryMapTypes
  "EfiReservedMemoryType      ",                               // 0x0001
  "EfiLoaderCode              ",                               // 0x0002
  "EfiLoaderData              ",                               // 0x0004
  "EfiBootServicesCode        ",                               // 0x0008
  "EfiBootServicesData        ",                               // 0x0010
  "EfiRuntimeServicesCode     ",                               // 0x0020
  "EfiRuntimeServicesData     ",                               // 0x0040
  "EfiConventionalMemory      ",                               // 0x0080
  "EfiUnusableMemory          ",                               // 0x0100
  "EfiACPIReclaimMemory       ",                               // 0x0200   Both ACPI types would
  "EfiACPIMemoryNVS           ",                               // 0x0400   be 0x0600
  "EfiMemoryMappedIO          ",                               // 0x0800
  "EfiMemoryMappedIOPortSpace ",                               // 0x1000
  "EfiPalCode                 ",                               // 0x2000
  "EfiMaxMemoryType           "
};

static
VOID
ThermalFailureShutdown (
  VOID
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  EFI_EVENT   TimerEvent; // Should set to a value
  UINTN       index;
  UINT32      WaitTime = PcdGet32 (PcdShutdownGraphicDisplayTime);

  // Display the too hot picture
  Status = DisplayBootGraphic (BG_CRITICAL_OVER_TEMP);
  if (EFI_ERROR (Status) != FALSE) {
    DEBUG ((DEBUG_ERROR, "%a Unabled to set graphics - %r\n", __FUNCTION__, Status));
  }

  // Wait a few seconds

  // Create an event
  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Create Event failed. %r\n", Status));
  }

  // Set timer using the event
  gBS->SetTimer (TimerEvent, TimerRelative, WaitTime);

  // Wait for event to fire
  Status = gBS->WaitForEvent (1, &TimerEvent, &index);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Wait for Event failed. %r\n", Status));
  }

  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

static
VOID
PowerFailureShutdown (
  VOID
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  EFI_EVENT   TimerEvent; // Should set to a value
  UINTN       index;
  UINT32      WaitTime = PcdGet32 (PcdShutdownGraphicDisplayTime);

  // Display the low battery picture

  DEBUG ((DEBUG_INFO, "Fetch the display picture.\n"));

  Status = DisplayBootGraphic (BG_CRITICAL_LOW_BATTERY);
  if (EFI_ERROR (Status) != FALSE) {
    DEBUG ((DEBUG_ERROR, "%a Unabled to set graphics - %r\n", __FUNCTION__, Status));
  }

  // Wait a few seconds
  // Create an event
  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Create Event failed. %r\n", Status));
  }

  // Set timer using the event
  gBS->SetTimer (TimerEvent, TimerRelative, WaitTime);

  // Wait for event to fire
  Status = gBS->WaitForEvent (1, &TimerEvent, &index);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Wait for Event failed. %r\n", Status));
  }

  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

static
EFI_STATUS
MsPreBootChecks (
  VOID
  )
{
  EFI_STATUS  Status      = EFI_SUCCESS;
  EFI_STATUS  WaitStatus  = EFI_SUCCESS;
  BOOLEAN     ThermalGood = TRUE;
  BOOLEAN     PowerGood   = TRUE;
  INT32       RetryCount  = PcdGet32 (PcdPowerAndThermalRetries);

  EFI_EVENT  TimerEvent = NULL;
  UINTN      index;
  UINT32     WaitTime = PcdGet32 (PcdThermalControlRetryWait);

  UINT32  ThermalFailureCount = 1;

  // Create an event

  DEBUG ((DEBUG_INFO, "Inside MsPrebootchecks\n"));

  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Create Event failed. %r\n", Status));
    goto CleanUp;
  }

  do {
    // Check to see if the Power situation is good

    DEBUG ((DEBUG_INFO, "SystemPowerCheck\n"));

    Status = SystemPowerCheck (PowerCaseBoot, &PowerGood);

    DEBUG ((DEBUG_INFO, "SystemPowerCheck %r\n", Status));

    if ((!EFI_ERROR (Status)) && (!PowerGood)) {
      DEBUG ((DEBUG_INFO, "SystemPowerMitigate(Boot)\n"));

      Status = SystemPowerMitigate (PowerCaseBoot);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_INFO, "SystemPowerMitigate(Boot) failed - %r.  Shutdown now\n", Status));

        PowerGood = FALSE;
        goto CleanUp;
      }

      // There should be enough power to boot so fall through to next test.
      // Thermal mitigation may burn battery so we will check power once
      // more before booting.
    } else {
      // change - error retrieving power state should not stop boot
      PowerGood = TRUE;
    }

    // Check to see if the thermal situation is good

    Status = SystemThermalCheck (ThermalCaseBoot, &ThermalGood);

    if ((!EFI_ERROR (Status)) && (!ThermalGood)) {
      if (1 == ThermalFailureCount) {
        // Set active cooling event

        DEBUG ((DEBUG_WARN, "MsPreBootChecks: Thermal mitgation has been started\n"));
      }

      if (2 == ThermalFailureCount) {
        // Set passive cooling event
      }

      if (ThermalFailureCount < 3) {
        ThermalFailureCount++;
      }

      // Wait for cooling to have an effect but not so long we completely
      // drain the battery. ToDo: should consider adding some UI to let the
      // user know what is going on.

      if (RetryCount > 0) {
        // Set timer using the event
        gBS->SetTimer (TimerEvent, TimerRelative, WaitTime);

        // Wait for event to fire
        WaitStatus = gBS->WaitForEvent (1, &TimerEvent, &index);
        if (EFI_ERROR (WaitStatus)) {
          DEBUG ((DEBUG_ERROR, "MsPreBootChecks: WaitForEvent failed. %r\n", Status));
        }
      }
    } else {
      // change - error retrieving thermal should not stop boot
      ThermalGood = TRUE;
    }
  } while ((RetryCount-- > 0) && ((!ThermalGood) || (!PowerGood)));

CleanUp:

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  if (!ThermalGood) {
    DEBUG ((DEBUG_ERROR, "MsPreBootChecks failed when calling Thermal Good function. %r\n", Status));
    LogTelemetry (TRUE, NULL, EFI_COMPUTING_UNIT_HOST_PROCESSOR | EFI_CU_HP_EC_THERMAL, NULL, NULL, 0, 0);
    ThermalFailureShutdown ();    // Should never return from this function
  }

  if (!PowerGood) {
    DEBUG ((DEBUG_ERROR, "MsPreBootChecks failed when calling Power Good function. %r\n", Status));
    LogTelemetry (TRUE, NULL, EFI_COMPUTING_UNIT_HOST_PROCESSOR | EFI_CU_HP_EC_LOW_VOLTAGE, NULL, NULL, 0, 0);
    PowerFailureShutdown ();    // Should never return from this function
  }

  return Status;
}

/**
  Lock the required boot variables if LockBootOrder is enabled
*/
static
VOID
BdsBootLockBootVariables (
  VOID
  )
{
  EFI_STATUS                      Status;
  BOOLEAN                         EnableBootOrderLock = FALSE;
  EDKII_VARIABLE_POLICY_PROTOCOL  *VarPolicyProtocol  = NULL;
  UINT16                          *BootOrder;
  UINTN                           BootOrderSize;
  CHAR16                          OptionName[sizeof ("Boot####")];
  UINTN                           i;
  static BOOLEAN                  AlreadyLocked = FALSE;

  if (AlreadyLocked) {
    // This can happen as we may call ready to boot a number of times;
    return;
  }

  Status = GetBootManagerSetting (
             DFCI_SETTING_ID__BOOT_ORDER_LOCK,
             &EnableBootOrderLock
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a Unable to get BootOrderLock setting\n", __FUNCTION__));
    return;
  }

  if (!EnableBootOrderLock) {
    DEBUG ((DEBUG_INFO, "%a - BootOrder is not locked\n", __FUNCTION__));
    return;
  }

  Status = gBS->LocateProtocol (&gEdkiiVariablePolicyProtocolGuid, NULL, (VOID **)&VarPolicyProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a - Failed to locate var policy protocol (%r).  Can't lock variables\n", __FUNCTION__, Status));
    return;
  }

  Status = RegisterBasicVariablePolicy (
             VarPolicyProtocol,
             &gEfiGlobalVariableGuid,
             EFI_BOOT_ORDER_VARIABLE_NAME,
             VARIABLE_POLICY_NO_MIN_SIZE,
             VARIABLE_POLICY_NO_MAX_SIZE,
             VARIABLE_POLICY_NO_MUST_ATTR,
             VARIABLE_POLICY_NO_CANT_ATTR,
             VARIABLE_POLICY_TYPE_LOCK_NOW
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Unable to lock BootOrder. Code=%r\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "Variable BootOrder locked\n"));
  }

  // Delete BootNext as a locked BootNext is a bad thing
  Status = gRT->SetVariable (
                  EFI_BOOT_NEXT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  0,
                  0,
                  NULL
                  );
  DEBUG ((DEBUG_INFO, "Status from deleting BootNext prior to lock. Code=%r\n", Status));

  Status = RegisterBasicVariablePolicy (
             VarPolicyProtocol,
             &gEfiGlobalVariableGuid,
             EFI_BOOT_NEXT_VARIABLE_NAME,
             VARIABLE_POLICY_NO_MIN_SIZE,
             VARIABLE_POLICY_NO_MAX_SIZE,
             VARIABLE_POLICY_NO_MUST_ATTR,
             VARIABLE_POLICY_NO_CANT_ATTR,
             VARIABLE_POLICY_TYPE_LOCK_NOW
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Unable to lock BootNext. Code=%r\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "Variable BootNext locked\n"));
  }

  GetVariable2 (
    L"BootOrder",
    &gEfiGlobalVariableGuid,
    (VOID **)&BootOrder,
    &BootOrderSize
    );
  if (BootOrder == NULL) {
    DEBUG ((DEBUG_ERROR, "%a - Failed to locate BootOrder (%r).  Can't lock Boot####\n", __FUNCTION__, Status));
    return;
  }

  for (i = 0; i < (BootOrderSize / sizeof (UINT16)); i++) {
    UnicodeSPrint (OptionName, sizeof (OptionName), L"Boot%04x", BootOrder[i]);
    Status = RegisterBasicVariablePolicy (
               VarPolicyProtocol,
               &gEfiGlobalVariableGuid,
               OptionName,
               VARIABLE_POLICY_NO_MIN_SIZE,
               VARIABLE_POLICY_NO_MAX_SIZE,
               VARIABLE_POLICY_NO_MUST_ATTR,
               VARIABLE_POLICY_NO_CANT_ATTR,
               VARIABLE_POLICY_TYPE_LOCK_NOW
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Unable to lock %s. Code=%r\n", OptionName, Status));
    } else {
      DEBUG ((DEBUG_INFO, "Variable %s locked\n", OptionName));
    }
  }
}

/**
 * Enable the On Screen Keyboard
 *
 *
 * @return VOID
 */
static
VOID
EnableOSK (
  VOID
  )
{
  EFI_STATUS                         Status;
  MS_ONSCREEN_KEYBOARD_PROTOCOL      *OSKProtocol;
  MS_SIMPLE_WINDOW_MANAGER_PROTOCOL  *SWMProtocol;

  //
  // Enable on-screen keyboard icon auto-display mode.  This will cause the keyboard
  // icon to be presented when an application (launched by BDS) waits on the keyboards
  // Simple Text In (or Extended) event.
  //

  // Locate the on-screen keyboard (OSK) protocol
  //
  Status = gBS->LocateProtocol (
                  &gMsOSKProtocolGuid,
                  NULL,
                  (VOID **)&OSKProtocol
                  );

  if (EFI_ERROR (Status)) {
    OSKProtocol = NULL;
    DEBUG ((DEBUG_WARN, "WARN: Failed to find the on-screen keyboard protocol (%r).\r\n", Status));
  } else {
    // Configure the OSK size, position, and state (75% of screen width, bottom right
    // position, docked) - this is most compatible with the Windows 10 English-localized
    // Bitlocker PIN screen.  The keyboard is not displayed by default.
    //
    OSKProtocol->ShowKeyboard (OSKProtocol, FALSE);
    OSKProtocol->SetKeyboardSize (OSKProtocol, 75);
    OSKProtocol->SetKeyboardPosition (OSKProtocol, BottomRight, Docked);
    OSKProtocol->ShowDockAndCloseButtons (OSKProtocol, TRUE);

    // Configure the OSK icon position (used to launch on-screen keyboard).  The keyboard
    // icon is not displayed by default.
    //
    OSKProtocol->ShowKeyboardIcon (OSKProtocol, FALSE);
    OSKProtocol->SetKeyboardIconPosition (OSKProtocol, BottomRight);

    // Configure the OSK icon for auto-enable and self-refresh modes.
    //
    OSKProtocol->SetKeyboardMode (OSKProtocol, (OSK_MODE_AUTOENABLEICON | OSK_MODE_SELF_REFRESH));
  }

  //
  // Disable the mouse pointer.  This allows a 3rd party application to render the pointer themselves
  // if they wish.  If the on-screen keyboard auto activates or if we launch our own application that
  // requires mouse pointer (ex: FrontPage), they will re-enable our mouse pointer.
  //

  // Locate the Simple Window Manager (SWM) protocol
  //
  Status = gBS->LocateProtocol (
                  &gMsSWMProtocolGuid,
                  NULL,
                  (VOID **)&SWMProtocol
                  );

  if (EFI_ERROR (Status)) {
    SWMProtocol = NULL;
    DEBUG ((DEBUG_WARN, "WARN: Failed to find the Simple Window Manager protocol (%r).\r\n", Status));
  } else {
    // Disable the mouse pointer.
    //
    SWMProtocol->EnableMousePointer (
                   SWMProtocol,
                   FALSE
                   );
  }
}

/**
 * Print Memory Map
 *
 * @return VOID
 */
static
VOID
PrintMemoryMap (
  )
{
  if (PcdGet8 (PcdEnableMemMapOutput)) {
    EFI_STATUS             Status;
    UINTN                  MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR  *MemoryMap    = NULL;
    VOID                   *p;
    CHAR8                  *Entry;
    UINTN                  MapKey;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
    UINTN                  Count;
    UINTN                  i;

    Status = gBS->GetMemoryMap (&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      MemoryMap = AllocatePool (MemoryMapSize + (sizeof (EFI_MEMORY_DESCRIPTOR) * 2));
      p         = (VOID *)MemoryMap; // Save for FreePool
      if (MemoryMap != NULL) {
        Status = gBS->GetMemoryMap (&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        Entry  = (CHAR8 *)MemoryMap;
        if (Status == EFI_SUCCESS) {
          Count = MemoryMapSize / DescriptorSize;
          for (i = 0; i < Count; i++) {
            MemoryMap = (EFI_MEMORY_DESCRIPTOR *)Entry;
            if (MemoryMap->Type <= EfiMaxMemoryType) {
              if (((1 << MemoryMap->Type) & PcdGet32 (PcdEnableMemMapTypes)) != 0) {
                DEBUG ((DEBUG_INFO, "%a at %p for %d pages\n", mMemoryType[MemoryMap->Type], MemoryMap->PhysicalStart, MemoryMap->NumberOfPages));
                if (PcdGet8 (PcdEnableMemMapDumpOutput)) {
                  DUMP_HEX (DEBUG_INFO, 0, (CHAR8 *)MemoryMap->PhysicalStart, 48, "");
                }
              }
            } else {
              DEBUG ((DEBUG_ERROR, "Invalid memory type - %x\n", MemoryMap->Type));
            }

            Entry += DescriptorSize;
          }
        }

        FreePool (p);
      }
    }
  }
}

/**
  Check if current BootCurrent variable is internal shell boot option.

  @retval  TRUE         BootCurrent is internal shell.
  @retval  FALSE        BootCurrent is not internal shell.
**/
static
BOOLEAN
BootCurrentIsInternalShell (
  VOID
  )
{
  UINTN                     VarSize;
  UINT16                    BootCurrent;
  CHAR16                    BootOptionName[16];
  UINT8                     *BootOption;
  UINT8                     *Ptr;
  BOOLEAN                   Result;
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *TempDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *LastDeviceNode;
  EFI_GUID                  *GuidPoint;

  BootOption = NULL;
  Result     = FALSE;

  //
  // Get BootCurrent variable
  //
  VarSize = sizeof (UINT16);
  Status  = gRT->GetVariable (
                   L"BootCurrent",
                   &gEfiGlobalVariableGuid,
                   NULL,
                   &VarSize,
                   &BootCurrent
                   );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  //
  // Create boot option Bootxxxx from BootCurrent
  //
  UnicodeSPrint (BootOptionName, sizeof (BootOptionName), L"Boot%04x", BootCurrent);

  GetEfiGlobalVariable2 (BootOptionName, (VOID **)&BootOption, &VarSize);

  if ((BootOption == NULL) || (VarSize == 0)) {
    return FALSE;
  }

  Ptr            = BootOption;
  Ptr           += sizeof (UINT32);
  Ptr           += sizeof (UINT16);
  Ptr           += StrSize ((CHAR16 *)Ptr);
  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Ptr;
  LastDeviceNode = TempDevicePath;
  while (!IsDevicePathEnd (TempDevicePath)) {
    LastDeviceNode = TempDevicePath;
    TempDevicePath = NextDevicePathNode (TempDevicePath);
  }

  GuidPoint = EfiGetNameGuidFromFwVolDevicePathNode (
                (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)LastDeviceNode
                );
  if ((GuidPoint != NULL) &&
      ((CompareGuid (GuidPoint, PcdGetPtr (PcdShellFile))) ||
       (CompareGuid (GuidPoint, &gUefiShellFileGuid)))
      )
  {
    //
    // if this option is internal shell, return TRUE
    //
    Result = TRUE;
  }

  if (BootOption != NULL) {
    FreePool (BootOption);
    BootOption = NULL;
  }

  return Result;
}

/**
Pre ready to boot callback to lock bds variables.

@param  Event                 Event whose notification function is being invoked.
@param  Context               The pointer to the notification function's context,
which is implementation-dependent.
**/
static
VOID
EFIAPI
PreReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  BdsBootLockBootVariables ();
  EnableOSK ();

  gBS->CloseEvent (Event);
  return;
}

/**
Post ready to boot callback to print memory map, and update FACS hardware signature.
For booting the internal shell, set the video resolution to low.

@param  Event                 Event whose notification function is being invoked.
@param  Context               The pointer to the notification function's context,
which is implementation-dependent.
**/
static
VOID
EFIAPI
PostReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  BOOLEAN         StartNetworkStack = FALSE;
  EFI_STATUS      Status;
  static BOOLEAN  FirstPass = TRUE;

  if (BootCurrentIsInternalShell ()) {
    //
    // This ConnectAll is what stalls J704 (attempt 30/31). BDS has already
    // committed to booting the Shell and signalled ReadyToBoot by the time we get
    // here, so binding every driver to every handle walks into
    // XhciDxe -> UsbBusDxe -> UsbRootHubInit and never comes back -- the Shell
    // image is never started, and the log stops mid-ReadyToBoot looking as though
    // BDS never attempted a boot option at all.
    //
    // Skipping it costs the Shell a populated mapping table (it sees only what is
    // already connected) and buys a live console to debug USB from: `connect -r`
    // at the Shell prompt reproduces the stall interactively.
    //
    if (PcdGetBool (PcdAppleConnectAllForInternalShell)) {
      EfiBootManagerConnectAll ();
    } else {
      DEBUG ((
        DEBUG_INFO,
        "%a: skipping ConnectAll for internal Shell "
        "(PcdAppleConnectAllForInternalShell=FALSE)\n",
        __FUNCTION__
        ));
    }

    if (PcdGetBool (PcdLowResolutionInternalShell)) {
      Status = SetGraphicsConsoleMode (GCM_LOW_RES);
      if (EFI_ERROR (Status) != FALSE) {
        DEBUG ((DEBUG_ERROR, "%a Unable to set console mode - %r\n", __FUNCTION__, Status));
      }
    }
  }

  if (FirstPass) {
    FirstPass = FALSE;
    Status    = GetBootManagerSetting (
                  DFCI_SETTING_ID__START_NETWORK,
                  &StartNetworkStack
                  );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a Unable to get Start Network setting\n", __FUNCTION__));
    } else {
      if (StartNetworkStack) {
        DEBUG ((DEBUG_INFO, "%a - Starting the network stack\n", __FUNCTION__));
        // This will unblock the network stack.
        StartNetworking ();

        // ConnectAll - Convert to ConnetLess in phase 2 (Work Item 1544)
        EfiBootManagerConnectAll ();
      }
    }

    PrintMemoryMap ();
    Status = UpdateFacsHardwareSignature (DefaultFacsHwSigAlgorithm);
  }

  return;
}

/**
 * Constructor   - This runs when BdsDxe is loaded, before BdsArch protocol is published
 *
 * @return EFI_STATUS
 */
EFI_STATUS
EFIAPI
DeviceBootManagerConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  // Install Pre-ReadyToBoot callback to note when the variables need to be locked
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  PreReadyToBoot,
                  NULL,
                  &gEfiEventPreReadyToBootGuid,
                  &mPreReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ERROR [BDS]: Failed to register OnReadyToBoot (%r).\r\n", Status));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  PostReadyToBoot,
                  NULL,
                  &gEfiEventAfterReadyToBootGuid,
                  &mPostReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ERROR [BDS]: Failed to register OnReadyToBoot (%r).\r\n", Status));
  }

  // Constructor MUST return EFI_SUCCESS as a failure can result in an unusable system
  return EFI_SUCCESS;
}

/**
  OnDemandConInConnect
 */
EFI_DEVICE_PATH_PROTOCOL **
EFIAPI
DeviceBootManagerOnDemandConInConnect (
  VOID
  )
{
  return GetPlatformConnectOnConInList ();
}

/**
  Do the device specific action at start of BdsEntry (callback into BdsArch from DXE Dispatcher)
**/
VOID
EFIAPI
DeviceBootManagerBdsEntry (
  VOID
  )
{
  EfiEventGroupSignal (&gMsStartOfBdsNotifyGuid);

  //
  // Also signal the DFCI version of this event.
  //
  EfiEventGroupSignal (&gDfciStartOfBdsNotifyGuid);

  UpdateRebootReason ();
}

//
// Byte-for-byte copy of MsCorePkg's mUsbClassKeyboardDevicePath
// (Common/MU/MsCorePkg/Library/PlatformBootManagerLib/BdsPlatform.c:13). Its
// declaration lives in that library's private BdsPlatform.h, so it cannot be
// included from here -- the layout is duplicated instead. Keep the two in step:
// EfiBootManagerUpdateConsoleVariable matches on the device path bytes, so any
// divergence silently turns the removal below into a no-op.
//
#define APPLE_USB_CLASS_HID          3
#define APPLE_USB_SUBCLASS_BOOT      1
#define APPLE_USB_PROTOCOL_KEYBOARD  1

typedef struct {
  USB_CLASS_DEVICE_PATH       UsbClass;
  EFI_DEVICE_PATH_PROTOCOL    End;
} APPLE_USB_CLASS_FORMAT_DEVICE_PATH;

STATIC APPLE_USB_CLASS_FORMAT_DEVICE_PATH  mUsbClassKeyboardDevicePath = {
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_USB_CLASS_DP,
      {
        (UINT8)(sizeof (USB_CLASS_DEVICE_PATH)),
        (UINT8)((sizeof (USB_CLASS_DEVICE_PATH)) >> 8)
      }
    },
    0xffff,                      // VendorId  (any)
    0xffff,                      // ProductId (any)
    APPLE_USB_CLASS_HID,
    APPLE_USB_SUBCLASS_BOOT,
    APPLE_USB_PROTOCOL_KEYBOARD
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { END_DEVICE_PATH_LENGTH, 0 }
  }
};

/**
  Drop the USB class keyboard short-form path from ConIn.

  PlatformBootManagerBeforeConsole appends it unconditionally, a few lines
  before it calls DeviceBootManagerBeforeConsole, so this runs late enough to
  undo it and early enough that nothing has tried to connect ConIn yet.

  On J704 the only keyboard is internal (MTP over dockchannel, no UEFI driver
  yet), so the path can never resolve. What it does instead is make the console
  depend on USB: connecting ConIn walks NonDiscoverablePciDeviceStart ->
  XhciDxe -> UsbBusDxe -> UsbRootHubInit, which currently stalls, and BDS never
  reaches a boot option. Removing it leaves XhciDxe registered and bindable --
  USB media can still be connected explicitly from the Shell with `connect -r`.
**/
STATIC
VOID
RemoveUsbKeyboardFromConIn (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = EfiBootManagerUpdateConsoleVariable (
             ConIn,
             NULL,
             (EFI_DEVICE_PATH_PROTOCOL *)&mUsbClassKeyboardDevicePath
             );

  DEBUG ((
    DEBUG_INFO,
    "%a: dropped USB keyboard from ConIn - %r\n",
    __FUNCTION__,
    Status
    ));
}

/**
  Dump one console variable as text, and report whether each instance in it can
  actually be connected.

  Added for attempt 33. FW-4 reached the Shell but no keystroke ever arrived, and
  the VUART never received the Shell's *ConOut* output either, while the raw
  DEBUG() stream over the same UART kept working perfectly. That combination says
  the serial console's device path is not resolving to a handle, so neither
  EfiBootManagerConnectConsoleVariable (ConOut) at BdsEntry.c:957 nor the ConIn
  equivalent at BdsEntry.c:77 can tag it -- and BmConsole.c *deletes* instances it
  fails to connect, so the evidence erases itself.

  Printing the variable and the per-instance status is the cheapest way to see
  which of those it is, rather than inferring it from absence.
**/
STATIC
VOID
DumpConsoleVariable (
  IN CONSOLE_TYPE  ConsoleType,
  IN CHAR16        *VarName
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Walk;
  EFI_DEVICE_PATH_PROTOCOL  *Instance;
  EFI_STATUS                Status;
  CHAR16                    *Text;
  UINTN                     Size;
  UINTN                     Index;

  DevicePath = NULL;
  GetEfiGlobalVariable2 (VarName, (VOID **)&DevicePath, NULL);

  if (DevicePath == NULL) {
    DEBUG ((DEBUG_ERROR, "CONSOLEDUMP: %s is ABSENT\n", VarName));
    return;
  }

  Text = ConvertDevicePathToText (DevicePath, FALSE, FALSE);
  DEBUG ((DEBUG_ERROR, "CONSOLEDUMP: %s = %s\n", VarName, Text != NULL ? Text : L"<untextable>"));
  if (Text != NULL) {
    FreePool (Text);
  }

  //
  // Walk the instances and report connectability per instance. This is
  // read-only: unlike BmConsole.c it never edits the variable on failure.
  //
  Walk  = DevicePath;
  Index = 0;
  while (Walk != NULL) {
    Instance = GetNextDevicePathInstance (&Walk, &Size);
    if (Instance == NULL) {
      break;
    }

    Status = EfiBootManagerConnectDevicePath (Instance, NULL);
    Text   = ConvertDevicePathToText (Instance, FALSE, FALSE);
    DEBUG ((
      DEBUG_ERROR,
      "CONSOLEDUMP:   %s[%d] connect = %r  <- %s\n",
      VarName,
      Index,
      Status,
      Text != NULL ? Text : L"<untextable>"
      ));
    if (Text != NULL) {
      FreePool (Text);
    }

    FreePool (Instance);
    Index++;
  }

  FreePool (DevicePath);
}

/**
  Do the device specific action before the console is connected.

  Such as:

      Initialize the platform boot order
      Supply Console information

  Returns value from GetPlatformPreffered Console, which will be the handle and device path
  of the device console
**/
EFI_HANDLE
EFIAPI
DeviceBootManagerBeforeConsole (
  EFI_DEVICE_PATH_PROTOCOL   **DevicePath,
  BDS_CONSOLE_CONNECT_ENTRY  **PlatformConsoles
  )
{
  if (!PcdGetBool (PcdAppleConnectUsbKeyboardConsole)) {
    RemoveUsbKeyboardFromConIn ();
  }

  MsBootOptionsLibRegisterDefaultBootOptions ();
  *PlatformConsoles = GetPlatformConsoleList ();

  return GetPlatformPreferredConsole (DevicePath);
}

/**
  Do the device specific action after the console is connected.

  Such as:
**/
EFI_DEVICE_PATH_PROTOCOL **
EFIAPI
DeviceBootManagerAfterConsole (
  VOID
  )
{
  EFI_BOOT_MODE    BootMode;
  TPM_PP_PROTOCOL  *TpmPp = NULL;
  EFI_STATUS       Status;

  //
  // Attempt 33 diagnostic. Runs after gPlatformConsoles has been merged into the
  // console variables (BdsPlatform.c:237-256) and after BdsEntry has connected
  // ConOut/ErrOut, so what it prints is the state that actually decided whether
  // the VUART became a console.
  //
  DumpConsoleVariable (ConIn, L"ConIn");
  DumpConsoleVariable (ConOut, L"ConOut");

  MsPreBootChecks ();

  PlatformPowerLevelCheck ();

  Status = DisplayBootGraphic (BG_SYSTEM_LOGO);
  if (EFI_ERROR (Status) != FALSE) {
    DEBUG ((DEBUG_ERROR, "%a Unabled to set graphics - %r\n", __FUNCTION__, Status));
  }

  ConsoleMsgLibDisplaySystemInfoOnConsole ();

  BootMode = GetBootModeHob ();

  if (BootMode != BOOT_ON_FLASH_UPDATE) {
    Status = gBS->LocateProtocol (&gTpmPpProtocolGuid, NULL, (VOID **)&TpmPp);
    if (!EFI_ERROR (Status) && (TpmPp != NULL)) {
      Status = TpmPp->PromptForConfirmation (TpmPp);
      DEBUG ((DEBUG_ERROR, "%a: Unexpected return from Tpm Physical Presence. Code=%r\n", __FUNCTION__, Status));
    }
  }

  return GetPlatformConnectList ();
}

static
VOID
RebootToFrontPage (
  VOID
  )
{
  UINT64      OsIndication;
  EFI_STATUS  Status;

  OsIndication = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
  Status       = gRT->SetVariable (
                        L"OsIndications",
                        &gEfiGlobalVariableGuid,
                        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                        sizeof (UINT64),
                        &OsIndication
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Unable to set OsIndications\n"));
  }

  DEBUG ((DEBUG_INFO, "%a Resetting system.\n", __FUNCTION__));
  gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);

  CpuDeadLoop ();
}

/**
ProcessBootCompletion
*/
VOID
EFIAPI
DeviceBootManagerProcessBootCompletion (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption
  )
{
  UINTN       BufferSize;
  BOOLEAN     MsBootNext = FALSE;
  EFI_STATUS  Status;
  EFI_STATUS  RestartStatus;

  BufferSize = sizeof (MsBootNext);
  Status     = gRT->GetVariable (
                      L"MsBootNext",
                      &gMsBootMenuFormsetGuid,
                      NULL,
                      &BufferSize,
                      &MsBootNext
                      );
  if (!EFI_ERROR (Status)) {
    Status = gRT->SetVariable (
                    L"MsBootNext",
                    &gMsBootMenuFormsetGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    0,
                    NULL
                    );
  }

  RestartStatus = BootOption->Status;
  if (OEM_PREVIOUS_SECURITY_VIOLATION == BootOption->Status) {
    RestartStatus = EFI_SECURITY_VIOLATION;
  }

  if (MsBootNext) {
    //
    // Second RebootToFrontPage() call site, deliberately left as a reset.
    //
    // It has the same underlying problem as the one in
    // DeviceBootManagerUnableToBoot() -- the emulated variable store loses
    // OsIndications across the reset, so this does not actually land in
    // FrontPage. It is not an infinite loop though: MsBootNext is cleared just
    // above, so the following boot falls through to UnableToBoot, which now
    // enters the menu directly. The cost is one wasted reset.
    //
    // Not converted to an in-place EfiBootManagerBoot() because this runs from
    // ProcessBootCompletion, i.e. while unwinding a boot attempt that has just
    // returned; re-entering the boot manager from there is a re-entrancy risk
    // that the UnableToBoot path does not have. Revisit together with real
    // variable persistence.
    //
    SetRebootReason (RestartStatus);
    RebootToFrontPage ();    // Reboot to front page
  }

  SetRebootReason (BootOption->Status);
  Status = SetGraphicsConsoleMode (GCM_NATIVE_RES);
  if (EFI_ERROR (Status) != FALSE) {
    DEBUG ((DEBUG_ERROR, "%a Unabled to set console mode - %r\n", __FUNCTION__, Status));
  }

  return;
}

/**
 * Check for HardKeys during boot.  If the hard keys are pressed, builds
 * a boot option for the specific hard key setting.
 *
 *
 * @param BootOption   - Boot Option filled in based on which hard key is pressed
 *
 * @return EFI_STATUS  - EFI_NOT_FOUND - no hard key pressed, no BootOption
 *                       EFI_SUCCESS   - BootOption is valid
 *                       other error   - Unable to build BootOption
 */
EFI_STATUS
EFIAPI
DeviceBootManagerPriorityBoot (
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption
  )
{
  BOOLEAN     FrontPageBoot;
  BOOLEAN     UFPBoot;
  BOOLEAN     AltDeviceBoot;
  EFI_STATUS  Status = EFI_SUCCESS;

  FrontPageBoot = MsBootPolicyLibIsSettingsBoot ();
  UFPBoot = FALSE;
  AltDeviceBoot = MsBootPolicyLibIsAltBoot ();
  MsBootPolicyLibClearBootRequests ();

  // There are four cases:
  //   1. Nothing pressed.             return EFI_NOT_FOUND
  //   2. FrontPageBoot                load FrontPage
  //   2. UFPBoot                      load UFP
  //   3. AltDeviceBoot                load alternate boot order
  //   4. Both indicators are present  Load NetworkUnlock

  // if (UFPBoot) {
    // Front Page Boot Option
    //DEBUG ((DEBUG_INFO, "[Bds] enter UFP\n"));
    //Status = SdBootOptionsLibGetUFPMenu (BootOption, "VOL-");
  // } else 
  if (AltDeviceBoot) {
    // Alternate boot or Network Unlock option
    if (FrontPageBoot) {
      DEBUG ((DEBUG_INFO, "[Bds] both detected. NetworkUnlock\n"));
      Status = MsBootOptionsLibGetDefaultBootApp (BootOption, "NS");
    } else {
      DEBUG ((DEBUG_INFO, "[Bds] alternate boot\n"));
      Status = MsBootOptionsLibGetDefaultBootApp (BootOption, "MA");
    }
  } else if (FrontPageBoot) {
    // Front Page Boot Option
    DEBUG ((DEBUG_INFO, "[Bds] enter Front Page\n"));
    Status = MsBootOptionsLibGetBootManagerMenu (BootOption, "VOL+");
    SetRebootReason (OEM_REBOOT_TO_SETUP_KEY);
  } else {
    Status = EFI_NOT_FOUND;
  }

  return Status;
}

/**
 This is called from BDS right before going into front page
 when no bootable devices/options found
*/
VOID
EFIAPI
DeviceBootManagerUnableToBoot (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  BootManagerMenu;

  //
  // Boot the Boot Manager Menu in place rather than calling RebootToFrontPage().
  //
  // The original comment read "Have to reboot to front page as settings are
  // locked at ReadyToBoot", and the mechanism was: set OsIndications =
  // EFI_OS_INDICATIONS_BOOT_TO_FW_UI, warm reset, and let the next boot notice
  // the variable and enter FrontPage early -- before ReadyToBoot locks settings.
  //
  // That mechanism cannot work here. AppleSiliconPkg.dsc.inc sets
  // PcdEmuVariableNvModeEnable|TRUE, so the variable store is emulated and lives
  // in RAM only; nothing survives a reset. (This is the same missing persistence
  // that made PcdResetOnMemoryTypeInformationChange|FALSE necessary.) So
  // OsIndications is gone by the time the next boot reads it, BDS again finds
  // nothing bootable, calls UnableToBoot, and resets again -- an unconditional
  // reboot loop in which FrontPage is never reached. m1n1 sees each one as PSCI
  // SYSTEM_RESET.
  //
  // Booting the menu directly costs the "settings unlocked" property that the
  // reboot was buying, so settings pages may be read-only this late in BDS. That
  // is a fair trade for a boot menu that appears at all, and it is the only
  // option until the variable store is actually backed by something persistent.
  // Revisit if/when that lands.
  //
  Status = MsBootOptionsLibGetBootManagerMenu (&BootManagerMenu, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: no Boot Manager Menu option (%r), falling back to reset\n", __FUNCTION__, Status));
    RebootToFrontPage ();
    return;
  }

  DEBUG ((DEBUG_INFO, "%a: nothing bootable, entering Boot Manager Menu\n", __FUNCTION__));
  EfiBootManagerBoot (&BootManagerMenu);
  EfiBootManagerFreeLoadOption (&BootManagerMenu);

  //
  // Returning hands control back to BDS, which loops and calls this again. That
  // is the correct behaviour for a menu the user exited -- it re-displays --
  // and unlike the old path it does not reset the machine to do it.
  //
  DEBUG ((DEBUG_INFO, "%a: returned from Boot Manager Menu\n", __FUNCTION__));
}
