/** @file
  Cut a die-sized MADT down to the cores a machine actually has.

  WHY
  ---
  The MADT in each SoC family package is sized to the die, because the die is
  what Asahi's device tree describes and therefore what Tools/add-soc.py can
  generate. Apple bins these parts: an M2 Pro die carries twelve cores and ships
  as ten or twelve, an M4 die carries ten and ships as eight or ten. On a binned
  machine a die-sized table names cores that are not there.

  That is not a harmless overstatement. Windows issues PSCI CPU_ON for every
  GICC with the enabled flag set, and a truthful failure is not degraded
  gracefully -- it answers with PSCI SYSTEM_RESET and the boot ends.
  T6020J414sTopology.h records the same conclusion from the other direction:
  "CPU slots 7 and 11 are absent in the ADT and must not be published."

  iBoot's ADT lists the cores this unit has, so that is the authority. Every
  core the firmware is running on is in it by construction.

  THE TWO SPELLINGS OF A CORE'S IDENTITY
  --------------------------------------
  The ADT gives a u32 `reg` on /cpus/cpuN with the field layout m1n1 uses
  (src/smp.c): core in bits 7:0, cluster in 10:8, die in 14:11.

  The MADT gives an MPIDR. Aff0 is the core. Aff1 is the cluster numbered
  across dies -- the Ultra parts continue die 1 at cluster 8, which is why the
  die falls out as Aff1 >> 3. Aff2 only separates P cores from E cores and is
  redundant with the cluster, so it takes no part in the comparison.

  This header holds that arithmetic and the table surgery, with the ADT lookup
  left to the caller as a predicate, so both can be exercised by
  Tests/test_madt_adt_trim.c with no EDK2, no ADT and no hardware.

 * Copyright (c) 2026 Aurora Silicon
  SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
**/

#ifndef NTASI_MADT_TRIM_H_
#define NTASI_MADT_TRIM_H_

typedef unsigned long long  NTASI_MADT_U64;
typedef unsigned int        NTASI_MADT_U32;
typedef unsigned short      NTASI_MADT_U16;
typedef unsigned char       NTASI_MADT_U8;
typedef int                 NTASI_MADT_BOOL;

#define NTASI_MADT_TRUE   1
#define NTASI_MADT_FALSE  0

//
// ACPI 6.3 table and sub-structure layout, spelled out rather than pulled from
// IndustryStandard/Acpi63.h so this header compiles on a host toolchain.
//
#define NTASI_MADT_HEADER_SIZE        44u  /* description header + LocalApicAddress + Flags */
#define NTASI_MADT_LENGTH_OFFSET       4u  /* UINT32 Length in the description header */
#define NTASI_MADT_TYPE_GICC        0x0Bu
#define NTASI_MADT_TYPE_GICR        0x0Eu
#define NTASI_MADT_GICC_MPIDR_OFFSET  64u  /* UINT64 MPIDR within a GICC */
#define NTASI_MADT_GICC_UID_OFFSET     8u  /* UINT32 AcpiProcessorUid */
#define NTASI_MADT_GICR_RANGE_OFFSET  12u  /* UINT32 DiscoveryRangeLength */

//
// One redistributor frame per core. Matches MADT_GICR_INIT() in every SoC
// package's MADT_Static.aslc.
//
#define NTASI_MADT_GICR_FRAME_SIZE  0x20000u

//
// Field layout of the ADT's /cpus/cpuN `reg`.
//
#define NTASI_ADT_CPU_REG_CORE     0x000000FFu
#define NTASI_ADT_CPU_REG_CLUSTER  0x00000700u
#define NTASI_ADT_CPU_REG_DIE      0x00007800u

//
// A core's identity, in the one form both spellings reduce to.
//
typedef struct {
  NTASI_MADT_U32    Die;
  NTASI_MADT_U32    Cluster;
  NTASI_MADT_U32    Core;
} NTASI_MADT_CPU_ID;

/**
  Decompose an MPIDR as the MADT publishes it.
**/
static inline NTASI_MADT_CPU_ID
NtasiMadtCpuIdFromMpidr (
  NTASI_MADT_U64  Mpidr
  )
{
  NTASI_MADT_CPU_ID  Id;
  NTASI_MADT_U32     Aff1 = (NTASI_MADT_U32)((Mpidr >> 8) & 0xFFu);

  Id.Core    = (NTASI_MADT_U32)(Mpidr & 0xFFu);
  Id.Cluster = Aff1 & 0x7u;
  Id.Die     = Aff1 >> 3;
  return Id;
}

/**
  Decompose the ADT's /cpus/cpuN `reg`.
**/
static inline NTASI_MADT_CPU_ID
NtasiMadtCpuIdFromAdtReg (
  NTASI_MADT_U32  Reg
  )
{
  NTASI_MADT_CPU_ID  Id;

  Id.Core    = Reg & NTASI_ADT_CPU_REG_CORE;
  Id.Cluster = (Reg & NTASI_ADT_CPU_REG_CLUSTER) >> 8;
  Id.Die     = (Reg & NTASI_ADT_CPU_REG_DIE) >> 11;
  return Id;
}

static inline NTASI_MADT_BOOL
NtasiMadtCpuIdEquals (
  NTASI_MADT_CPU_ID  A,
  NTASI_MADT_CPU_ID  B
  )
{
  return (A.Die == B.Die) && (A.Cluster == B.Cluster) && (A.Core == B.Core);
}

/**
  Read a GICC's MPIDR without assuming the host's alignment or endianness.
**/
static inline NTASI_MADT_U64
NtasiMadtGiccMpidr (
  const NTASI_MADT_U8  *Gicc
  )
{
  NTASI_MADT_U64  Mpidr = 0;
  unsigned        Index;

  for (Index = 0; Index < 8u; Index++) {
    Mpidr |= ((NTASI_MADT_U64)Gicc[NTASI_MADT_GICC_MPIDR_OFFSET + Index]) << (8u * Index);
  }

  return Mpidr;
}

//
// Answers whether the machine has this core. Supplied by the caller so the
// table surgery below can be tested without an ADT.
//
typedef NTASI_MADT_BOOL (*NTASI_MADT_CPU_PRESENT_FN)(
  NTASI_MADT_CPU_ID  Id,
  void               *Context
  );

typedef struct {
  NTASI_MADT_U32    Kept;
  NTASI_MADT_U32    Dropped;
  NTASI_MADT_BOOL   Rewritten;
} NTASI_MADT_TRIM_RESULT;

/**
  Remove GICC structures for cores the predicate does not recognise.

  Rewrites the table in place and shortens it. Every other structure is
  preserved in order, and the redistributor's discovery range is re-derived
  from the surviving core count so it does not describe frames for cores that
  are gone.

  The table is left untouched, and Rewritten is false, when nothing would be
  dropped, when the predicate recognises nothing (far more likely a wrong
  assumption than a machine with no processors, and an MADT with no GICC is
  unbootable), or when a sub-structure length is malformed.

  @param[in,out] Table      The MADT.
  @param[in,out] Size       Its length; updated when the table is rewritten.
  @param[in]     Present    Predicate answering whether a core exists.
  @param[in]     Context    Passed through to the predicate.

  @return Counts, and whether the table was rewritten.
**/
static inline NTASI_MADT_TRIM_RESULT
NtasiMadtTrim (
  NTASI_MADT_U8              *Table,
  NTASI_MADT_U64             *Size,
  NTASI_MADT_CPU_PRESENT_FN  Present,
  void                       *Context
  )
{
  NTASI_MADT_TRIM_RESULT  Result = { 0, 0, NTASI_MADT_FALSE };
  NTASI_MADT_U64          ReadOff;
  NTASI_MADT_U64          WriteOff;
  NTASI_MADT_U8           *Gicr;
  NTASI_MADT_U64          Index;

  if ((Table == 0) || (Size == 0) || (*Size < NTASI_MADT_HEADER_SIZE)) {
    return Result;
  }

  //
  // Decide first, rewrite second. The verdict can come out "leave it alone" --
  // nothing to drop, nothing matched, a malformed length -- and a single pass
  // that compacted as it went would already have moved the trailing structures
  // by the time it found out.
  //
  for (ReadOff = NTASI_MADT_HEADER_SIZE; ReadOff + 2u <= *Size; ) {
    NTASI_MADT_U8  Type   = Table[ReadOff];
    NTASI_MADT_U8  Length = Table[ReadOff + 1u];

    if ((Length == 0) || (ReadOff + Length > *Size)) {
      //
      // A zero length loops forever and an overlong one reads off the end.
      // Leave the table exactly as the build produced it.
      //
      Result.Kept    = 0;
      Result.Dropped = 0;
      return Result;
    }

    if (Type == NTASI_MADT_TYPE_GICC) {
      if (Present (NtasiMadtCpuIdFromMpidr (NtasiMadtGiccMpidr (Table + ReadOff)), Context)) {
        Result.Kept++;
      } else {
        Result.Dropped++;
      }
    }

    ReadOff += Length;
  }

  if ((Result.Dropped == 0) || (Result.Kept == 0)) {
    return Result;
  }

  Gicr     = 0;
  WriteOff = NTASI_MADT_HEADER_SIZE;
  for (ReadOff = NTASI_MADT_HEADER_SIZE; ReadOff + 2u <= *Size; ) {
    NTASI_MADT_U8  Type   = Table[ReadOff];
    NTASI_MADT_U8  Length = Table[ReadOff + 1u];

    if ((Type == NTASI_MADT_TYPE_GICC) &&
        !Present (NtasiMadtCpuIdFromMpidr (NtasiMadtGiccMpidr (Table + ReadOff)), Context))
    {
      ReadOff += Length;
      continue;
    }

    if (WriteOff != ReadOff) {
      for (Index = 0; Index < Length; Index++) {
        Table[WriteOff + Index] = Table[ReadOff + Index];
      }
    }

    if (Type == NTASI_MADT_TYPE_GICR) {
      Gicr = Table + WriteOff;
    }

    WriteOff += Length;
    ReadOff  += Length;
  }

  if (Gicr != 0) {
    NTASI_MADT_U32  Range = NTASI_MADT_GICR_FRAME_SIZE * Result.Kept;

    for (Index = 0; Index < 4u; Index++) {
      Gicr[NTASI_MADT_GICR_RANGE_OFFSET + Index] = (NTASI_MADT_U8)(Range >> (8u * Index));
    }
  }

  for (Index = 0; Index < 4u; Index++) {
    Table[NTASI_MADT_LENGTH_OFFSET + Index] = (NTASI_MADT_U8)(WriteOff >> (8u * Index));
  }

  *Size            = WriteOff;
  Result.Rewritten = NTASI_MADT_TRUE;
  return Result;
}

#endif /* NTASI_MADT_TRIM_H_ */
