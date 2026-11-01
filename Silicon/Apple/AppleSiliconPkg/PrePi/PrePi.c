/**
 * PrePi.c
 * 
 * Main implementation of SEC phase
 * 
 * PEI is not necessary in this implementation, so build up HOBs and jump right to DXE.
 * 
 * TODO: Adapt code to dynamically change PcdSystemMemorySize to use the values from FDT instead of
 * hardcoding values.
 * 
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */


#include "PrePi.h"
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <PiDxe.h>
#include <PiPei.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PeCoffLib.h>
#include <Library/PeCoffGetEntryPointLib.h>
#include <Library/PerformanceLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/PrePiLib.h>
#include <Library/SerialPortLib.h>
#include <Guid/AppleFdInfoHob.h>

UINT32 InitializeUART(VOID);

VOID EFIAPI ProcessLibraryConstructorList(VOID);

// Non-trapping breadcrumbs for very early J813 bring-up.  BRK cannot be used
// here because PrePi is still running at EL2 and the installed m1n1 monitor
// cannot safely resume that context after proxying the exception to the host.
// The magic makes the adjacent stage word discoverable in the raw FD image.
typedef struct {
  UINT64          Magic;
  volatile UINT64 Stage;
} MU_CHECKPOINT_RECORD;

volatile MU_CHECKPOINT_RECORD gMuCheckpoint = {
  0x554D41524F525541ULL, // "AURORAMU"
  0
};

extern volatile UINT64 gMuFrameBufferBase;
extern volatile UINT32 gMuFrameBufferStride;
extern volatile UINT32 gMuFrameBufferWidth;
extern volatile UINT32 gMuFrameBufferHeight;

STATIC
VOID
MuVisualCheckpoint (
  IN UINT64 Stage
  )
{
  STATIC CONST UINT32 Colors[] = {
    0x00FFFFFF, // PrePi Main
    0x0000FFFF, // HOB constructor
    0x0000FF00, // MemoryPeim
    0x00FFFF00, // PlatformPeim
    0x00FF8000, // Library constructors
    0x00FF00FF, // FV decompressed
    0x00FF0000  // Entering DXE Core
  };
  UINTN          Index;
  UINTN          RowIndex;
  UINTN          X;
  UINTN          Y;
  UINTN          XStart;
  UINTN          YStart;
  volatile UINT32 *Row;

  if ((gMuFrameBufferBase == 0) ||
      (gMuFrameBufferStride < sizeof (UINT32)) ||
      (gMuFrameBufferWidth < 448) ||
      (gMuFrameBufferHeight < 96)) {
    return;
  }

  if ((Stage >= 0x10) && (Stage <= 0x16)) {
    Index    = (UINTN)(Stage - 0x10);
    RowIndex = 0;
  } else if ((Stage >= 0x20) && (Stage <= 0x26)) {
    Index    = (UINTN)(Stage - 0x20);
    RowIndex = 1;
  } else {
    return;
  }

  XStart = 24 + (Index * 56);
  YStart = 24 + (RowIndex * 32);

  // Two rows of seven 48x24 blocks in the upper-left corner. The first row is
  // PrePi; the second row is the DXE loader and entry-point handoff.
  for (Y = YStart; Y < (YStart + 24); Y++) {
    Row = (volatile UINT32 *)(UINTN)(
                                  gMuFrameBufferBase +
                                  (Y * gMuFrameBufferStride)
                                  );
    for (X = XStart; X < (XStart + 48); X++) {
      Row[X] = Colors[Index];
    }
  }

  ArmDataMemoryBarrier ();
}

STATIC
VOID
MuCheckpoint (
  IN UINT64 Stage
  )
{
  gMuCheckpoint.Stage = Stage;
  ArmDataMemoryBarrier ();
  MuVisualCheckpoint (Stage);
}

VOID Main(IN VOID *StackBase, IN UINTN StackSize, IN VOID *DeviceTreePtr, IN UINT64 UefiMemoryBase)
{
    EFI_HOB_HANDOFF_INFO_TABLE  *HobList;
    EFI_STATUS Status;

    SetPrePiProgressCallback (MuVisualCheckpoint);
    MuCheckpoint(0x10);

    UINTN UefiMemoryLength = FixedPcdGet32(PcdSystemMemoryUefiRegionSize);

    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD,
        "Flattened Device Tree Pointer: 0x%p\n",
        DeviceTreePtr));

    DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx, Stack Base = 0x%p, Stack "
       "Size = 0x%llx\n",
       UefiMemoryBase, UefiMemoryLength, StackBase, StackSize));
    
    //Set the FdtPointer PCD to the set location we copied the FDT to during ModuleEntryPoint
    //PatchPcdSet64(PcdAdtPointer, (UINT64)DeviceTreePtr);//TODO: why it crashes and is it needed


    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Setting up DXE Hand-Off Blocks.\n"));

    HobList = HobConstructor(
        (VOID *)UefiMemoryBase,
        UefiMemoryLength,
        (VOID *)UefiMemoryBase,
        StackBase
    );

    MuCheckpoint(0x11);

    PrePeiSetHobList(HobList);

    //set up memory HOBs
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Invalidating data cache.\n"));

    InvalidateDataCacheRange((VOID *)(UINTN)PcdGet64(PcdFdBaseAddress), PcdGet32(PcdFdSize));    

    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD, 
        "Beginning memory hand off block setup.\n"));
    Status = MemoryPeim(UefiMemoryBase, UefiMemoryLength);
    if(EFI_ERROR(Status))
    {
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "PrePi: Memory setup failed! Status: 0x%llx \n",
            Status)
            );
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "Looping forever, check serial log for failure point\n")
            );
        DEBUG(
            (DEBUG_ERROR | DEBUG_INFO | DEBUG_LOAD,
            "Issue reboot command through hypervisor shell, JTAG interface, or send VDM command to reboot\n")
            );
        CpuDeadLoop();
    }

    MuCheckpoint(0x12);

    //
    // Publish where m1n1 actually loaded us, for consumers in later phases.
    //
    // PcdFdBaseAddress / PcdFdSize are [PcdsPatchableInModule]: this module's copy
    // is patched to the real address, but every other module links the FDF's
    // build-time defaults (0x830000000 / 0x1E00000), which point into MMIO on this
    // platform. BootRamdiskHelperDxe derived an address from those PCDs and took
    // an SError on the first read. Anything outside PrePi must use this HOB.
    //
    {
        APPLE_FD_INFO_HOB FdInfo;

        FdInfo.FdBase = PcdGet64(PcdFdBaseAddress);
        FdInfo.FdSize = PcdGet32(PcdFdSize);
        DEBUG((EFI_D_INFO | EFI_D_LOAD,
               "Publishing FD info HOB: base 0x%llx size 0x%llx\n",
               FdInfo.FdBase, FdInfo.FdSize));
        BuildGuidDataHob(&gAppleSiliconPkgFdInfoHobGuid, &FdInfo, sizeof(FdInfo));
    }

    //set up stack and CPU HOBs
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Building up Stack/CPU HOBs\n"));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Stack Base: 0x%llx, Stack Size: 0x%llx\n", (UINT64)StackBase, StackSize));
    BuildStackHob((UINT64)StackBase, StackSize);
    BuildCpuHob(ArmGetPhysicalAddressBits(), PcdGet8(PcdPrePiCpuIoSize));

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Setting boot mode to default settings...\n"));
    SetBootMode(BOOT_WITH_DEFAULT_SETTINGS);

    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Starting PlatformPeim\n"));
    Status = PlatformPeim();
    ASSERT_EFI_ERROR(Status);

    MuCheckpoint(0x13);

    // SEC phase needs to run library constructors by hand.
    ProcessLibraryConstructorList();
    MuCheckpoint(0x14);

    // Assume the FV that contains the PI (our code) also contains a compressed
    // FV.
    DEBUG((DEBUG_INFO, "Decompressing FV...\n"));
    Status = DecompressFirstFv();
    ASSERT_EFI_ERROR(Status);
    MuCheckpoint(0x15);

    // Load the DXE Core and transfer control to it
    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD,
        "Loading DXE Core now\n")
        );
    MuCheckpoint(0x16);
    Status = LoadDxeCoreFromFv(NULL, 0);
    ASSERT_EFI_ERROR(Status);
    
    //if we reach here, something has *seriously* gone wrong
    CpuDeadLoop();
}

VOID CEntryPoint(IN VOID *StackBase, IN UINTN StackSize, IN VOID *DeviceTreePtr, IN UINT64 UefiMemoryBase)
{
    Main(StackBase, StackSize, DeviceTreePtr, UefiMemoryBase);
    //DXE Core should not return, if it does, something is *very* wrong
    ASSERT(FALSE);
}


UINT32 InitializeUART(VOID)
{
    SerialPortInitialize();
    DEBUG(
        (EFI_D_INFO | EFI_D_LOAD, 
        "Apple Silicon Project Mu Firmware (arm64/arm64e)\n")
        );
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "If you can see this message, UART works\n"));
    //
    // Hand-bumped build marker. Successive .fd builds are byte-identical in size
    // and often load every module at the same address, so a log alone cannot tell
    // you which firmware actually ran -- three separate debugging rounds were
    // spent reasoning about code that was not in the image under test. Bump this
    // whenever handing a new .fd over, and grep the log for it before trusting
    // anything else in that log.
    //
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "J704 firmware build marker: J704-FW-15\n"));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "FD Base Address - 0x%llx\n", PcdGet64(PcdFdBaseAddress)));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "FV Base Address - 0x%llx\n", PcdGet64(PcdFvBaseAddress)));
    DEBUG((EFI_D_INFO | EFI_D_LOAD, "Current ADT Pointer: 0x%llx\n", PcdGet64(PcdAdtPointer)));
    return EFI_SUCCESS;
}

//borrowed from ArmVirtPkg/PrePi/PrePi.c
VOID
RelocatePeCoffImage (
  IN  EFI_PEI_FV_HANDLE         FwVolHeader,
  IN  PE_COFF_LOADER_READ_FILE  ImageRead
  )
{
  EFI_PEI_FILE_HANDLE           FileHandle;
  VOID                          *SectionData;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  EFI_STATUS                    Status;

  FileHandle = NULL;
  Status     = FfsFindNextFile (
                 EFI_FV_FILETYPE_SECURITY_CORE,
                 FwVolHeader,
                 &FileHandle
                 );
  ASSERT_EFI_ERROR (Status);

  Status = FfsFindSectionData (EFI_SECTION_PE32, FileHandle, &SectionData);
  if (EFI_ERROR (Status)) {
    Status = FfsFindSectionData (EFI_SECTION_TE, FileHandle, &SectionData);
  }

  ASSERT_EFI_ERROR (Status);

  ZeroMem (&ImageContext, sizeof ImageContext);

  ImageContext.Handle    = (EFI_HANDLE)SectionData;
  ImageContext.ImageRead = ImageRead;
  PeCoffLoaderGetImageInfo (&ImageContext);

  if (ImageContext.ImageAddress != (UINTN)SectionData) {
    ImageContext.ImageAddress = (UINTN)SectionData;
    PeCoffLoaderRelocateImage (&ImageContext);
  }
}
