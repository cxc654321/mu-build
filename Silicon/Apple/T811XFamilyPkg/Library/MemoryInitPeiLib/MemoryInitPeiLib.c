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
 * Copyright (c) 2026 Aurora Silicon
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

#include <AppendedRamdisk.h>

//Device memory map configuration file for UEFI (this is to help with pagetable initialization)
#include <Library/T811XFamilyVirtualMemoryMapDefines.h>

//
// T8112 uses 4 core MMIO range(s) + 1 PCIe range(s) + DRAM +
// framebuffer + terminator = 8 descriptors, plus headroom.
// Generated alongside the window list above -- keep the two in step.
//
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 12

#define DDR_ATTRIBUTES_CACHED           ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED         ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

STATIC CONST EFI_GUID  mNtasiAppendedRamdiskLocationHobGuid =
  NTASI_APPENDED_RAMDISK_LOCATION_HOB_GUID;

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
  CONST NTASI_APPENDED_RAMDISK_HEADER  *AppendedHeader;
  UINT64                              AppendedReservationSize;
  EFI_PHYSICAL_ADDRESS                AppendedTop;
  NTASI_APPENDED_RAMDISK_LOCATION     AppendedLocation;

  // build up virtual memory map
  BuildVirtualMemoryMap(&MemoryTable);

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

  //
  // m1n1 may place a page-sized NTASI header and a GPT/FAT ramdisk directly
  // after the declared FD region.  Keep that range out of the PEI/DXE memory
  // allocators and publish its physical location to BootRamdiskHelperDxe.
  //
  // This handoff already exists in the T602x MemoryInitPeiLib.  J813 uses the
  // separate T811X library, so without the equivalent code the valid appended
  // disk is silently ignored and BootRamdiskHelperDxe falls back to the small
  // FV-embedded HelloWorld disk.
  //
  AppendedReservationSize = 0;
  AppendedHeader = (CONST NTASI_APPENDED_RAMDISK_HEADER *)(UINTN)FdTop;
  if (AppendedHeader->Signature == NTASI_APPENDED_RAMDISK_SIGNATURE) {
    if (!NtasiValidateAppendedRamdisk (
           AppendedHeader,
           NTASI_APPENDED_RAMDISK_MAX_MAPPED_SPAN,
           FALSE,
           NULL,
           NULL,
           &AppendedReservationSize
           ))
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: invalid appended ramdisk header at 0x%lx\n", FdTop));
      return EFI_COMPROMISED_DATA;
    }

    AppendedTop = FdTop + AppendedReservationSize;
    if ((AppendedTop < FdTop) ||
        (AppendedTop > SystemMemoryTop) ||
        (AppendedReservationSize > MAX_UINT32))
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: appended ramdisk lies outside J813 system RAM\n"));
      return EFI_COMPROMISED_DATA;
    }

    ReserveMemoryRegion (FdTop, (UINT32)AppendedReservationSize);

    AppendedLocation.Signature             = NTASI_APPENDED_RAMDISK_LOCATION_SIGNATURE;
    AppendedLocation.Version               = NTASI_APPENDED_RAMDISK_LOCATION_VERSION;
    AppendedLocation.StructureSize         = sizeof (AppendedLocation);
    AppendedLocation.HeaderPhysicalAddress = FdTop;
    AppendedLocation.ReservationSize       = AppendedReservationSize;
    if (BuildGuidDataHob (
          &mNtasiAppendedRamdiskLocationHobGuid,
          &AppendedLocation,
          sizeof (AppendedLocation)
          ) == NULL)
    {
      DEBUG ((DEBUG_ERROR, "MemoryInitPeiLib: cannot publish appended ramdisk location HOB\n"));
      return EFI_OUT_OF_RESOURCES;
    }

    DEBUG ((
      DEBUG_INFO,
      "MemoryInitPeiLib: reserved appended ramdisk and published location HOB at 0x%lx (0x%lx bytes)\n",
      FdTop,
      AppendedReservationSize
      ));
  } else {
    DEBUG ((DEBUG_INFO, "MemoryInitPeiLib: no appended ramdisk header at FdTop 0x%lx\n", FdTop));
  }

  //reserve secondary stacks carveouts passed into cpm-impl-reg
  for(int i = 0; i < PcdGet32(PcdCoreCount); i++){
    CHAR8 CpuNodeName[14];
    UINTN CarveoutLength = 0;

    AsciiSPrint(CpuNodeName, ARRAY_SIZE(CpuNodeName), "/cpus/cpu%d", i);
    dt_node_t *CpuNode = dt_get(CpuNodeName);

    //
    // A cpu node can legitimately be absent, so PcdCoreCount is an upper bound
    // and not a promise.
    //
    // m1n1's run_guest.py deletes /cpus/cpuN outright for every core not listed
    // in -C ("Disabled cpu1" .. "Disabled cpu9" in its log), so a single-core
    // hypervisor boot leaves only cpu0 while PcdCoreCount is still 10.
    //
    // dt_get() returns NULL for a missing node and dt_node_prop(NULL, ...) walks
    // straight into dt_parse(NULL, ...), which reads node->nprop at offset 0.
    // That aborted the guest with FAR = 0x0 in _dt_parse+0x9c.
    //
    if (CpuNode == NULL) {
      DEBUG((DEBUG_INFO, "MemoryPeim: no %a node, skipping its carveout\n", CpuNodeName));
      continue;
    }

    UINT32 *Carveout = (UINT32 *)dt_node_prop(CpuNode, "cpm-impl-reg", &CarveoutLength);

    // dt_node_prop() only logs a missing property; it still returns NULL, and
    // the four words below are read unconditionally.
    if (Carveout == NULL || CarveoutLength < 4 * sizeof(UINT32)) {
      DEBUG((DEBUG_WARN, "MemoryPeim: %a has no usable cpm-impl-reg (len 0x%x), skipping\n",
             CpuNodeName, (UINT32)CarveoutLength));
      continue;
    }

    ReserveMemoryRegion (
      ((UINT64)Carveout[1] << 32) | Carveout[0],
      ((UINT64)Carveout[3] << 32) | Carveout[2]
    );
  }



  // Build Memory Allocation Hob
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

  //
  // MMIO - PMGR/AIC/core system peripherals, then PCIe.
  //
  // Ranges are derived from the J704 ADT (see the memory map header). Range 4
  // (0x380000000) is the one that matters for early bringup: it contains PMGR,
  // AIC, the watchdog, GPIO and UART0.
  //
#define MAP_DEVICE_RANGE(Base, Size)                                       \
  do {                                                                     \
    VirtualMemoryTable[Index].PhysicalBase = (Base);                       \
    VirtualMemoryTable[Index].VirtualBase  = (Base);                       \
    VirtualMemoryTable[Index].Length       = (Size);                       \
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE; \
    Index++;                                                               \
  } while (FALSE)

  //
  // Core system MMIO, generated from the device tree. See the memory map
  // header for how the windows were chosen.
  //
  MAP_DEVICE_RANGE (APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE,  APPLE_CORE_SYSTEM_MMIO_RANGE_1_SIZE);
  MAP_DEVICE_RANGE (APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE,  APPLE_CORE_SYSTEM_MMIO_RANGE_2_SIZE);
  MAP_DEVICE_RANGE (APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE,  APPLE_CORE_SYSTEM_MMIO_RANGE_3_SIZE);
  MAP_DEVICE_RANGE (APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE,  APPLE_CORE_SYSTEM_MMIO_RANGE_4_SIZE);

  //
  // PCIe: config space, then the BAR windows decoded from `ranges`. Without
  // these, enumerating the bus and placing a BAR would fault.
  //
  MAP_DEVICE_RANGE (APPLE_PCIE_MMIO_RANGE_1_BASE, APPLE_PCIE_MMIO_RANGE_1_SIZE);

#undef MAP_DEVICE_RANGE

  //
  // The loop above leaves Index one past the last written entry, whereas the
  // code below expects it to point *at* it. Step back to re-establish that.
  //
  Index--;

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
