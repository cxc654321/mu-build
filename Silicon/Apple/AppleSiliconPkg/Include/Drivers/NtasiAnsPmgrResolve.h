/*
 * Copyright (c) 2026 Aurora Silicon
 */

/** @file
  Pure, freestanding, host-testable arithmetic for resolving Apple PMGR
  power-state register addresses from the live ADT's "/arm-io/pmgr" node,
  by exact device name -- mirrors m1n1's pmgr_find_device() +
  pmgr_device_get_addr() (src/pmgr.c) byte for byte.

  WHY THIS EXISTS: on 2026-07-30, Mu's PcdAppleAnsPmgr*Base PCDs were
  hardcoded against the wrong "/arm-io/pmgr" register block. The node's
  own "ps-regs" property describes *multiple* physical register blocks
  (each a {reg_idx, reg_offset} pair indexing the node's own multi-tuple
  "reg" property) -- ANS2/APCIE_ST/APCIE_ST_SYS/APCIE_ST1_SYS live in the
  "pmgr_east" block, not the main "pmgr" block the hardcoded constants
  pointed into. At the exact same low offsets in the wrong block sit
  DCS_09/DCS_10 -- DRAM controller power domains -- so the wrong constants
  passed every alignment/distinctness sanity check while pointing at
  completely different, far more dangerous hardware.

  This header has ZERO dependencies -- no EDK2 headers, no <stdint.h>, no
  <stdbool.h> -- for the same reason NtasiGpuReservationGuard.h does not:
  EDK2 PEI/DXE builds are typically -nostdinc, so the exact same text
  compiles unmodified both into AcpiPlatform.c (under the EDK2/Clang
  AArch64 toolchain) and into Tests/test_ans_pmgr_resolve.c (compiled
  directly with the host cc, no EDK2, no hardware, no ADT involved).

  The device-table byte layout below is modeled as raw offsets into a byte
  buffer, not a C struct, on purpose: it stays includable from a
  freestanding host test with no struct-packing/ABI assumptions about the
  including compiler, and it is the literal 48-byte layout of
  `struct pmgr_device` in m1n1's src/pmgr.c (PACKED, verified field by
  field against that source: flags=1, unk1=2, id1=1, parent-union=4,
  unk3=2, addr_offset=1 @10, psreg_idx=1 @11, unk4=14, id2=2, unk5=4,
  name=16 @32 -- 48 bytes total).

  THE GUARD THIS BUYS: NtasiPmgrFindDomainAddress() can only ever return
  an address it found attached to the exact device name it was asked to
  look for, read fresh from the live ADT on this boot. There is no numeric
  fallback anywhere in this header -- a caller that only ever asks for
  "ANS2"/"APCIE_ST"/"APCIE_ST_SYS"/"APCIE_ST1_SYS" cannot be handed back a
  DCS address by construction, the way a hardcoded constant could be wrong
  without anything noticing.

  SPDX-License-Identifier: MIT
**/

#ifndef NTASI_ANS_PMGR_RESOLVE_H_
#define NTASI_ANS_PMGR_RESOLVE_H_

typedef unsigned long long  NTASI_PMGR_U64;
typedef unsigned int        NTASI_PMGR_U32;
typedef unsigned char       NTASI_PMGR_U8;
typedef int                 NTASI_PMGR_BOOL;

#define NTASI_PMGR_TRUE   1
#define NTASI_PMGR_FALSE  0

/* Byte layout of one "/arm-io/pmgr" "devices" entry (48 bytes, packed). */
#define NTASI_PMGR_DEVICE_SIZE          48u
#define NTASI_PMGR_DEVICE_ADDR_OFFSET   10u /* u8 addr_offset  (0 on T8142) */
#define NTASI_PMGR_DEVICE_PSREG_IDX     11u /* u8 psreg_idx    (0 on T8142) */
#define NTASI_PMGR_DEVICE_GROUP_WORD    16u /* u32 group_and_offset, T8142 */
#define NTASI_PMGR_DEVICE_NAME_OFFSET   32u /* char name[16] */
#define NTASI_PMGR_DEVICE_NAME_LEN      16u

/* One PS register group, addr = RegTupleBases[reg_idx] + reg_offset. */
#define NTASI_PMGR_PSREG_STRIDE  3u /* {reg_idx, reg_offset, unused} u32 triples */

/*
  T8142 (Apple M5) REPLACED "ps-regs" WITH "ps-groups".

  Up to and including T8132 (M4) a device's power-state register took a
  two-level lookup: ps_regs[psreg_idx] gave {reg_idx, offset}, and the device
  record's own addr_offset was added shifted left by 3.

  T8142 flattens that. "ps-regs" is gone. "ps-groups" is a much smaller table
  (3 entries on J813, measured) whose first word of each triple is just a reg
  index, and the device record carries a single u32 at +0x10 holding BOTH the
  group and the complete byte offset:

      v    = u32 at device record + 0x10
      addr = RegTupleBases[ps_groups[v >> 24].reg_idx] + (v & 0xffffff)

  The ps-group's own second/third words (offset, mask) are NOT part of the
  address -- only its reg index is. The old addr_offset/psreg_idx bytes are
  zero on T8142.

  This mirrors m1n1's pmgr_get_psgroup_addr() (src/pmgr.c), which derived the
  format by diffing the J704 and J713 ADTs. Corroborated on J813 by this
  firmware's own survey: exactly 3 ps-groups, the device group byte takes only
  the values 0/1/2 across all 386 devices, and the four storage domains resolve
  to the addresses the DSC's hardware-confirmed PcdAppleAnsPmgr*Base constants
  already record.
*/
#define NTASI_PMGR_PSGROUP_STRIDE     3u /* {reg_idx, offset, mask} u32 triples */
#define NTASI_PMGR_GROUP_INDEX(v)     ((v) >> 24)
#define NTASI_PMGR_GROUP_OFFSET(v)    ((v) & 0xFFFFFFu)

/**
  TRUE if device `Index`'s 16-byte name field exactly equals `Name`
  (NUL-terminated, at most 16 bytes including the terminator). Never reads
  past `Name`'s own terminator, and never reads past the 16-byte name
  field, regardless of `Name`'s length.
**/
static inline NTASI_PMGR_BOOL
NtasiPmgrDeviceNameEquals (
  const unsigned char  *Devices,
  NTASI_PMGR_U32        Index,
  const char            *Name
  )
{
  const unsigned char  *Field;
  NTASI_PMGR_U32         I;
  unsigned char           Expected;

  Field = Devices + (NTASI_PMGR_U64)Index * NTASI_PMGR_DEVICE_SIZE + NTASI_PMGR_DEVICE_NAME_OFFSET;
  for (I = 0; I < NTASI_PMGR_DEVICE_NAME_LEN; I++) {
    Expected = (unsigned char)Name[I];
    if (Field[I] != Expected) {
      return NTASI_PMGR_FALSE;
    }
    if (Expected == 0) {
      return NTASI_PMGR_TRUE;
    }
  }
  /* Name field is exactly 16 bytes with no terminator seen: only a match
   * if Name is also exactly 16 (non-NUL-terminated within the field) --
   * none of the domains this file cares about are that long, so treat as
   * no match rather than reading Name[16] (which may not be valid). */
  return NTASI_PMGR_FALSE;
}

static inline NTASI_PMGR_U8
NtasiPmgrDeviceAddrOffset (
  const unsigned char  *Devices,
  NTASI_PMGR_U32        Index
  )
{
  return Devices[(NTASI_PMGR_U64)Index * NTASI_PMGR_DEVICE_SIZE + NTASI_PMGR_DEVICE_ADDR_OFFSET];
}

static inline NTASI_PMGR_U8
NtasiPmgrDevicePsRegIdx (
  const unsigned char  *Devices,
  NTASI_PMGR_U32        Index
  )
{
  return Devices[(NTASI_PMGR_U64)Index * NTASI_PMGR_DEVICE_SIZE + NTASI_PMGR_DEVICE_PSREG_IDX];
}

/**
  Read the T8142 "group_and_offset" u32 at device record + 0x10.

  Assembled byte by byte, little-endian, for the same reason the rest of this
  header models the record as raw offsets: the buffer comes straight out of the
  ADT with no alignment guarantee, and a u32 load through a cast would be
  undefined on an unaligned address.
**/
static inline NTASI_PMGR_U32
NtasiPmgrDeviceGroupWord (
  const unsigned char  *Devices,
  NTASI_PMGR_U32        Index
  )
{
  const unsigned char  *Field;

  Field = Devices + (NTASI_PMGR_U64)Index * NTASI_PMGR_DEVICE_SIZE + NTASI_PMGR_DEVICE_GROUP_WORD;
  return (NTASI_PMGR_U32)Field[0]
         | ((NTASI_PMGR_U32)Field[1] << 8)
         | ((NTASI_PMGR_U32)Field[2] << 16)
         | ((NTASI_PMGR_U32)Field[3] << 24);
}

/**
  Resolve one device's PMGR PS register address from the T8142 ps-groups
  table. Matches m1n1's pmgr_get_psgroup_addr() (src/pmgr.c): the complete byte
  offset is already folded into GroupWord, so unlike the ps-regs path there is
  no addr_offset to add afterwards.
**/
static inline NTASI_PMGR_BOOL
NtasiPmgrResolveGroupAddress (
  const NTASI_PMGR_U64  *RegTupleBases,
  NTASI_PMGR_U32          RegTupleCount,
  const NTASI_PMGR_U32  *PsGroups,
  NTASI_PMGR_U32          PsGroupsCount,
  NTASI_PMGR_U32          GroupWord,
  NTASI_PMGR_U64         *Address
  )
{
  NTASI_PMGR_U32  Group;
  NTASI_PMGR_U32  Offset;
  NTASI_PMGR_U32  RegIdx;

  Group  = NTASI_PMGR_GROUP_INDEX (GroupWord);
  Offset = NTASI_PMGR_GROUP_OFFSET (GroupWord);

  if ((Group + 1u) * NTASI_PMGR_PSGROUP_STRIDE > PsGroupsCount) {
    return NTASI_PMGR_FALSE;
  }

  RegIdx = PsGroups[NTASI_PMGR_PSGROUP_STRIDE * Group];
  if (RegIdx >= RegTupleCount) {
    return NTASI_PMGR_FALSE;
  }

  *Address = RegTupleBases[RegIdx] + (NTASI_PMGR_U64)Offset;
  return NTASI_PMGR_TRUE;
}

/**
  Compute one device's PMGR PS register address from its (PsRegIdx,
  AddrOffset) and the node-wide ps-regs/reg-tuple tables. Matches m1n1's
  pmgr_get_psreg() + pmgr_device_get_addr() (no per-die offset -- callers
  on a multi-die SoC must add PMGR_DIE_OFFSET*die themselves; J414s/T6020
  is single-die).

  RegTupleBases[reg_idx] must already hold the resolved base address of
  the pmgr ADT node's reg_idx'th own "reg" tuple (i.e. the result of
  dt_node_reg(PmgrNode, reg_idx, ...) on the EDK2 side); this header does
  not parse #address-cells/#size-cells "reg" tuples itself, so it stays
  includable from a plain host test with a synthetic table.
**/
static inline NTASI_PMGR_BOOL
NtasiPmgrResolveAddress (
  const NTASI_PMGR_U64  *RegTupleBases,
  NTASI_PMGR_U32          RegTupleCount,
  const NTASI_PMGR_U32  *PsRegs,
  NTASI_PMGR_U32          PsRegsCount,
  NTASI_PMGR_U8           PsRegIdx,
  NTASI_PMGR_U8           AddrOffset,
  NTASI_PMGR_U64         *Address
  )
{
  NTASI_PMGR_U32  RegIdx;
  NTASI_PMGR_U32  RegOffset;

  if (((NTASI_PMGR_U32)PsRegIdx + 1u) * NTASI_PMGR_PSREG_STRIDE > PsRegsCount) {
    return NTASI_PMGR_FALSE;
  }

  RegIdx    = PsRegs[NTASI_PMGR_PSREG_STRIDE * PsRegIdx];
  RegOffset = PsRegs[NTASI_PMGR_PSREG_STRIDE * PsRegIdx + 1u];

  if (RegIdx >= RegTupleCount) {
    return NTASI_PMGR_FALSE;
  }

  *Address = RegTupleBases[RegIdx] + (NTASI_PMGR_U64)RegOffset + ((NTASI_PMGR_U64)AddrOffset << 3);
  return NTASI_PMGR_TRUE;
}

/**
  Find the unique "/arm-io/pmgr" device named exactly `Name` and resolve its
  PS register address, from EITHER layout. NTASI_PMGR_FALSE (and *Address left
  untouched) if zero or more than one device matches, or if the address cannot
  be resolved against the supplied tables -- ambiguity is refused, never
  guessed at. This is the one place the "never return an address for the wrong
  name" guarantee is enforced.

  Exactly one of the two tables is used, chosen the way m1n1 chooses: ps-regs
  when the node has it, ps-groups otherwise. Passing PsRegsCount == 0 selects
  the T8142 ps-groups path; passing PsGroupsCount == 0 selects the classic
  ps-regs path. Supplying neither resolves nothing, which is the correct
  fail-closed answer for an ADT this code does not understand.
**/
static inline NTASI_PMGR_BOOL
NtasiPmgrFindDomainAddressEx (
  const unsigned char   *Devices,
  NTASI_PMGR_U32          DeviceCount,
  const NTASI_PMGR_U64  *RegTupleBases,
  NTASI_PMGR_U32          RegTupleCount,
  const NTASI_PMGR_U32  *PsRegs,
  NTASI_PMGR_U32          PsRegsCount,
  const NTASI_PMGR_U32  *PsGroups,
  NTASI_PMGR_U32          PsGroupsCount,
  const char             *Name,
  NTASI_PMGR_U64         *Address
  )
{
  NTASI_PMGR_U32  Index;
  NTASI_PMGR_U32  MatchCount;
  NTASI_PMGR_U64  Resolved;
  NTASI_PMGR_BOOL Ok;

  MatchCount = 0;
  Resolved   = 0;
  Ok         = NTASI_PMGR_TRUE;

  for (Index = 0; Index < DeviceCount; Index++) {
    if (!NtasiPmgrDeviceNameEquals (Devices, Index, Name)) {
      continue;
    }

    MatchCount++;
    if (MatchCount > 1) {
      continue; /* keep counting to report ambiguity accurately */
    }

    if ((PsRegs != 0) && (PsRegsCount != 0)) {
      Ok = NtasiPmgrResolveAddress (
             RegTupleBases,
             RegTupleCount,
             PsRegs,
             PsRegsCount,
             NtasiPmgrDevicePsRegIdx (Devices, Index),
             NtasiPmgrDeviceAddrOffset (Devices, Index),
             &Resolved
             );
    } else if ((PsGroups != 0) && (PsGroupsCount != 0)) {
      Ok = NtasiPmgrResolveGroupAddress (
             RegTupleBases,
             RegTupleCount,
             PsGroups,
             PsGroupsCount,
             NtasiPmgrDeviceGroupWord (Devices, Index),
             &Resolved
             );
    } else {
      Ok = NTASI_PMGR_FALSE;
    }
  }

  if ((MatchCount != 1) || !Ok) {
    return NTASI_PMGR_FALSE;
  }

  *Address = Resolved;
  return NTASI_PMGR_TRUE;
}

/**
  Classic ps-regs-only lookup, kept as the pre-T8142 spelling of the call.
**/
static inline NTASI_PMGR_BOOL
NtasiPmgrFindDomainAddress (
  const unsigned char   *Devices,
  NTASI_PMGR_U32          DeviceCount,
  const NTASI_PMGR_U64  *RegTupleBases,
  NTASI_PMGR_U32          RegTupleCount,
  const NTASI_PMGR_U32  *PsRegs,
  NTASI_PMGR_U32          PsRegsCount,
  const char             *Name,
  NTASI_PMGR_U64         *Address
  )
{
  return NtasiPmgrFindDomainAddressEx (
           Devices,
           DeviceCount,
           RegTupleBases,
           RegTupleCount,
           PsRegs,
           PsRegsCount,
           0,
           0,
           Name,
           Address
           );
}

#endif // NTASI_ANS_PMGR_RESOLVE_H_
