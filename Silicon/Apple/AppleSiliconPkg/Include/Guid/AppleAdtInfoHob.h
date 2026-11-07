/** SPDX-License-Identifier: BSD-2-Clause-Patent
 * Actual copied ADT handoff. Geometry/reservation is NOT loader ownership proof.
 */
#ifndef APPLE_ADT_INFO_HOB_H
#define APPLE_ADT_INFO_HOB_H
#include <Uefi.h>
#define APPLE_ADT_INFO_VERSION 1U
#define APPLE_ADT_INFO_RESERVED 1U
typedef struct {
  UINT32 Version;
  UINT32 Flags;
  UINT64 AdtBase;
  UINT64 AdtSize;
  UINT64 AdtPagesBase;
  UINT64 AdtPagesSize;
  UINT64 BootArgsPagesBase;
  UINT64 BootArgsPagesSize;
} APPLE_ADT_INFO_HOB;
extern EFI_GUID gAppleSiliconPkgAdtInfoHobGuid;
#endif
