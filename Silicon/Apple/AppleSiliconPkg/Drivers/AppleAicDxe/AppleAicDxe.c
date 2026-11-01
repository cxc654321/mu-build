/**
 * @file AppleAicDxe.c
 * @author amarioguy (arminders208@outlook.com)
 * 
 * This file implements the selector logic for the AIC DXE Driver. Based off ArmGicDxe
 * 
 * @version 1.0
 * @date 2022-08-16
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 * 
 * Credits to the Asahi Linux contributors for the initial AIC implementations in m1n1 and linux.
 * 
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */

#include <PiDxe.h>

#include "AppleAicDxe.h"

/**
 * Initialize the AIC protocol.
 * 
 * @param ImageHandle 
 * @param SystemTable 
 * @return EFI_STATUS 
 */

EFI_STATUS
InterruptDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  APPLE_AIC_VERSION Version;

  Version = AppleArmGetAicVersion();
  switch(Version) {
    case APPLE_AIC_VERSION_1:
    case APPLE_AIC_VERSION_2:
    case APPLE_AIC_VERSION_3:
      //
      // AICv1/v2/v3 share the same init path; AppleAicV2DxeInit() branches on
      // the version for the layout differences (AICv1 TARGET_CPU-seeded banks,
      // AICv3 IRQ_CFG at 0x10000 with ADT-described capability offsets).
      //
      Status = AppleAicV2DxeInit(ImageHandle, SystemTable, Version);
      break;
    default:
      DEBUG((DEBUG_INFO, "Unsupported AIC revision, exiting\n"));
      Status = EFI_UNSUPPORTED;
  }

  return Status;
}