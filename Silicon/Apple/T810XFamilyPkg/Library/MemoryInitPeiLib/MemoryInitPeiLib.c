/**
 * @file MemoryInitPeiLib.c
 * 
 * @author amarioguy (Arminder Singh)
 * 
 * This file implements page table setup, memory HOB setup, and MMU initialization.
 * Adapted from SurfaceDuoPkg/MemoryInitPeiLib.c
 * 
 * @version 1.0
 * @date 2022-07-31
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 * 
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 **/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/AppleDTLib.h>
#include <Guid/AppleAdtInfoHob.h>

//Device memory map configuration file for UEFI (this is to help with pagetable initialization)
#include <Library/T810XFamilyVirtualMemoryMapDefines.h>

#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 18

#define DDR_ATTRIBUTES_CACHED           ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED         ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

VOID BuildMemoryTypeInformationHob(VOID);

VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap);

STATIC VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{
    VOID *MemoryTranslationTableBase;
    UINTN MemoryTranslationTableSize;
    RETURN_STATUS StatusCode;

    DEBUG(
        (DEBUG_INFO, 
        "MemoryInitPeiLib: Enabling MMU, Page Table Base: 0x%p, Page Table Size: 0x%p\n", 
        &MemoryTranslationTableBase, &MemoryTranslationTableSize)
        );
    StatusCode = ArmConfigureMmu(MemoryTable, &MemoryTranslationTableBase, &MemoryTranslationTableSize);

    if(EFI_ERROR(StatusCode))
    {
        DEBUG((DEBUG_ERROR | DEBUG_INFO, "MemoryInitPeiLib: MMU enable failed!! Status: %llx\n", StatusCode));
    }
}

//borrowed from edk2-platforms:Armada7k8kMemoryInitPeiLib
STATIC VOID ReserveMemoryRegion ( IN EFI_PHYSICAL_ADDRESS ReservedRegionBase, IN UINT32 ReservedRegionSize)
{
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  EFI_PHYSICAL_ADDRESS         ReservedRegionTop;
  EFI_PHYSICAL_ADDRESS         ResourceTop;
  EFI_PEI_HOB_POINTERS         NextHob;
  UINT64                       ResourceLength;

  ReservedRegionTop = ReservedRegionBase + ReservedRegionSize;

  //
  // Search for System Memory Hob that covers the reserved region,
  // and punch a hole in it
  //
  for (NextHob.Raw = GetHobList ();
       NextHob.Raw != NULL;
       NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
                                 NextHob.Raw)) {

    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (ReservedRegionBase >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (ReservedRegionTop <= NextHob.ResourceDescriptor->PhysicalStart +
                      NextHob.ResourceDescriptor->ResourceLength))
    {
      ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
      ResourceLength = NextHob.ResourceDescriptor->ResourceLength;
      ResourceTop = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

      if (ReservedRegionBase == NextHob.ResourceDescriptor->PhysicalStart) {
        //
        // This region starts right at the start of the reserved region, so we
        // can simply move its start pointer and reduce its length by the same
        // value
        //
        NextHob.ResourceDescriptor->PhysicalStart += ReservedRegionSize;
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else if ((NextHob.ResourceDescriptor->PhysicalStart +
                  NextHob.ResourceDescriptor->ResourceLength) ==
                  ReservedRegionTop) {

        //
        // This region ends right at the end of the reserved region, so we
        // can simply reduce its length by the size of the region.
        //
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else {
        //
        // This region covers the reserved region. So split it into two regions,
        // each one touching the reserved region at either end, but not covering
        // it.
        //
        NextHob.ResourceDescriptor->ResourceLength =
                 ReservedRegionBase - NextHob.ResourceDescriptor->PhysicalStart;

        // Create the System Memory HOB for the remaining region (top of the FD)
        BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY,
                                    ResourceAttributes,
                                    ReservedRegionTop,
                                    ResourceTop - ReservedRegionTop);
      }

      //
      // Reserve the memory space.
      //
      BuildResourceDescriptorHob (EFI_RESOURCE_MEMORY_RESERVED,
        0,
        ReservedRegionBase,
        ReservedRegionSize);

      break;
    }
    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }
}


// Reserve the copied pages, not a guessed maximum ADT allocation. These pages
// were excluded from the entire PHIT arena by EarlySetup's copy planner.
STATIC BOOLEAN AdtPagesValid (UINT64 Base, UINT64 Size,
                              UINT64 RamBase, UINT64 RamSize,
                              UINT64 ArenaBase, UINT64 ArenaSize)
{
  if (!Size || (Base & 4095) || (Size & 4095) ||
      Base < RamBase || Size > RamSize || Base - RamBase > RamSize - Size ||
      Base > MAX_UINT64 - Size || ArenaBase > MAX_UINT64 - ArenaSize)
    return FALSE;
  return !(Base < ArenaBase + ArenaSize && ArenaBase < Base + Size);
}

STATIC BOOLEAN AdtPagesAvailable (UINT64 Base, UINT64 Size)
{
  EFI_PEI_HOB_POINTERS Hob;
  UINT64 End = Base + Size;
  BOOLEAN Covered = FALSE;
  for (Hob.Raw = GetHobList (); !END_OF_HOB_LIST (Hob); Hob.Raw = GET_NEXT_HOB (Hob)) {
    if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_MEMORY_ALLOCATION) {
      UINT64 Start = Hob.MemoryAllocation->AllocDescriptor.MemoryBaseAddress;
      UINT64 Length = Hob.MemoryAllocation->AllocDescriptor.MemoryLength;
      if (Length > MAX_UINT64 - Start || (Base < Start + Length && Start < End)) return FALSE;
    }
    if (GET_HOB_TYPE (Hob) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      UINT64 Start = Hob.ResourceDescriptor->PhysicalStart;
      UINT64 Length = Hob.ResourceDescriptor->ResourceLength;
      if (Length > MAX_UINT64 - Start) return FALSE;
      if (Base < Start + Length && Start < End) {
        if (Hob.ResourceDescriptor->ResourceType != EFI_RESOURCE_SYSTEM_MEMORY) return FALSE;
        if (Base >= Start && End <= Start + Length) Covered = TRUE;
      }
    }
  }
  return Covered;
}

STATIC EFI_STATUS ReserveAdtCopy (UINT64 ArenaBase, UINT64 ArenaSize)
{
  VOID *Hob;
  APPLE_ADT_INFO_HOB *Info;
  UINT64 RamBase = PcdGet64 (PcdSystemMemoryBase);
  UINT64 RamSize = PcdGet64 (PcdSystemMemorySize);
  Hob = GetFirstGuidHob (&gAppleSiliconPkgAdtInfoHobGuid);
  // Legacy producers do not gain a keyboard capability, but still boot.
  if (Hob == NULL) return EFI_SUCCESS;
  if (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (*Info)) return EFI_COMPROMISED_DATA;
  Info = GET_GUID_HOB_DATA (Hob);
  if (Info->Version != APPLE_ADT_INFO_VERSION || Info->Flags != 0 ||
      !AdtPagesValid (Info->AdtPagesBase, Info->AdtPagesSize, RamBase, RamSize, ArenaBase, ArenaSize) ||
      !AdtPagesValid (Info->BootArgsPagesBase, Info->BootArgsPagesSize, RamBase, RamSize, ArenaBase, ArenaSize) ||
      !Info->AdtSize || Info->AdtBase < Info->AdtPagesBase ||
      Info->AdtSize > Info->AdtPagesSize ||
      Info->AdtBase - Info->AdtPagesBase > Info->AdtPagesSize - Info->AdtSize ||
      (Info->AdtPagesBase < Info->BootArgsPagesBase + Info->BootArgsPagesSize &&
       Info->BootArgsPagesBase < Info->AdtPagesBase + Info->AdtPagesSize))
    return EFI_COMPROMISED_DATA;
  if (!AdtPagesAvailable (Info->AdtPagesBase, Info->AdtPagesSize) ||
      !AdtPagesAvailable (Info->BootArgsPagesBase, Info->BootArgsPagesSize))
    return EFI_COMPROMISED_DATA;
  // BuildMemoryAllocationHob is void/assert-on-exhaustion in PrePiHobLib.
  // Check PHIT space for both allocations and the terminating HOB first.
  {
    EFI_HOB_HANDOFF_INFO_TABLE *Phit = GetHobList ();
    UINT64 Need = 2 * sizeof (EFI_HOB_MEMORY_ALLOCATION) + sizeof (EFI_HOB_GENERIC_HEADER);
    if (Phit->EfiFreeMemoryTop < Phit->EfiFreeMemoryBottom ||
        Phit->EfiFreeMemoryTop - Phit->EfiFreeMemoryBottom < Need)
      return EFI_OUT_OF_RESOURCES;
  }
  BuildMemoryAllocationHob (Info->AdtPagesBase, Info->AdtPagesSize, EfiBootServicesData);
  BuildMemoryAllocationHob (Info->BootArgsPagesBase, Info->BootArgsPagesSize, EfiBootServicesData);
  Info->Flags = APPLE_ADT_INFO_RESERVED;
  return EFI_SUCCESS;
}

//Borrowed from ArmPlatformPkg
EFI_STATUS EFIAPI MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{
  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable;
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  UINT64                        ResourceLength;
  EFI_PEI_HOB_POINTERS          NextHob;
  EFI_PHYSICAL_ADDRESS          FdTop;
  EFI_PHYSICAL_ADDRESS          SystemMemoryTop;
  EFI_PHYSICAL_ADDRESS          ResourceTop;
  BOOLEAN                       Found;

  EFI_STATUS                    AdtStatus;

  MemoryTable = NULL;

  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);

  //
  // Now, the permanent memory has been installed, we can call AllocatePages()
  //
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );

  //
  // Check if the resource for the main system memory has been declared
  //
  Found       = FALSE;
  NextHob.Raw = GetHobList ();
  while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (PcdGet64 (PcdSystemMemoryBase) >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength <= PcdGet64 (PcdSystemMemoryBase) + PcdGet64 (PcdSystemMemorySize)))
    {
      Found = TRUE;
      break;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  if (!Found) {
    // Reserved the memory space occupied by the firmware volume
    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttributes,
      PcdGet64 (PcdSystemMemoryBase),
      PcdGet64 (PcdSystemMemorySize)
      );
  }

  //
  // Reserved the memory space occupied by the firmware volume
  //

  SystemMemoryTop = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemoryBase) + (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemorySize);
  FdTop           = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdFdBaseAddress) + (EFI_PHYSICAL_ADDRESS)PcdGet32 (PcdFdSize);

  // EDK2 does not have the concept of boot firmware copied into DRAM. To avoid the DXE
  // core to overwrite this area we must create a memory allocation HOB for the region,
  // but this only works if we split off the underlying resource descriptor as well.
  if ((PcdGet64 (PcdFdBaseAddress) >= PcdGet64 (PcdSystemMemoryBase)) && (FdTop <= SystemMemoryTop)) {
    Found = FALSE;

    // Search for System Memory Hob that contains the firmware
    NextHob.Raw = GetHobList ();
    while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
      if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
          (PcdGet64 (PcdFdBaseAddress) >= NextHob.ResourceDescriptor->PhysicalStart) &&
          (FdTop <= NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength))
      {
        ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
        ResourceLength     = NextHob.ResourceDescriptor->ResourceLength;
        ResourceTop        = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

        if (PcdGet64 (PcdFdBaseAddress) == NextHob.ResourceDescriptor->PhysicalStart) {
          if (SystemMemoryTop != FdTop) {
            // Create the System Memory HOB for the firmware
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              PcdGet64 (PcdFdBaseAddress),
              PcdGet32 (PcdFdSize)
              );

            // Top of the FD is system memory available for UEFI
            NextHob.ResourceDescriptor->PhysicalStart  += PcdGet32 (PcdFdSize);
            NextHob.ResourceDescriptor->ResourceLength -= PcdGet32 (PcdFdSize);
          }
        } else {
          // Create the System Memory HOB for the firmware
          BuildResourceDescriptorHob (
            EFI_RESOURCE_SYSTEM_MEMORY,
            ResourceAttributes,
            PcdGet64 (PcdFdBaseAddress),
            PcdGet32 (PcdFdSize)
            );

          // Update the HOB
          NextHob.ResourceDescriptor->ResourceLength = PcdGet64 (PcdFdBaseAddress) - NextHob.ResourceDescriptor->PhysicalStart;

          // If there is some memory available on the top of the FD then create a HOB
          if (FdTop < NextHob.ResourceDescriptor->PhysicalStart + ResourceLength) {
            // Create the System Memory HOB for the remaining region (top of the FD)
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              FdTop,
              ResourceTop - FdTop
              );
          }
        }

        // Mark the memory covering the Firmware Device as boot services data
        BuildMemoryAllocationHob (
          PcdGet64 (PcdFdBaseAddress),
          PcdGet32 (PcdFdSize),
          EfiBootServicesData
          );

        Found = TRUE;
        break;
      }

      NextHob.Raw = GET_NEXT_HOB (NextHob);
    }

    ASSERT (Found);
  }
  
  //reserve secondary stacks carveouts passed into cpm-impl-reg 
  for(int i = 0; i < PcdGet32(PcdCoreCount); i++){
    CHAR8 CpuNodeName[14];
    UINTN CarveoutLength = 0;

    AsciiSPrint(CpuNodeName, ARRAY_SIZE(CpuNodeName), "/cpus/cpu%d", i);
    dt_node_t *CpuNode = dt_get(CpuNodeName);
    UINT32 *Carveout;
    if (CpuNode == NULL) continue;
    Carveout = (UINT32 *)dt_node_prop(CpuNode, "cpm-impl-reg", &CarveoutLength);
    if (Carveout == NULL || CarveoutLength < 16 || Carveout[3] != 0 || Carveout[2] == 0) continue;

    ReserveMemoryRegion (
      ((UINT64)Carveout[1] << 32) | Carveout[0],
      ((UINT64)Carveout[3] << 32) | Carveout[2]
    );
  }



  // SystemMemory resources exist; reserve before ANY page-table allocation.
  AdtStatus = ReserveAdtCopy (UefiMemoryBase, UefiMemorySize);
  if (EFI_ERROR (AdtStatus)) return AdtStatus;
  BuildVirtualMemoryMap (&MemoryTable);
  if (MemoryTable == NULL) return EFI_OUT_OF_RESOURCES;
  InitMmu (MemoryTable);

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}

/**
 * BuildVirtualMemoryMap
 * 
 * This will build up the memory map of the platform used to initialize the MMU and page tables.
 * 
 * @param VirtualMemoryMap - A pointer to a pointer to be used for page table setup.
 * 
 * does not return anything as the value will be stored in a pointer accessible by MemoryPeim.
 * 
 */
VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap)
{
  ARM_MEMORY_REGION_ATTRIBUTES CacheAttributes;
  UINTN Index = 0;
  ARM_MEMORY_REGION_DESCRIPTOR *VirtualMemoryTable;

  //ensure we actually have a valid memory map pointer
  ASSERT(VirtualMemoryMap != NULL);

  DEBUG((DEBUG_INFO, "Allocating virtual memory table pages\n"));
  VirtualMemoryTable = (ARM_MEMORY_REGION_DESCRIPTOR *)AllocatePages (EFI_SIZE_TO_PAGES (sizeof (ARM_MEMORY_REGION_DESCRIPTOR) * MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS));
  if (VirtualMemoryTable == NULL) {
    DEBUG((DEBUG_INFO, "Unexpected failure to allocate VirtualMemoryTable\n"));
    return;
  }

  CacheAttributes = DDR_ATTRIBUTES_CACHED;

  /**
   * NOTE - On Apple silicon platforms, non PCIe MMIO regions *must* use nGnRnE mappings, 
   * while all PCIe regions *must* use nGnRE mappings.
   * by default EDK2 sets up the MMIO as nGnRnE, good for core system devices
   * though we will need to add an attribute for nGnRE mappings at some point.
   * 
   * TODO: add ARM_MEMORY_REGION_ATTRIBUTE_DEVICE_POSTED_WRITE
   **/

  //MMIO - PMGR/AIC/Core System Peripherals and PCIe
  VirtualMemoryTable[Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].VirtualBase  = APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].Length       = APPLE_CORE_SYSTEM_MMIO_RANGE_1_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_2_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_1_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_2_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_3_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_3_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_4_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_4_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_5_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_5_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_6_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_6_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_7_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_7_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //System DRAM
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdSystemMemorySize);
  VirtualMemoryTable[Index].Attributes     = CacheAttributes;


  DEBUG ((
    DEBUG_ERROR,
    "%a: Dumping System DRAM Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n"
    "\tTop of system RAM: 0x%lX\n",
    __FUNCTION__,
    VirtualMemoryTable[Index].PhysicalBase,
    VirtualMemoryTable[Index].VirtualBase,
    VirtualMemoryTable[Index].Length,
    VirtualMemoryTable[Index].PhysicalBase + VirtualMemoryTable[Index].Length
    ));

  //Framebuffer
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdFrameBufferSize);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;

  // End of Table
  VirtualMemoryTable[++Index].PhysicalBase  = 0;
  VirtualMemoryTable[Index].VirtualBase     = 0;
  VirtualMemoryTable[Index].Length          = 0;
  VirtualMemoryTable[Index].Attributes      = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT((Index + 1) <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  *VirtualMemoryMap = VirtualMemoryTable;
}