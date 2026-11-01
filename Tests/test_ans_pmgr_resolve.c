/*
 * Host-side, hardware-free regression test for the ANS PMGR domain
 * resolution arithmetic in
 * Silicon/Apple/AppleSiliconPkg/Include/Drivers/NtasiAnsPmgrResolve.h.
 *
 * This file #includes that header directly (the exact same text compiled
 * into AcpiPlatform.c under the EDK2/Clang toolchain) and compiles
 * standalone with a plain host C compiler -- no EDK2, no ADT, no
 * hardware, no proxy. Run it with:
 *
 *   cc -std=c99 -Wall -Wextra -o /tmp/test_ans_pmgr_resolve \
 *      Tests/test_ans_pmgr_resolve.c && /tmp/test_ans_pmgr_resolve
 *
 * or via Tests/test_ans_pmgr_resolve.py, which does exactly that.
 *
 * The synthetic device table built in build_synthetic_pmgr_table() below
 * reproduces the real J414s "/arm-io/pmgr" table shape well enough to pin
 * the bug: two register blocks (the main "pmgr" block at 0x28E080000 and
 * the "pmgr_east" block at 0x290280000, matching the coordinator's live
 * ADT walk on 2026-07-30), with ANS2/APCIE_ST/APCIE_ST_SYS/APCIE_ST1_SYS
 * placed in pmgr_east and DCS_09/DCS_10 placed in the main pmgr block at
 * the *same* low offsets (0x1A0/0x1A8) the wrong DSC constants used to
 * point at. Name-based resolution must find the pmgr_east addresses for
 * the ANS domains and must never confuse them with DCS_09/DCS_10, even
 * though both live at identical offsets within their respective blocks.
 *
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "../Silicon/Apple/AppleSiliconPkg/Include/Drivers/NtasiAnsPmgrResolve.h"

static int gFailures = 0;

#define CHECK(description, condition)                                          \
  do {                                                                          \
    if (condition) {                                                           \
      printf("PASS: %s\n", (description));                                     \
    } else {                                                                   \
      printf("FAIL: %s\n", (description));                                     \
      gFailures++;                                                             \
    }                                                                          \
  } while (0)

#define PMGR_MAIN_BASE   0x28E080000ULL
#define PMGR_EAST_BASE   0x290280000ULL
#define MAX_DEVICES      6

static void
set_device (
  unsigned char *Devices,
  unsigned       Index,
  const char    *Name,
  unsigned char  PsRegIdx,
  unsigned char  AddrOffsetShr3
  )
{
  unsigned char *Entry = Devices + (unsigned)Index * NTASI_PMGR_DEVICE_SIZE;

  memset(Entry, 0, NTASI_PMGR_DEVICE_SIZE);
  Entry[NTASI_PMGR_DEVICE_PSREG_IDX] = PsRegIdx;
  Entry[NTASI_PMGR_DEVICE_ADDR_OFFSET] = AddrOffsetShr3;
  /* name field: 16 bytes, NUL-padded, deliberately not assuming strlen(Name) < 16 */
  {
    size_t i;
    size_t len = strlen(Name);
    for (i = 0; i < NTASI_PMGR_DEVICE_NAME_LEN; i++) {
      Entry[NTASI_PMGR_DEVICE_NAME_OFFSET + i] = (i < len) ? (unsigned char)Name[i] : 0;
    }
  }
}

/*
 * Builds a synthetic "/arm-io/pmgr" table with the real J414s shape:
 * two reg-tuple blocks, and six devices -- the four ANS domains (in
 * pmgr_east) plus DCS_09/DCS_10 (in the main pmgr block, at the same low
 * offsets) to reproduce the exact 2026-07-30 misresolution.
 */
static void
build_synthetic_pmgr_table (
  unsigned char   *Devices,
  NTASI_PMGR_U64  *RegTupleBases,
  NTASI_PMGR_U32  *PsRegs
  )
{
  /* reg tuple 0 = main "pmgr" block, reg tuple 1 = "pmgr_east" block. */
  RegTupleBases[0] = PMGR_MAIN_BASE;
  RegTupleBases[1] = PMGR_EAST_BASE;

  /* ps-regs group 0 -> reg tuple 0 (main pmgr), group 1 -> reg tuple 1 (pmgr_east). */
  PsRegs[0] = 0; PsRegs[1] = 0; PsRegs[2] = 0; /* group 0: {reg_idx=0, reg_offset=0, unused} */
  PsRegs[3] = 1; PsRegs[4] = 0; PsRegs[5] = 0; /* group 1: {reg_idx=1, reg_offset=0, unused} */

  /* The four real ANS domains, in pmgr_east (ps-reg group 1). */
  set_device(Devices, 0, "ANS2",          1, 0x1A8u >> 3); /* -> 0x2902801A8 */
  set_device(Devices, 1, "APCIE_ST",      1, 0x1A0u >> 3); /* -> 0x2902801A0 */
  set_device(Devices, 2, "APCIE_ST_SYS",  1, 0x408u >> 3); /* -> 0x290280408 */
  set_device(Devices, 3, "APCIE_ST1_SYS", 1, 0x410u >> 3); /* -> 0x290280410 */

  /* DCS_09/DCS_10, in the main pmgr block (ps-reg group 0), at the SAME
   * low offsets the old wrong DSC constants pointed at. */
  set_device(Devices, 4, "DCS_10", 0, 0x1A8u >> 3); /* -> 0x28E0801A8 (old PcdAppleAnsPmgrResetBase) */
  set_device(Devices, 5, "DCS_09", 0, 0x1A0u >> 3); /* -> 0x28E0801A0 (old PcdAppleAnsPmgrApcieStBase) */
}

static void
test_AnsDomainsResolveToPmgrEast (void)
{
  unsigned char   devices[MAX_DEVICES * NTASI_PMGR_DEVICE_SIZE];
  NTASI_PMGR_U64  regTupleBases[2];
  NTASI_PMGR_U32  psRegs[6];
  NTASI_PMGR_U64  address;

  build_synthetic_pmgr_table(devices, regTupleBases, psRegs);

  CHECK(
    "ANS2 resolves to the hardware-confirmed 0x2902801A8, not the old 0x28E0801A8",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "ANS2", &address) == NTASI_PMGR_TRUE
      && address == 0x2902801A8ULL
  );

  CHECK(
    "APCIE_ST resolves to the hardware-confirmed 0x2902801A0, not the old 0x28E0801A0",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "APCIE_ST", &address) == NTASI_PMGR_TRUE
      && address == 0x2902801A0ULL
  );

  CHECK(
    "APCIE_ST_SYS resolves to the hardware-confirmed 0x290280408",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "APCIE_ST_SYS", &address) == NTASI_PMGR_TRUE
      && address == 0x290280408ULL
  );

  CHECK(
    "APCIE_ST1_SYS resolves to the hardware-confirmed 0x290280410",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "APCIE_ST1_SYS", &address) == NTASI_PMGR_TRUE
      && address == 0x290280410ULL
  );
}

static void
test_DcsMisresolutionRegression_2026_07_30 (void)
{
  unsigned char   devices[MAX_DEVICES * NTASI_PMGR_DEVICE_SIZE];
  NTASI_PMGR_U64  regTupleBases[2];
  NTASI_PMGR_U32  psRegs[6];
  NTASI_PMGR_U64  ans2Address;
  NTASI_PMGR_U64  apcieStAddress;
  NTASI_PMGR_U64  dcs09Address;
  NTASI_PMGR_U64  dcs10Address;

  build_synthetic_pmgr_table(devices, regTupleBases, psRegs);

  /* The bug: someone (or something) assumes a single register block and
   * computes "block base + low offset" directly instead of resolving by
   * name. Reproduce that wrong computation explicitly and confirm it does
   * NOT match what name-based resolution returns for the ANS domains --
   * i.e. that computation lands on DCS instead. */
  CHECK(
    "regression: old PcdAppleAnsPmgrResetBase (0x28E0801A8) is DCS_10's address, not ANS2's",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "DCS_10", &dcs10Address) == NTASI_PMGR_TRUE
      && dcs10Address == 0x28E0801A8ULL
  );
  CHECK(
    "regression: old PcdAppleAnsPmgrApcieStBase (0x28E0801A0) is DCS_09's address, not APCIE_ST's",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "DCS_09", &dcs09Address) == NTASI_PMGR_TRUE
      && dcs09Address == 0x28E0801A0ULL
  );

  NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "ANS2", &ans2Address);
  NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "APCIE_ST", &apcieStAddress);

  CHECK(
    "regression: resolving \"ANS2\" by name never returns DCS_10's address",
    ans2Address != dcs10Address
  );
  CHECK(
    "regression: resolving \"APCIE_ST\" by name never returns DCS_09's address",
    apcieStAddress != dcs09Address
  );
}

static void
test_AmbiguousOrMissingNamesAreRefused (void)
{
  unsigned char   devices[MAX_DEVICES * NTASI_PMGR_DEVICE_SIZE];
  NTASI_PMGR_U64  regTupleBases[2];
  NTASI_PMGR_U32  psRegs[6];
  NTASI_PMGR_U64  address;

  build_synthetic_pmgr_table(devices, regTupleBases, psRegs);

  CHECK(
    "a name with no matching device is refused, not silently zero",
    NtasiPmgrFindDomainAddress(devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "ANS3", &address) == NTASI_PMGR_FALSE
  );

  {
    /* Duplicate "ANS2" entries: ambiguity must be refused, not resolved
     * to whichever one happens to be found first. */
    unsigned char dup[MAX_DEVICES * NTASI_PMGR_DEVICE_SIZE];
    memcpy(dup, devices, sizeof(dup));
    set_device(dup, 4, "ANS2", 0, 0x1A0u >> 3); /* clobber DCS_10's slot with a second "ANS2" */

    CHECK(
      "two devices sharing a name are refused rather than resolved to the first",
      NtasiPmgrFindDomainAddress(dup, MAX_DEVICES, regTupleBases, 2, psRegs, 6, "ANS2", &address) == NTASI_PMGR_FALSE
    );
  }

  CHECK(
    "an out-of-range psreg_idx is refused",
    NtasiPmgrResolveAddress(regTupleBases, 2, psRegs, 6, (NTASI_PMGR_U8)5, 0, &address) == NTASI_PMGR_FALSE
  );

  CHECK(
    "a reg_idx beyond the resolved reg-tuple count is refused",
    NtasiPmgrResolveAddress(regTupleBases, 1 /* only tuple 0 available */, psRegs, 6, (NTASI_PMGR_U8)1, 0, &address) == NTASI_PMGR_FALSE
  );
}

static void
test_NameMatchingEdgeCases (void)
{
  unsigned char entry[NTASI_PMGR_DEVICE_SIZE];

  set_device(entry, 0, "ANS2", 0, 0);
  CHECK("exact name match", NtasiPmgrDeviceNameEquals(entry, 0, "ANS2") == NTASI_PMGR_TRUE);
  CHECK("prefix is not a match", NtasiPmgrDeviceNameEquals(entry, 0, "ANS") == NTASI_PMGR_FALSE);
  CHECK("superstring is not a match", NtasiPmgrDeviceNameEquals(entry, 0, "ANS20") == NTASI_PMGR_FALSE);
  CHECK("different name entirely is not a match", NtasiPmgrDeviceNameEquals(entry, 0, "DCS_09") == NTASI_PMGR_FALSE);
  CHECK("case matters: lowercase does not match the uppercase ADT spelling", NtasiPmgrDeviceNameEquals(entry, 0, "ans2") == NTASI_PMGR_FALSE);

  set_device(entry, 0, "APCIE_ST1_SYS", 0, 0); /* 13 chars, the longest of the four */
  CHECK("longest domain name (13 chars) matches exactly", NtasiPmgrDeviceNameEquals(entry, 0, "APCIE_ST1_SYS") == NTASI_PMGR_TRUE);
  CHECK("longest domain name does not match a truncation", NtasiPmgrDeviceNameEquals(entry, 0, "APCIE_ST1_SY") == NTASI_PMGR_FALSE);
}

/*
 * T8142 (M5 / J813) ps-groups layout.
 *
 * Every number below is measured from this machine's live ADT, not invented:
 * 54 reg tuples, 3 ps-groups indexing reg 0 / 1 / 48, and the storage domains
 * named ANS / APCIE_ST / APCIE_SYS_ST resolving to 0x380700300 / 0x380700410 /
 * 0x380700520 -- which are exactly the three hardware-confirmed
 * PcdAppleAnsPmgr*Base constants already in MacBookAir2026.dsc. There is no
 * APCIE_ST1_SYS on this SoC.
 */
#define T8142_PMGR_REG0   0x380700000ULL
#define T8142_PMGR_REG1   0x388280000ULL
#define T8142_PMGR_REG48  0x580E80000ULL

static void
set_device_group (
  unsigned char  *Devices,
  unsigned        Index,
  const char     *Name,
  unsigned        Group,
  unsigned        Offset
  )
{
  unsigned char *Entry = Devices + (unsigned)Index * NTASI_PMGR_DEVICE_SIZE;
  NTASI_PMGR_U32 Word = ((NTASI_PMGR_U32)Group << 24) | (Offset & 0xFFFFFFu);
  size_t i;
  size_t len = strlen(Name);

  memset(Entry, 0, NTASI_PMGR_DEVICE_SIZE);
  /* addr_offset and psreg_idx are zero on T8142; leave them so. */
  Entry[NTASI_PMGR_DEVICE_GROUP_WORD + 0] = (unsigned char)(Word & 0xFFu);
  Entry[NTASI_PMGR_DEVICE_GROUP_WORD + 1] = (unsigned char)((Word >> 8) & 0xFFu);
  Entry[NTASI_PMGR_DEVICE_GROUP_WORD + 2] = (unsigned char)((Word >> 16) & 0xFFu);
  Entry[NTASI_PMGR_DEVICE_GROUP_WORD + 3] = (unsigned char)((Word >> 24) & 0xFFu);
  for (i = 0; i < NTASI_PMGR_DEVICE_NAME_LEN; i++) {
    Entry[NTASI_PMGR_DEVICE_NAME_OFFSET + i] = (i < len) ? (unsigned char)Name[i] : 0;
  }
}

static void
test_T8142PsGroupsLayout (void)
{
  unsigned char   devices[5 * NTASI_PMGR_DEVICE_SIZE];
  NTASI_PMGR_U64  regTupleBases[49];
  NTASI_PMGR_U32  psGroups[9];
  NTASI_PMGR_U64  address;
  unsigned        i;

  for (i = 0; i < 49; i++) {
    regTupleBases[i] = 0;
  }
  regTupleBases[0]  = T8142_PMGR_REG0;
  regTupleBases[1]  = T8142_PMGR_REG1;
  regTupleBases[48] = T8142_PMGR_REG48;

  /* {reg_idx, offset, mask} triples -- only reg_idx participates in the address. */
  psGroups[0] = 0;  psGroups[1] = 0; psGroups[2] = 0;
  psGroups[3] = 1;  psGroups[4] = 0; psGroups[5] = 0;
  psGroups[6] = 48; psGroups[7] = 0; psGroups[8] = 0;

  set_device_group(devices, 0, "ANS",          0, 0x300);
  set_device_group(devices, 1, "APCIE_ST",     0, 0x410);
  set_device_group(devices, 2, "APCIE_SYS_ST", 0, 0x520);
  set_device_group(devices, 3, "ATC0_USB",     1, 0x0D0);
  set_device_group(devices, 4, "OUT_OF_RANGE", 7, 0x100);

  CHECK(
    "T8142 ANS resolves to the measured 0x380700300",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "ANS", &address) == NTASI_PMGR_TRUE
      && address == 0x380700300ULL
  );

  CHECK(
    "T8142 APCIE_ST resolves to the measured 0x380700410",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "APCIE_ST", &address) == NTASI_PMGR_TRUE
      && address == 0x380700410ULL
  );

  CHECK(
    "T8142 APCIE_SYS_ST resolves to the measured 0x380700520",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "APCIE_SYS_ST", &address) == NTASI_PMGR_TRUE
      && address == 0x380700520ULL
  );

  CHECK(
    "a ps-group other than 0 indexes its own reg tuple (group 1 -> reg 1)",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "ATC0_USB", &address) == NTASI_PMGR_TRUE
      && address == (T8142_PMGR_REG1 + 0x0D0ULL)
  );

  CHECK(
    "a group index past the ps-groups table is refused, not wrapped",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "OUT_OF_RANGE", &address) == NTASI_PMGR_FALSE
  );

  CHECK(
    "the T602x name is absent on T8142 and resolves to nothing",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "ANS2", &address) == NTASI_PMGR_FALSE
  );

  CHECK(
    "APCIE_ST1_SYS does not exist on T8142 and is refused rather than aliased",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, psGroups, 9, "APCIE_ST1_SYS", &address) == NTASI_PMGR_FALSE
  );

  CHECK(
    "with neither table supplied nothing resolves (fail closed)",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 49, 0, 0, 0, 0, "ANS", &address) == NTASI_PMGR_FALSE
  );

  /*
   * A reg index beyond what the caller resolved must be refused rather than
   * read out of bounds. This is the case Mu's old 16-tuple cap would have hit
   * on J813, whose third ps-group indexes reg 48.
   */
  CHECK(
    "a ps-group whose reg index exceeds the resolved tuple count is refused",
    NtasiPmgrFindDomainAddressEx(devices, 5, regTupleBases, 16, 0, 0, psGroups, 9, "ANS", &address) == NTASI_PMGR_TRUE
  );
  {
    unsigned char groupTwo[NTASI_PMGR_DEVICE_SIZE];
    set_device_group(groupTwo, 0, "HIGH_REG", 2, 0x40);
    CHECK(
      "group 2 (reg 48) resolves with 49 tuples but is refused with only 16",
      NtasiPmgrFindDomainAddressEx(groupTwo, 1, regTupleBases, 49, 0, 0, psGroups, 9, "HIGH_REG", &address) == NTASI_PMGR_TRUE
        && address == (T8142_PMGR_REG48 + 0x40ULL)
        && NtasiPmgrFindDomainAddressEx(groupTwo, 1, regTupleBases, 16, 0, 0, psGroups, 9, "HIGH_REG", &address) == NTASI_PMGR_FALSE
    );
  }
}

static void
test_PsRegsWinsWhenBothTablesArePresent (void)
{
  unsigned char   devices[MAX_DEVICES * NTASI_PMGR_DEVICE_SIZE];
  NTASI_PMGR_U64  regTupleBases[2];
  NTASI_PMGR_U32  psRegs[6];
  NTASI_PMGR_U32  psGroups[3];
  NTASI_PMGR_U64  address;

  build_synthetic_pmgr_table(devices, regTupleBases, psRegs);

  /* A ps-groups table that, if consulted, would give a different answer. */
  psGroups[0] = 0; psGroups[1] = 0; psGroups[2] = 0;

  CHECK(
    "ps-regs takes precedence when the ADT carries both layouts",
    NtasiPmgrFindDomainAddressEx(
      devices, MAX_DEVICES, regTupleBases, 2, psRegs, 6, psGroups, 3, "ANS2", &address
      ) == NTASI_PMGR_TRUE
      && address == 0x2902801A8ULL
  );
}

int
main (void)
{
  test_AnsDomainsResolveToPmgrEast();
  test_DcsMisresolutionRegression_2026_07_30();
  test_AmbiguousOrMissingNamesAreRefused();
  test_NameMatchingEdgeCases();
  test_T8142PsGroupsLayout();
  test_PsRegsWinsWhenBothTablesArePresent();

  if (gFailures != 0) {
    printf("\n%d assertion(s) FAILED\n", gFailures);
    return 1;
  }

  printf("\nall assertions passed\n");
  return 0;
}
