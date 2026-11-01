/**
 * Copyright (c) 2024, amarioguy (AppleWOA authors).
 * 
 * Module Name:
 *     AppleDartIoMmuDxe.c
 * 
 * Abstract:
 *     Platform specific driver for Apple silicon platforms to set up the DARTs.
 *     This driver depends on 16k page allocation working as it (hopefully) should.
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
 *     =Copyright (C) The Asahi Linux Contributors.
 *     Copyright (c) 2026 Aurora Silicon
*/

//
// Major refactor consideration: should we have an "AppleDartIoMmuLib" for common DART operations, with separate drivers for different device type DART management?
// We do need to consider bringing up other DARTs if we want to be considered feature complete.
//

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/ArmLib.h>
#include <Library/PrintLib.h>
#include <Library/PcdLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/TimerLib.h>
#include <Library/AppleDTLib.h>

#include <Protocol/IoMmu.h>

#include <Drivers/AppleDartIoMmuDxe.h>

//
// A machine whose board has not been measured sets PcdAppleNumDwc3Controllers
// to zero -- GenericBoardPkg does, because the controller count is something
// read off a live machine rather than derived from the SoC. Zero is the honest
// value and it is handled below, but it cannot size an array: a zero-length
// array is a constraint violation, and the ones here carry an initializer, so
// the compiler rejects it outright rather than quietly accepting an extension.
//
// Size to at least one and refuse to run instead. The alternative -- keeping
// some other machine's controller count so the arrays stay non-empty -- is how
// this file would come to describe hardware nobody has looked at.
//
#define APPLE_DART_DWC3_COUNT   (FixedPcdGet32 (PcdAppleNumDwc3Controllers))
#define APPLE_DART_ARRAY_FLOOR  (APPLE_DART_DWC3_COUNT > 0 ? APPLE_DART_DWC3_COUNT : 1)

APPLE_DART_INFO DartInfo[APPLE_DART_ARRAY_FLOOR * 2];

// STATIC
// PHYSICAL_ADDRESS
// HostToDeviceAddress (
//   IN  VOID  *Address
//   )
// {
//   return (PHYSICAL_ADDRESS)(UINTN)Address;
// //   return (PHYSICAL_ADDRESS)(UINTN)Address + PcdGet64 (PcdDmaDeviceOffset);
// }

//
// Description:
//   Sets an attribute over memory that the DART manages.
//
// Return values:
//   EFI_SUCCESS - attribute is set successfully.
//

STATIC EFI_STATUS EFIAPI AppleDartIoMmuSetAttribute (
    IN EDKII_IOMMU_PROTOCOL *This, 
    IN EFI_HANDLE DeviceHandle, 
    IN VOID *Mapping, 
    IN UINT64 IoMmuAccess
    ) 
{
    //
    // Not sure of a way to do this on DART, for now, pass through the operation and return success.
    //
    return EFI_SUCCESS;
}

//
// Description:
//   Maps IOMMU managed memory so it becomes usable.
//
// Return values:
//   EFI_SUCCESS - mapped the range successfully.
//

STATIC EFI_STATUS EFIAPI AppleDartIoMmuMap(
    IN EDKII_IOMMU_PROTOCOL *This, 
    IN EDKII_IOMMU_OPERATION Operation, 
    IN VOID *HostAddress, 
    IN OUT UINTN *NumberOfBytes, 
    OUT EFI_PHYSICAL_ADDRESS *DeviceAddress, 
    OUT VOID **Mapping
    )
{

    // PHYSICAL_ADDRESS PhysAddr, DmaVirtAddr;
    // unsigned long PhysSize, Off;
    // INT32 i, idx;
    // APPLE_DART_MAPPING *DartMappingInfo;
    // UINTN NumBytes = *NumberOfBytes;

    // DEBUG((DEBUG_INFO, "%a - mapping memory into IOMMU page tables, with host address 0x%p\n", __FUNCTION__, HostAddress));

    // switch(Operation) {
    //     //
    //     // Treat everything as "common buffer" for now.
    //     //
    //     case EdkiiIoMmuOperationBusMasterRead:
    //     case EdkiiIoMmuOperationBusMasterRead64:
    //     case EdkiiIoMmuOperationBusMasterWrite:
    //     case EdkiiIoMmuOperationBusMasterWrite64:
    //     case EdkiiIoMmuOperationBusMasterCommonBuffer:
    //     case EdkiiIoMmuOperationBusMasterCommonBuffer64:
    //         //
    //         // if we are in bypass mode, just return the address verbatim.
    //         //
    //         if(DartInfo->BypassMode == TRUE) {
    //             *DeviceAddress = (PHYSICAL_ADDRESS)HostAddress;
    //             *Mapping = NULL; // nothing to map
    //             return EFI_SUCCESS;
    //         }

    //         //
    //         // since the USB-A controller doesn't support bypass mode, the below code MUST work correctly.
    //         //
    //         DartMappingInfo = AllocateZeroPool(sizeof(APPLE_DART_MAPPING));
    //         DartMappingInfo->HostAddr = (PHYSICAL_ADDRESS)HostAddress;
    //         PhysAddr = ALIGN_DOWN((PHYSICAL_ADDRESS)HostAddress, DART_PAGE_SIZE);
    //         DartMappingInfo->PhysAddress = PhysAddr;
    //         Off = (PHYSICAL_ADDRESS)HostAddress - PhysAddr;
    //         DartMappingInfo->Offset = Off;
    //         PhysSize = ALIGN(NumBytes + Off, DART_PAGE_SIZE);
    //         DartMappingInfo->PhysicalSize = PhysSize;
    //         DmaVirtAddr = (PHYSICAL_ADDRESS)HostAddress;
    //         DartMappingInfo->DmaVirtualAddr = DmaVirtAddr;

    //         DartMappingInfo->NumBytes = *NumberOfBytes;

    //         idx = DmaVirtAddr / DART_PAGE_SIZE;

    //         for(i = 0; i < PhysSize / DART_PAGE_SIZE; i++) {
    //             DartInfo->L2[idx + i] = (PhysAddr >> DartInfo->Shift) | DART_L2_VALID | DART_L2_START(0LL) | DART_L2_END(~0LL);
    //             PhysAddr += DART_PAGE_SIZE;
    //         }
    //         WriteBackInvalidateDataCacheRange((VOID *)DartInfo->L2[idx], ((UINTN)DartInfo->L2[idx + i] - (UINTN)DartInfo->L2[idx]));


    //         DartInfo->TlbFlush((VOID *)DartInfo);

    //         *DeviceAddress = HostToDeviceAddress((VOID *)DmaVirtAddr + Off);
    //         DEBUG((DEBUG_INFO, "%a - device address is 0x%llx, Off = 0x%llx, DVA: 0x%llx\n", __FUNCTION__, (DmaVirtAddr + Off), Off, DmaVirtAddr));
    //         *Mapping = DartMappingInfo;
    //         break;
        
    //     default:
    //         // if we have an invalid operation, just return
    //         break;

    // }

    
    return EFI_SUCCESS;
}

//
// Description:
//   Unmaps IOMMU managed memory.
//
// Return values:
//   EFI_SUCCESS - unmapped the range successfully.
//

STATIC EFI_STATUS EFIAPI AppleDartIoMmuUnmap(IN EDKII_IOMMU_PROTOCOL *This, IN VOID *Mapping) {
    // PHYSICAL_ADDRESS DmaVirtAddr;
    // unsigned long PhysSize;
    // INT32 i, idx;
    // APPLE_DART_MAPPING *DartMappingInfo;

    // DartMappingInfo = (APPLE_DART_MAPPING *)Mapping;

    // // DEBUG((DEBUG_INFO, "%a - Unmapping memory from IOMMU page tables\n", __FUNCTION__));

    // //
    // // if we are in bypass mode, there is no conception of unmapping, we're done.
    // //
    // if(DartInfo->BypassMode == TRUE) {
    //     return EFI_SUCCESS;
    // }

    // //
    // // since USB-A controller doesn't support bypass mode, the below code MUST work correctly if that controller is on.
    // //

    // DmaVirtAddr = ALIGN_DOWN(DartMappingInfo->PhysAddress, DART_PAGE_SIZE);

    // PhysSize = (DartMappingInfo->NumBytes) + (DartMappingInfo->PhysAddress - DmaVirtAddr);
    // PhysSize = ALIGN(PhysSize, DART_PAGE_SIZE);

    // idx = DmaVirtAddr / DART_PAGE_SIZE;

    // for(i = 0; i < PhysSize / DART_PAGE_SIZE; i++) {
    //     DartInfo->L2[idx + i] = DART_L2_INVAL;
    // }

    // WriteBackInvalidateDataCacheRange((VOID *)DartInfo->L2[idx], ((UINTN)DartInfo->L2[idx + i] - (UINTN)DartInfo->L2[idx]));

    // DartInfo->TlbFlush((VOID *)DartInfo);
    // FreeAlignedPages((VOID *)DmaVirtAddr, PhysSize);
    
    return EFI_SUCCESS;
}

//
// Description:
//   Allocates a DMA buffer for the IOMMU.
//
// Return values:
//   EFI_SUCCESS - allocated the buffer successfully.
//

STATIC EFI_STATUS EFIAPI AppleDartIoMmuAllocateBuffer (
    IN EDKII_IOMMU_PROTOCOL *This, 
    IN EFI_ALLOCATE_TYPE Type, 
    IN EFI_MEMORY_TYPE MemoryType, 
    IN UINTN Pages, 
    IN OUT VOID **HostAddress, 
    IN UINT64 Attributes
    )
{
    // UINTN NewPages;
    // NewPages = Pages;
    // //
    // // The only valid memory types are EfiBootServicesData and EfiRuntimeServicesData.
    // // Currently copied from CoherentDmaLib, atm we're just hardcoding DART_PAGE_SIZE as alignment so it should all be good
    // //

    // //
    // // HACK: if this is needed, then because EFI is always allocating pages in increments of 4k, always map 4 times the number
    // // of pages requested, so that we always end up with 16k pages from the IOMMU perspective.
    // //
    // NewPages = 4 * Pages;
    // DEBUG((DEBUG_INFO, "%a - Allocating DMA buffer for IOMMU with %d pages\n", __FUNCTION__, NewPages));
    // if (MemoryType == EfiBootServicesData) {
    //     *HostAddress = AllocateAlignedPages (NewPages, DART_PAGE_SIZE);
    // } else if (MemoryType == EfiRuntimeServicesData) {
    //     *HostAddress = AllocateAlignedRuntimePages (NewPages, DART_PAGE_SIZE);
    // } else {
    //     return EFI_INVALID_PARAMETER;
    // }

    // DEBUG((DEBUG_INFO, "%a - HostAddress is 0x%p\n", __FUNCTION__, *HostAddress));

    // if (*HostAddress == NULL) {
    //     return EFI_OUT_OF_RESOURCES;
    // }
    return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI AppleDartIoMmuFreeBuffer (
    IN EDKII_IOMMU_PROTOCOL *This,
    IN UINTN Pages,
    IN VOID *HostAddress
    )
{
    // UINTN NewPages;
    // NewPages = Pages;

    // //
    // // HACK: if this is needed, then because EFI is always allocating pages in increments of 4k, always map 4 times the number
    // // of pages requested, so that we always end up with 16k pages from the IOMMU perspective.
    // //
    // NewPages = 4 * Pages;

    // // DEBUG((DEBUG_INFO, "%a - Freeing DMA buffer at 0x%p, %d pages\n", __FUNCTION__, HostAddress, NewPages));
    // if (HostAddress == NULL) {
    //     return EFI_INVALID_PARAMETER;
    // }

    // FreeAlignedPages (HostAddress, NewPages);
    return EFI_SUCCESS;
}

EDKII_IOMMU_PROTOCOL mAppleDartIoMmuProtocol = {
    EDKII_IOMMU_PROTOCOL_REVISION,
    AppleDartIoMmuSetAttribute,
    AppleDartIoMmuMap,
    AppleDartIoMmuUnmap,
    AppleDartIoMmuAllocateBuffer,
    AppleDartIoMmuFreeBuffer,
};

STATIC VOID AppleDartT8020TlbFlush(VOID *DartInformation) {

    APPLE_DART_INFO *DartInfoStruct = (APPLE_DART_INFO *)DartInformation;
    __asm__("dsb sy");
    //SpeculationBarrier();
    MmioWrite32(DartInfoStruct->BaseAddress + DART_T8020_TLB_SIDMASK, DART_ALL_STREAMS(DartInfoStruct));
    MmioWrite32(DartInfoStruct->BaseAddress + DART_T8020_TLB_CMD, DART_T8020_TLB_CMD_FLUSH);
    while((MmioRead32(DartInfoStruct->BaseAddress + DART_T8020_TLB_CMD) & DART_T8020_TLB_CMD_BUSY) != 0) {
        continue;
    }
}

STATIC VOID AppleDartT8110TlbFlush(VOID *DartInformation) {
    APPLE_DART_INFO *DartInfoStruct = (APPLE_DART_INFO *)DartInformation;
    __asm__("dsb sy");
    //
    // OP == FLUSH_ALL is the value 0, so this is a bare write of the OP field.
    // This previously wrote to BaseAddress + BIT(8) -- the ERROR register --
    // and then polled the real TLB_CMD, which is never busy because nothing
    // had been commanded.  It looked like a working flush and was a no-op.
    //
    MmioWrite32(DartInfoStruct->BaseAddress + DART_T8110_TLB_CMD,
                DART_T8110_TLB_CMD_OP_FLUSH_ALL << DART_T8110_TLB_CMD_OP_SHIFT);
    while((MmioRead32(DartInfoStruct->BaseAddress + DART_T8110_TLB_CMD)) & DART_T8110_TLB_CMD_BUSY) {
        continue;
    }
}

/**
  Encode a physical address into a DART2-format page table entry.

  The address field holds PhysAddr >> 4 in bits 37:10, which is to say
  PhysAddr bits 41:14.  A 16KB granule is therefore implicit and the output
  address is capped at 42 bits -- exactly the PA_WIDTH J813 reports.
**/
STATIC UINT64 AppleDartEncodePte(IN UINT64 PhysAddr) {
    return ((PhysAddr >> APPLE_DART2_PTE_ADDR_SHIFT) & APPLE_DART2_PTE_ADDR_MASK) |
           APPLE_DART_PTE_VALID_BIT;
}

//
// One reserved allocation, carved into 16KB tables by a bump pointer.
//
// AllocateAlignedReservedPages() cannot be used here. It over-allocates and
// then hands the slack back with FreePages(), and under this platform's
// memory protection policy freeing part of an EfiReservedMemoryType
// allocation returns EFI_INVALID_PARAMETER -- which the library reports with
// ASSERT_EFI_ERROR, i.e. a DEBUG-build deadloop in the middle of DXE. Measured
// on J813: "ASSERT [AppleDartIoMmuDxe] MemoryAllocationLib.c(222)". So align by
// hand, waste the head, and never call FreePages at all.
//
STATIC UINT64 mDartTablePool = 0;
STATIC UINTN  mDartTablePoolRemaining = 0;

STATIC EFI_STATUS AppleDartReserveTablePool(IN UINT64 DramSize) {
    EFI_STATUS            Status;
    EFI_PHYSICAL_ADDRESS  Memory;
    UINT64                LeafSpan;
    UINT64                MidSpan;
    UINT64                Tables;
    UINTN                 PoolBytes;
    UINTN                 Pages;

    //
    // Exactly what a contiguous range needs: one leaf per 32MB, one mid-level
    // table per 64GB (plus one in case the range straddles a boundary), and
    // the top table. Sized rather than guessed so that running out is a bug
    // that fails closed in AppleDartBuildIdentityMap rather than a map with a
    // silent hole in it.
    //
    LeafSpan = (UINT64)DART_PTES_PER_TABLE * DART_PAGE_SIZE;
    MidSpan  = LeafSpan * DART_PTES_PER_TABLE;
    Tables   = (DramSize + LeafSpan - 1) / LeafSpan;
    Tables  += (DramSize + MidSpan - 1) / MidSpan + 1;
    Tables  += 1;

    PoolBytes = (UINTN)(Tables * DART_TABLE_SIZE);
    Pages = EFI_SIZE_TO_PAGES(PoolBytes) + EFI_SIZE_TO_PAGES(DART_TABLE_SIZE);

    //
    // EfiReservedMemoryType, not BootServicesData: these tables stay live for
    // as long as the DART translates, which is for the whole life of the OS.
    // Anything the OS is allowed to reclaim would be handed to a driver as
    // ordinary RAM while the DART was still walking it, and the first symptom
    // would be device DMA landing at whatever address the recycled page now
    // encodes.
    //
    Status = gBS->AllocatePages(AllocateAnyPages, EfiReservedMemoryType, Pages, &Memory);
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "%a: could not reserve %lu pages for DART tables: %r\n",
               __FUNCTION__, (unsigned long)Pages, Status));
        return Status;
    }

    mDartTablePool = ALIGN_VALUE((UINT64)Memory, (UINT64)DART_TABLE_SIZE);
    mDartTablePoolRemaining = PoolBytes;

    DEBUG((DEBUG_INFO, "%a: reserved %lu tables at 0x%llx (%lu pages from 0x%llx)\n",
           __FUNCTION__, (unsigned long)Tables, mDartTablePool,
           (unsigned long)Pages, (UINT64)Memory));
    return EFI_SUCCESS;
}

STATIC UINT64 *AppleDartAllocTable(VOID) {
    UINT64 *Table;

    if (mDartTablePoolRemaining < DART_TABLE_SIZE) {
        return NULL;
    }

    Table = (UINT64 *)(UINTN)mDartTablePool;
    mDartTablePool += DART_TABLE_SIZE;
    mDartTablePoolRemaining -= DART_TABLE_SIZE;

    ZeroMem(Table, DART_TABLE_SIZE);
    return Table;
}

/**
  Build a four-level identity map (DVA == PA) covering [DramBase, DramBase+DramSize).

  This is the fallback for DARTs that cannot bypass.  On T8142 the usb DART's
  TCR bypass bits are hardwired to zero -- PARAMS2 advertises bypass support and
  the hardware refuses it -- so the only way to make the controller's DMA
  transparent to an OS that knows nothing about DARTs is to translate every
  address to itself.

  Three levels of table plus the page is what Apple calls "four level" (the
  TTBR counts).  Three levels would reach only 64GB of DVA; DRAM on this part
  starts at 0x100_0000_0000, so the fourth level is not optional.

  Returns the physical address of the top-level table, or 0 on failure.
**/
STATIC UINT64 AppleDartBuildIdentityMap(IN UINT64 DramBase, IN UINT64 DramSize) {
    UINT64 *TopTable;
    UINT64 Va;
    UINT64 VaEnd;
    UINTN  LeafTables = 0;

    if (DramBase == 0 || DramSize == 0) {
        DEBUG((DEBUG_ERROR, "%a: refusing to build an empty identity map (0x%llx+0x%llx). "
                            "Enabling translation against one would block every transfer "
                            "while looking configured.\n", __FUNCTION__, DramBase, DramSize));
        return 0;
    }

    if ((DramBase & (DART_PAGE_SIZE - 1)) != 0 || (DramSize & (DART_PAGE_SIZE - 1)) != 0) {
        DEBUG((DEBUG_ERROR, "%a: DRAM range 0x%llx+0x%llx is not %d-aligned\n",
               __FUNCTION__, DramBase, DramSize, DART_PAGE_SIZE));
        return 0;
    }

    if (EFI_ERROR(AppleDartReserveTablePool(DramSize))) {
        return 0;
    }

    TopTable = AppleDartAllocTable();
    if (TopTable == NULL) {
        DEBUG((DEBUG_ERROR, "%a: out of memory for the top-level table\n", __FUNCTION__));
        return 0;
    }

    Va = DramBase;
    VaEnd = DramBase + DramSize;

    while (Va < VaEnd) {
        UINT64 *Level1;
        UINT64 *Level2;
        UINT32 Index0 = (UINT32)((Va >> 36) & DART_LEVEL_INDEX_MASK);
        UINT32 Index1 = (UINT32)((Va >> 25) & DART_LEVEL_INDEX_MASK);
        UINT32 Index2 = (UINT32)((Va >> 14) & DART_LEVEL_INDEX_MASK);

        if ((TopTable[Index0] & APPLE_DART_PTE_VALID_BIT) == 0) {
            Level1 = AppleDartAllocTable();
            if (Level1 == NULL) {
                DEBUG((DEBUG_ERROR, "%a: out of memory at level 1\n", __FUNCTION__));
                return 0;
            }
            TopTable[Index0] = AppleDartEncodePte((UINT64)(UINTN)Level1);
        } else {
            Level1 = (UINT64 *)(UINTN)((TopTable[Index0] & APPLE_DART2_PTE_ADDR_MASK)
                                       << APPLE_DART2_PTE_ADDR_SHIFT);
        }

        if ((Level1[Index1] & APPLE_DART_PTE_VALID_BIT) == 0) {
            Level2 = AppleDartAllocTable();
            if (Level2 == NULL) {
                DEBUG((DEBUG_ERROR, "%a: out of memory at level 2\n", __FUNCTION__));
                return 0;
            }
            Level1[Index1] = AppleDartEncodePte((UINT64)(UINTN)Level2);
            LeafTables++;
        } else {
            Level2 = (UINT64 *)(UINTN)((Level1[Index1] & APPLE_DART2_PTE_ADDR_MASK)
                                       << APPLE_DART2_PTE_ADDR_SHIFT);
        }

        //
        // Fill out the rest of this leaf table before walking again; the walk
        // is the expensive part and one leaf covers 32MB.
        //
        for (; Index2 < DART_PTES_PER_TABLE && Va < VaEnd; Index2++, Va += DART_PAGE_SIZE) {
            Level2[Index2] = AppleDartEncodePte(Va) | APPLE_DART_PTE_SUBPAGE_ALL;
        }
    }

    DEBUG((DEBUG_INFO, "%a: identity mapped 0x%llx..0x%llx using %lu leaf tables\n",
           __FUNCTION__, DramBase, VaEnd, (unsigned long)LeafTables));

    return (UINT64)(UINTN)TopTable;
}

/**
  Probe whether this DART really implements bypass.

  PARAMS2 bit 0 is not trustworthy: J813's usb DARTs set it and then refuse
  every write to the TCR bypass bits, which read back as zero under every
  precondition tried (translation off, streams disabled, after UNPROTECT).
  So ask the register instead of the capability bit, and put the TCR back
  the way it was found either way.
**/
STATIC BOOLEAN AppleDartProbeBypass(IN APPLE_DART_INFO *Dart) {
    UINT64  TcrAddress = Dart->BaseAddress + DART_TCR(*Dart, 0);
    UINT32  Original = MmioRead32(TcrAddress);
    UINT32  ReadBack;

    MmioWrite32(TcrAddress, Dart->TcrBypass);
    ReadBack = MmioRead32(TcrAddress);
    MmioWrite32(TcrAddress, Original);

    return (BOOLEAN)(ReadBack == Dart->TcrBypass);
}

EFI_STATUS EFIAPI 
AppleDartIoMmuDxeInitialize(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
)
{
    UINT32 Midr;
    dt_node_t *DartNode[APPLE_DART_ARRAY_FLOOR] = { 0 };
    UINT64 DartReg[APPLE_DART_ARRAY_FLOOR * 2] = { 0 };
    UINT32 DartIndex = 0;
    UINT32 Params4; // U-Boot does this
    // PHYSICAL_ADDRESS Address;
    // PHYSICAL_ADDRESS L2;
    // INT32 Ntte;
    // INT32 NL1, NL2;
    INT32 sid, i;
    UINT32 Params2;
    CHAR8 DartNodeName[33];
    UINT64 DramBase;
    UINT64 DramSize;
    //
    // Built lazily and shared by every DART instance that needs it, so the
    // 8MB of tables is paid for once no matter how many apertures exist.
    //
    UINT64 IdentityMapRoot = 0;
    // BOOLEAN DartFound = TRUE; // assume the DART exists to start.
    //UINT32 Params4;

    //
    // Nothing to do on a machine whose DWC3 complement has not been measured.
    // Everything below walks usb-drdN nodes and their DARTs by index up to the
    // controller count, so with a count of zero there is no work, and the
    // arrays above exist only to satisfy the language.
    //
    if (APPLE_DART_DWC3_COUNT == 0) {
      DEBUG ((
        DEBUG_INFO,
        "AppleDartIoMmu: no DWC3 controller count for this board; "
        "not programming any USB DARTs\n"
        ));
      return EFI_SUCCESS;
    }


    //
    // set up the IOMMU. for now, we should only be really setting up the DARTs for the USB controllers,
    // but very likely that this will change in the future.
    
    //
    // Each DWC3 has two separate DARTs to manage requests but because the DWC3 DARTs are able to work in bypass mode, we can simply set bypass mode in this driver for each DART,
    // and then simply not register the protocol for any of them. (UEFI assumes direct DMA access is possible if no IOMMU protocol is present)
    // Note: this will NOT work for the USB-A controller, as that's on the PCIe bus (whose DARTs do NOT work in bypass mode), but other devices have a one device to one DART relation.
    // Also this driver is NOT a secure driver by virtue of setting up bypass mode.
    //


    //
    // Verify that we are on an Apple SoC by reading MIDR and checking the vendor ID bit,
    // bail out if not found.
    //
    Midr = ArmReadMidr();
    if(((Midr >> 24) & 0x61) == 0) {
      DEBUG((DEBUG_INFO, "MIDR: 0x%x\n", Midr));
      DEBUG((DEBUG_ERROR, "%a - not on an Apple SoC, DARTs not present, aborting\n", __FUNCTION__));
      ASSERT(FALSE);
      return EFI_NOT_FOUND;
    }

    //
    // Get the base addresses from the ADT. Right now there's no good way I know of to test for:
    //   a. the number of dies a multi-die capable SoC has.
    //   b. how many DARTs a given SoC die has.
    // Given that this driver is mainly meant to bring up USB controller DARTs and was mostly designed for that purpose, for now
    // only bring up the USB controller DARTs (which the following switch statement will assume hardcoded knowledge of for each SoC for now)
    // TODO: make this a dynamic lookup to keep the code SoC-agnostic.
    //

    //
    // USB-A DARTs used to be brought up in an earlier version of this driver, however due to issues with getting devices to enumerate
    // on the USB-A ports on supported devices, that code is disabled for the moment.
    //

    //
    // The whole of DRAM, from the ADT rather than from PcdSystemMemoryBase:
    // m1n1 carves itself out below phys_base, so the Pcd describes only the
    // part handed to UEFI. The identity map wants a superset of every address
    // a device could be pointed at, and mapping the extra carveout costs
    // nothing beyond leaf tables that are already being allocated in bulk.
    //
    DramBase = dt_get_u64("chosen", "dram-base", 0);
    DramSize = dt_get_u64("chosen", "dram-size", 0);
    DEBUG((DEBUG_INFO, "%a: DRAM is 0x%llx + 0x%llx\n", __FUNCTION__, DramBase, DramSize));
    if(DramBase == 0 || DramSize == 0) {
        //
        // Not fatal: a DART that can bypass never needs this. Identity mapping
        // checks IdentityMapRoot and fails closed if it could not be built.
        //
        DEBUG((DEBUG_ERROR, "%a: /chosen has no usable dram-base/dram-size; "
                            "DARTs that cannot bypass will be left blocked\n", __FUNCTION__));
    }

    for(INT32 DartNodeIndex = 0; DartNodeIndex < FixedPcdGet32(PcdAppleNumDwc3Controllers); DartNodeIndex++) {
        AsciiSPrint(DartNodeName, ARRAY_SIZE(DartNodeName), "dart-usb%d", DartNodeIndex);
        DartNode[DartNodeIndex] = dt_get(DartNodeName);

        if(DartNode[DartNodeIndex] == NULL) {
            DEBUG((DEBUG_INFO, "Did not find node %a\n\n", DartNodeName));
            DartIndex += 2;
            continue;
        }

        dt_node_reg(DartNode[DartNodeIndex], 0, &DartReg[DartIndex++], NULL);
        DEBUG((DEBUG_INFO, "DART reg[0] for %a is 0x%llx \n", DartNodeName, DartReg[DartIndex - 1]));
        dt_node_reg(DartNode[DartNodeIndex], 1, &DartReg[DartIndex++], NULL);
        DEBUG((DEBUG_INFO, "DART reg[1] for %a is 0x%llx \n", DartNodeName, DartReg[DartIndex - 1]));
    }


    for(DartIndex = 0; DartIndex < FixedPcdGet32(PcdAppleNumDwc3Controllers) * 2; DartIndex++) {
        //
        // m1n1 removes the USB controller and DART nodes that back its proxy
        // connection. Skip exactly those absent nodes, rather than fixed
        // controller indices, so every unowned Type-C port can DMA.
        //
        if(DartNode[DartIndex / 2] == NULL) {
            DEBUG((DEBUG_INFO, "Skipping absent/owned USB DART %d\n", DartIndex));
            continue;
        }

        //
        // Both register apertures of a usb DART node are real, independent
        // DART instances and both must be programmed; m1n1 says so explicitly
        // in usb_phy_handoff_host(), and a controller whose second instance
        // is unprogrammed cannot DMA.  A T8142-only branch used to skip every
        // odd DartIndex here on the theory that m1n1 owned the companion
        // aperture and touching it faulted.  Measured on J813: both apertures
        // answer identically (PARAMS1..4 byte-for-byte the same), neither
        // faults, and the blank one only looked special because the zeroing
        // pass below had just cleared it.
        //
        //DEBUG((DEBUG_INFO, "Test0\n"));
        DartInfo[DartIndex].BaseAddress = DartReg[DartIndex];

        char *CompatibleStr;
        size_t CompatibleStrLength = 0;
        CompatibleStr = dt_node_prop(DartNode[DartIndex / 2], "compatible", &CompatibleStrLength);

        if( !AsciiStrCmp(CompatibleStr ,"dart,t8110")) {
            //
            // T8110 compatible DARTs have different setup.
            //
            DEBUG((DEBUG_INFO, "%a - Setting up T8110-compatible DART\n", __FUNCTION__));
            Params4 = MmioRead32(DartInfo[DartIndex].BaseAddress + DART_T8110_PARAMS4);
            DartInfo[DartIndex].Nsid = Params4 & DART_T8110_PARAMS4_NSID_MASK;
            DartInfo[DartIndex].Nttbr = 1;
            DartInfo[DartIndex].SidEnableBase = DART_T8110_SID_ENABLE_BASE;
            DartInfo[DartIndex].TcrBase = DART_T8110_TCR_BASE;
            DartInfo[DartIndex].TcrTranslateEnable = DART_T8110_TCR_TRANSLATE_ENABLE;
            DartInfo[DartIndex].TcrBypass = (DART_T8110_TCR_BYPASS_DAPF | DART_T8110_TCR_BYPASS_DART);
            DartInfo[DartIndex].TtbrBase = DART_T8110_TTBR_BASE;
            DartInfo[DartIndex].TtbrIsValid = DART_T8110_TTBR_VALID;
            DartInfo[DartIndex].BypassMode = FALSE; // Assume there's no bypass mode by default.
            DartInfo[DartIndex].TlbFlush = AppleDartT8110TlbFlush;

        }
        else {
            DEBUG((DEBUG_INFO, "%a - Setting up T8020-compatible DART\n", __FUNCTION__));
            DartInfo[DartIndex].Nsid = 16;
            DartInfo[DartIndex].Nttbr = 4;
            DartInfo[DartIndex].SidEnableBase = DART_T8020_SID_ENABLE;
            DartInfo[DartIndex].TcrBase = DART_T8020_TCR_BASE;
            DartInfo[DartIndex].TcrTranslateEnable = DART_T8020_TCR_TRANSLATE_ENABLE;
            DartInfo[DartIndex].TcrBypass = (DART_T8020_TCR_BYPASS_DAPF | DART_T8020_TCR_BYPASS_DART);
            DartInfo[DartIndex].TtbrBase = DART_T8020_TTBR_BASE;
            DartInfo[DartIndex].TtbrIsValid = DART_T8020_TTBR_VALID;
            DartInfo[DartIndex].BypassMode = FALSE; // Assume there's no bypass mode by default.
            DartInfo[DartIndex].TlbFlush = AppleDartT8020TlbFlush;
        }

        if((AsciiStrCmp(CompatibleStr ,"dart,t8110") == 0) || (AsciiStrCmp(CompatibleStr ,"dart,t6000") == 0)) {
            DartInfo[DartIndex].Shift = 4;
        }

        DartInfo[DartIndex].DmaVirtAddrBase = DART_PAGE_SIZE;
        DartInfo[DartIndex].DmaVirtAddrEnd = SIZE_4GB - DART_PAGE_SIZE;

        for(sid = 0; sid < DartInfo[DartIndex].Nsid; sid++) {
            MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_TCR(DartInfo[DartIndex], sid), 0);
        }
        for(sid = 0; sid < DartInfo[DartIndex].Nsid; sid++) {
            for(i = 0; i < DartInfo[DartIndex].Nttbr; i++) {
                MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_TTBR(DartInfo[DartIndex], sid, i), 0);
            }
        }

        DartInfo[DartIndex].TlbFlush((VOID *)&DartInfo[DartIndex]);

        Params2 = MmioRead32(DartInfo[DartIndex].BaseAddress + DART_PARAMS2);
        //
        // Ask the TCR whether bypass works rather than believing PARAMS2.
        // On T8142 PARAMS2 sets BYPASS_SUPPORT and the TCR bypass bits are
        // hardwired to zero, so trusting the capability bit leaves the DART
        // translating nothing with translation disabled -- which blocks all
        // DMA and reads back as the "TCR=0x0, expected 0x6" that kept DWC3 in
        // reset.
        //
        if((Params2 & DART_PARAMS2_BYPASS_SUPPORT) != 0 && AppleDartProbeBypass(&DartInfo[DartIndex])) {
            DEBUG((DEBUG_INFO, "AppleDartIoMmuDxeInitialize: USB DART%d supports bypass mode\n", DartIndex));
            for(sid = 0; sid < DartInfo[DartIndex].Nsid; sid++) {
                MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_TCR(DartInfo[DartIndex], sid), DartInfo[DartIndex].TcrBypass);
            }
            DartInfo[DartIndex].BypassMode = TRUE;
            //
            // Bypass mode means the controllers can just DMA right into physical memory so we skip installing the IOMMU protocol.
            //
        }
        else if(AsciiStrCmp(CompatibleStr, "dart,t8110") == 0) {
            UINT32 Params3 = MmioRead32(DartInfo[DartIndex].BaseAddress + DART_T8110_PARAMS3);
            UINT32 VaWidth = (Params3 >> DART_T8110_PARAMS3_VA_WIDTH_SHIFT) & DART_T8110_PARAMS3_WIDTH_MASK;
            UINT32 PaWidth = (Params3 >> DART_T8110_PARAMS3_PA_WIDTH_SHIFT) & DART_T8110_PARAMS3_WIDTH_MASK;
            UINT64 DramTop = DramBase + DramSize;
            UINT32 Tcr;

            DEBUG((DEBUG_INFO, "AppleDartIoMmuDxeInitialize: USB DART%d has no usable bypass "
                               "(PARAMS2=0x%x); building an identity map. VA_WIDTH=%d PA_WIDTH=%d\n",
                   DartIndex, Params2, VaWidth, PaWidth));

            //
            // Both widths have to name the top of DRAM or DVA == PA is not
            // expressible and there is no point continuing: a partial identity
            // map silently corrupts whatever DMA lands above the cutoff.
            //
            if(VaWidth < 64 && (DramTop - 1) >= (1ULL << VaWidth)) {
                DEBUG((DEBUG_ERROR, "%a: DART%d VA_WIDTH %d cannot address DRAM top 0x%llx\n",
                       __FUNCTION__, DartIndex, VaWidth, DramTop));
                continue;
            }
            if(PaWidth < 64 && (DramTop - 1) >= (1ULL << PaWidth)) {
                DEBUG((DEBUG_ERROR, "%a: DART%d PA_WIDTH %d cannot address DRAM top 0x%llx\n",
                       __FUNCTION__, DartIndex, PaWidth, DramTop));
                continue;
            }
            //
            // Three levels of table reach only 64GB of DVA. Anything above
            // that needs the fourth, and DRAM starts at 0x100_0000_0000.
            //
            if(VaWidth <= 36) {
                DEBUG((DEBUG_ERROR, "%a: DART%d VA_WIDTH %d has no four-level walk\n",
                       __FUNCTION__, DartIndex, VaWidth));
                continue;
            }

            if(IdentityMapRoot == 0) {
                IdentityMapRoot = AppleDartBuildIdentityMap(DramBase, DramSize);
                if(IdentityMapRoot == 0) {
                    DEBUG((DEBUG_ERROR, "%a: could not build the identity map; DART%d left blocked\n",
                           __FUNCTION__, DartIndex));
                    continue;
                }
            }

            //
            // Every DART instance shares one set of tables: they describe the
            // same identity, and a second copy would be 8MB spent to be able
            // to disagree with the first.
            //
            for(sid = 0; sid < DartInfo[DartIndex].Nsid; sid++) {
                MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_TTBR(DartInfo[DartIndex], sid, 0),
                            (UINT32)(((IdentityMapRoot >> DART_T8110_TTBR_ADDR_SHIFT)
                                      << DART_T8110_TTBR_ADDR_FIELD_SHIFT) & DART_T8110_TTBR_ADDR_MASK)
                                | DART_T8110_TTBR_VALID);
            }
            //
            // Streams have to be enabled or the DART never consults the TCR
            // it was just given. Enable exactly the SIDs that exist; J813
            // reports 16, so this is a single word of 0xffff.
            //
            for(i = 0; i * 32 < DartInfo[DartIndex].Nsid; i++) {
                INT32 Remaining = DartInfo[DartIndex].Nsid - i * 32;
                UINT32 StreamMask = (Remaining >= 32) ? ~0u : ((1u << Remaining) - 1u);
                MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_SID_ENABLE(DartInfo[DartIndex], i), StreamMask);
            }
            for(sid = 0; sid < DartInfo[DartIndex].Nsid; sid++) {
                MmioWrite32(DartInfo[DartIndex].BaseAddress + DART_TCR(DartInfo[DartIndex], sid),
                            DART_T8110_TCR_TRANSLATE_ENABLE | DART_T8110_TCR_FOUR_LEVEL);
            }
            DartInfo[DartIndex].TlbFlush((VOID *)&DartInfo[DartIndex]);

            Tcr = MmioRead32(DartInfo[DartIndex].BaseAddress + DART_TCR(DartInfo[DartIndex], 0));
            if(Tcr != (DART_T8110_TCR_TRANSLATE_ENABLE | DART_T8110_TCR_FOUR_LEVEL)) {
                DEBUG((DEBUG_ERROR, "%a: DART%d refused the four-level TCR (read 0x%x)\n",
                       __FUNCTION__, DartIndex, Tcr));
                continue;
            }

            DartInfo[DartIndex].IdentityMapped = TRUE;
            DartInfo[DartIndex].IdentityMapRoot = IdentityMapRoot;
            //
            // DVA == PA, so UEFI's no-IOMMU-protocol assumption of direct DMA
            // stays true and nothing downstream has to be taught about DARTs.
            //
            DEBUG((DEBUG_INFO, "AppleDartIoMmuDxeInitialize: USB DART%d identity-mapped, "
                               "root 0x%llx, TCR=0x%x, ERROR=0x%x\n",
                   DartIndex, IdentityMapRoot, Tcr,
                   MmioRead32(DartInfo[DartIndex].BaseAddress + DART_T8110_ERROR)));
        }
        else {
            DEBUG((DEBUG_ERROR, "AppleDartIoMmuDxeInitialize: USB DART%d can neither bypass nor "
                                "identity-map (compatible %a); it will block DMA\n",
                   DartIndex, CompatibleStr));
        }

        //
        // if there's no bypass mode available for this DART, set up translation.
        // Warning: might not work for some devices given page size shenanigans
        // This code is disabled for now while I work out why USB-A DARTs aren't working.
        //
        // Ntte = DIV_ROUND_UP(DartInfo->DmaVirtAddrEnd, DART_PAGE_SIZE);
        // NL2 = DIV_ROUND_UP(Ntte, DART_PAGE_SIZE / sizeof(UINT64));
        // NL1 = DIV_ROUND_UP(NL2, DART_PAGE_SIZE / sizeof(UINT64));

        // DartInfo->L2 = AllocateAlignedPages(NL2 * DART_PAGE_SIZE, DART_PAGE_SIZE);
        // memset(DartInfo->L2, 0, NL2 * DART_PAGE_SIZE);
        // WriteBackInvalidateDataCacheRange((VOID *)DartInfo->L2, NL2 * DART_PAGE_SIZE);

        // DartInfo->L1 = AllocateAlignedPages(NL1 * DART_PAGE_SIZE, DART_PAGE_SIZE);
        // memset(DartInfo->L1, 0, NL1 * DART_PAGE_SIZE);
        // L2 = (PHYSICAL_ADDRESS)DartInfo->L2;
        // for(i = 0; i < NL2; i++) {
        //     DartInfo->L1[i] = (L2 >> DartInfo->Shift) | DART_L1_TABLE;
        //     L2 += DART_PAGE_SIZE;
        // }
        // WriteBackInvalidateDataCacheRange((VOID *)DartInfo->L1, NL1 * DART_PAGE_SIZE);

        // for(sid = 0; sid < DartInfo->Nsid; sid++) {
        //     Address = (PHYSICAL_ADDRESS)DartInfo->L1;
        //     for(i = 0; i < NL1; i++) {
        //         MmioWrite32(DartInfo->BaseAddress + DART_TTBR(DartInfo, sid, i), (Address >> DART_TTBR_SHIFT | DartInfo->TtbrIsValid));
        //         Address += DART_PAGE_SIZE;
        //     }
        // }
        // DartInfo->TlbFlush((VOID *)DartInfo);

        // for(i = 0; i < DartInfo->Nsid / 32; i++) {
        //     MmioWrite32(DartInfo->BaseAddress + DART_SID_ENABLE(DartInfo, i), ~0);
        // }

        // for(sid = 0; sid < DartInfo->Nsid; sid++) {
        //     MmioWrite32(DartInfo->BaseAddress + DART_TCR(DartInfo, sid), DartInfo->TcrTranslateEnable);
        // }

        // return gBS->InstallMultipleProtocolInterfaces (
        //                 &ImageHandle,
        //                 &gEdkiiIoMmuProtocolGuid,
        //                 &mAppleDartIoMmuProtocol,
        //                 NULL
        //                 );
    }
    DEBUG((DEBUG_INFO, "All done\n"));
    return EFI_SUCCESS;
}
