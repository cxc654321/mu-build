/**
 * Copyright (c) 2024, amarioguy (AppleWOA authors).
 * 
 * Module Name:
 *     AppleDartIoMmuDxe.c
 * 
 * Abstract:
 *     Platform specific driver for Apple silicon platforms to set up the DARTs.
 *     Note that the DARTs as of right now are being configured in bypass mode, so
 *     security of device memory acccesses is not as strong as it could be.
 * 
 * 
 * Environment:
 *     UEFI DXE (Driver Execution Environment).
 * 
 * License:
 *     SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT) AND GPL-2.0
 * 
 *     Original code basis is from the Asahi Linux project fork of u-boot, original copyright and author notices below.
 *     Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 *     Copyright (c) 2026 Aurora Silicon
*/

#ifndef APPLE_DART_IOMMU_DXE_H
#define APPLE_DART_IOMMU_DXE_H

#include <Library/ConvenienceMacros.h>

//
// Type definitions. Ported from AsahiLinux/u-boot project
//

typedef enum {
	AppleDartT8020Compatible = 0,
	AppleDartT8110Compatible
} APPLE_DART_TYPE;

typedef struct AppleDartInfoStruct {
	UINT64 BaseAddress;
	UINT64 *L1;
	UINT64 *L2;
	BOOLEAN BypassMode;
	INT32 Shift;
	PHYSICAL_ADDRESS DmaVirtAddrBase;
	PHYSICAL_ADDRESS DmaVirtAddrEnd;

	INT32 Nsid;
	INT32 Nttbr;
	INT32 SidEnableBase;
	INT32 TcrBase;
	UINT32 TcrTranslateEnable;
	UINT32 TcrBypass;
	INT32 TtbrBase;
	UINT32 TtbrIsValid;
	//
	// Set when this DART could not be put in bypass and was given an
	// identity map instead (see AppleDartBuildIdentityMap).  IdentityMapRoot
	// is the physical address of the top-level table the TTBRs point at.
	//
	BOOLEAN IdentityMapped;
	UINT64 IdentityMapRoot;
	//
	// This is needed due to different peripherals potentially having different types of DARTs. (T8110 style and T8020 style DARTs flush the TLB differently.)
	//
	void (*TlbFlush)(VOID *DartInfoStruct);
} APPLE_DART_INFO;

typedef struct AppleDartMapping {
	PHYSICAL_ADDRESS HostAddr;
	PHYSICAL_ADDRESS DmaVirtualAddr;
	PHYSICAL_ADDRESS PhysAddress;
	unsigned long PhysicalSize;
	unsigned long Offset;
	UINTN NumBytes;
} APPLE_DART_MAPPING;


//
// Definitions taken from AsahiLinux/u-boot/drivers/iommu/apple_dart.c
//

#define DART_PARAMS2		0x0004
#define  DART_PARAMS2_BYPASS_SUPPORT	BIT(0)

#define DART_T8020_TLB_CMD		0x0020
#define  DART_T8020_TLB_CMD_FLUSH		BIT(20)
#define  DART_T8020_TLB_CMD_BUSY		BIT(2)
#define DART_T8020_TLB_SIDMASK		0x0034
#define DART_T8020_ERROR		0x0040
#define DART_T8020_ERROR_ADDR_LO	0x0050
#define DART_T8020_ERROR_ADDR_HI	0x0054
#define DART_T8020_CONFIG		0x0060
#define  DART_T8020_CONFIG_LOCK			BIT(15)
#define DART_T8020_SID_ENABLE		0x00fc
#define DART_T8020_TCR_BASE		0x0100
#define  DART_T8020_TCR_TRANSLATE_ENABLE	BIT(7)
#define  DART_T8020_TCR_BYPASS_DART		BIT(8)
#define  DART_T8020_TCR_BYPASS_DAPF		BIT(12)
#define DART_T8020_TTBR_BASE		0x0200
#define  DART_T8020_TTBR_VALID			BIT(31)

#define DART_T8110_PARAMS3		0x0008
#define  DART_T8110_PARAMS3_VER_MIN_MASK	(0xff << 0)
#define  DART_T8110_PARAMS3_VER_MAJ_MASK	(0xff << 8)
#define  DART_T8110_PARAMS3_VA_WIDTH_SHIFT	16
#define  DART_T8110_PARAMS3_PA_WIDTH_SHIFT	24
#define  DART_T8110_PARAMS3_WIDTH_MASK		0x3f

#define DART_T8110_PARAMS4		0x000c
#define  DART_T8110_PARAMS4_NSID_MASK		(0x1ff << 0)
#define DART_T8110_TLB_CMD		0x0080
#define  DART_T8110_TLB_CMD_BUSY		BIT(31)
//
// The OP field selects the operation; it is NOT a bitmask.  FLUSH_ALL is the
// value zero, so a "flush all" is a bare write of 0 to DART_T8110_TLB_CMD.
// The constant this replaced was named FLUSH_ALL but held BIT(8), which is
// OP == 1, i.e. FLUSH_SID -- and it was being passed as the register OFFSET
// as well, so the write landed on DART_T8110_ERROR and no TLB was ever
// flushed.  Both halves of that are fixed; see AppleDartT8110TlbFlush.
//
#define  DART_T8110_TLB_CMD_OP_SHIFT		8
#define  DART_T8110_TLB_CMD_OP_FLUSH_ALL	0
#define  DART_T8110_TLB_CMD_OP_FLUSH_SID	1
#define  DART_T8110_TLB_CMD_STREAM_MASK		0xff
#define DART_T8110_ERROR		0x0100
#define DART_T8110_ERROR_MASK		0x0104
#define DART_T8110_ERROR_ADDR_LO	0x0170
#define DART_T8110_ERROR_ADDR_HI	0x0174
#define DART_T8110_PROTECT		0x0200
#define  DART_T8110_PROTECT_TTBR_TCR		BIT(0)
#define DART_T8110_SID_ENABLE_BASE	0x0c00
#define DART_T8110_TCR_BASE		0x1000
#define  DART_T8110_TCR_REMAP_MASK		(0xf << 8)
#define  DART_T8110_TCR_REMAP_EN		BIT(7)
//
// Selects a four-level walk (the TTBR counts as one level), which is what
// raises the addressable DVA range from the three-level 64GB to the full
// PARAMS3 VA_WIDTH.  T8142 needs it: DRAM sits at 0x100_0000_0000.
//
#define  DART_T8110_TCR_FOUR_LEVEL		BIT(3)
#define  DART_T8110_TCR_BYPASS_DAPF		BIT(2)
#define  DART_T8110_TCR_BYPASS_DART		BIT(1)
#define  DART_T8110_TCR_TRANSLATE_ENABLE	BIT(0)
#define DART_T8110_TTBR_BASE		0x1400
#define  DART_T8110_TTBR_VALID			BIT(0)
#define  DART_T8110_TTBR_ADDR_SHIFT		14
#define  DART_T8110_TTBR_ADDR_FIELD_SHIFT	2
#define  DART_T8110_TTBR_ADDR_MASK		0x3ffffffcU

//
// Page table entry format, "DART2" style (t8110 and t6000).  Verified against
// AsahiLinux io-pgtable-dart.c and read back off J813 hardware.
//
//   bits 37:10  physical address >> 4  (so PA bits 41:14 -- a 16KB granule and
//               a 42-bit output address, matching PARAMS3 PA_WIDTH == 42)
//   bits 51:40  subpage end,   0xfff == the whole page is accessible
//   bits 63:52  subpage start, 0     == ditto
//   bit 0       valid
//
// Table descriptors use the same address encoding with no subpage field.
//
#define APPLE_DART2_PTE_ADDR_MASK	0x0000003FFFFFFC00ULL
#define APPLE_DART2_PTE_ADDR_SHIFT	4
#define APPLE_DART_PTE_SUBPAGE_ALL	(0xfffULL << 40)
#define APPLE_DART_PTE_VALID_BIT	BIT(0)

#define DART_TABLE_SIZE			SIZE_16KB
#define DART_PTES_PER_TABLE		(DART_TABLE_SIZE / sizeof(UINT64))
#define DART_LEVEL_BITS			11
#define DART_LEVEL_INDEX_MASK		((1U << DART_LEVEL_BITS) - 1)

#define DART_SID_ENABLE(DartInfo, idx) \
	((DartInfo).SidEnableBase + 4 * (idx))
#define DART_TCR(DartInfo, sid)	((DartInfo).TcrBase + 4 * (sid))
#define DART_TTBR(DartInfo, sid, idx)	\
	((DartInfo).TtbrBase + 4 * (DartInfo).Nttbr * (sid) + 4 * (idx))
#define  DART_TTBR_SHIFT	12

#define DART_ALL_STREAMS(DartInfo)	((1U << (DartInfo)->Nsid) - 1)

#define DART_PAGE_SIZE		SIZE_16KB
#define DART_PAGE_MASK		(DART_PAGE_SIZE - 1)

#define DART_L1_TABLE		0x3
#define DART_L2_INVAL		0
#define DART_L2_VALID		BIT(0)
#define DART_L2_FULL_PAGE	BIT(1)
#define DART_L2_START(addr)	((((addr) & DART_PAGE_MASK) >> 2) << 52)
#define DART_L2_END(addr)	((((addr) & DART_PAGE_MASK) >> 2) << 40)



#endif //APPLE_DART_IOMMU_DXE_H