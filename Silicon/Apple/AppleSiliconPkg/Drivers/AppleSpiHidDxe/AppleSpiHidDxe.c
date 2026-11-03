/** @file
 *  AppleSpiHidDxe - internal keyboard/trackpad transport survey for M1 (J313).
 *
 *  The J313 ADT has no MTP node, no SPI-HID-less keyboard and no boot-protocol
 *  USB keyboard: the built image enumerates pointing devices but never a
 *  keyboard, which is exactly why BDS reports
 *      [Bds] Connect ConIn failed - Not Found!!!
 *  and the UI it just started cannot be driven.
 *
 *  M1 puts the internal keyboard and trackpad behind Apple SPI-HID:
 *
 *      spi@23510c000            compatible "apple,t8103-spi", "apple,spi"
 *        hid-transport@0        compatible "apple,spi-hid-transport"
 *
 *  That is the transport Linux drives with spi-hid-core plus spi-hid-apple.
 *  Nothing in this tree implements either half yet, so the real driver is
 *  three pieces of work: an Apple SPI controller, the SPI-HID message
 *  protocol, and HID report decoding into SimpleTextInputEx.
 *
 *  THIS STAGE DOES NOT PRODUCE INPUT AND DELIBERATELY WRITES NOTHING.
 *
 *  How much of that work is actually required depends on one unmeasured fact:
 *  whether iBoot leaves spi3 powered, clocked and streaming. Unlike the MTP
 *  survey (AppleMtpHidDxe) the answer is cheap to measure, because the ADT
 *  nodes are reachable directly instead of through a coprocessor handshake:
 *
 *    - Controller alive and FIFO moving  -> reader plus framing. Moderate.
 *    - Controller dead                   -> PMGR power domain, clock gate and
 *                                           pinctrl first, then the above. Large.
 *
 *  So this driver only locates the nodes by compatible string, prints the
 *  controller register state it finds, and watches the RX FIFO while the
 *  operator touches the trackpad. It writes no SPI register, starts nothing
 *  and programs no clock. Register layout is from m1n1
 *  (proxyclient/m1n1/hw/spi.py, class SPIRegs), the only public description
 *  of this block.
 *
 *  Copyright (c) 2026 Aurora Silicon
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>

#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

//
// Apple SPI -- proxyclient/m1n1/hw/spi.py, class SPIRegs.
//
#define SPI_CTRL      0x000
#define SPI_CFG       0x004
#define SPI_STATUS    0x008
#define SPI_PIN       0x00C
#define SPI_RXDATA    0x020
#define SPI_CLKDIV    0x030
#define SPI_RXCNT     0x034
#define SPI_INTER_DELAY 0x038
#define SPI_TXCNT     0x04C
#define SPI_FIFOSTAT  0x10C
#define SPI_IE_XFER   0x130
#define SPI_IF_XFER   0x134
#define SPI_SHIFTCFG  0x150
#define SPI_PINCFG    0x154
#define SPI_DELAY_PRE 0x160
#define SPI_SCKCFG    0x164
#define SPI_XFSTATUS  0x1C0

//
// R_FIFOSTAT: LEVEL_RX is bits 31:24, RX_EMPTY bit 20, LEVEL_TX 15:8, TX_FULL bit 4.
//
#define SPI_FIFOSTAT_LEVEL_RX(v)  (((v) >> 24) & 0xFF)

//
// How long to watch the RX FIFO, and how often to sample. The operator should
// touch the trackpad during this window; that is the only input this machine
// can produce without the driver this survey is meant to size.
//
#define SPI_WATCH_MILLISECONDS   8000
#define SPI_WATCH_INTERVAL_MS    100

STATIC dt_node_t  *mHidTransportNode;
STATIC dt_node_t  *mSpiNode;

/**
  Return TRUE when the NUL-separated string list Val/Len contains Target.
**/
STATIC
BOOLEAN
ListContains (
  IN CONST CHAR8  *Val,
  IN UINTN        Len,
  IN CONST CHAR8  *Target
  )
{
  UINTN  Offset;
  UINTN  TargetLen;

  TargetLen = AsciiStrLen (Target);

  for (Offset = 0; Offset < Len; ) {
    CONST CHAR8  *Entry;
    UINTN        EntryLen;

    Entry    = Val + Offset;
    EntryLen = AsciiStrnLenS (Entry, Len - Offset);
    if (EntryLen >= TargetLen) {
      if (AsciiStrnCmp (Entry, Target, TargetLen) == 0) {
        return TRUE;
      }
    }

    Offset += EntryLen + 1;
  }

  return FALSE;
}

/**
  Property visitor: record the SPI-HID transport node and the first SPI
  controller. Returning 0 keeps the walk going.
**/
STATIC
int
EFIAPI
SpiHidPropCallback (
  void        *Arg,
  dt_node_t   *Node,
  int         Depth,
  const char  *Key,
  void        *Val,
  size_t      Len
  )
{
  (VOID)Arg;
  (VOID)Depth;

  if ((Key == NULL) || (Val == NULL)) {
    return 0;
  }

  if (AsciiStrCmp (Key, "compatible") != 0) {
    return 0;
  }

  if ((mHidTransportNode == NULL) &&
      ListContains ((CONST CHAR8 *)Val, Len, "apple,spi-hid-transport"))
  {
    mHidTransportNode = Node;
  }

  return 0;
}

/**
  Node visitor. It has to exist because dt_parse calls it for every node.
**/
STATIC
int
EFIAPI
SpiHidNodeCallback (
  void       *Arg,
  dt_node_t  *Node,
  int        Depth
  )
{
  (VOID)Arg;
  (VOID)Node;
  (VOID)Depth;

  return 0;
}

/**
  Read-only survey. See the file header for why nothing is written.
**/
EFI_STATUS
EFIAPI
AppleSpiHidDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  uint64_t    Base;
  uint64_t    Size;
  UINT32      Cfg;
  UINT32      Control;
  UINT32      StatusReg;
  UINT32      FifoStat;
  UINT32      RxCount;
  UINT32      Baseline;
  UINT32      Peak;
  UINT32      Samples;
  UINT32      Level;
  UINT32      Index;
  BOOLEAN     Changed;
  dt_node_t   *Root;

  DEBUG ((DEBUG_ERROR, "SPIHID: ===== internal keyboard/trackpad (SPI-HID) survey, stage 1 =====\n"));
  DEBUG ((DEBUG_ERROR, "SPIHID: read-only: no register is written, nothing is started\n"));

  //
  // 1. Locate the nodes in the live ADT. Matching on compatible keeps this
  //    independent of the ADT's own naming, which has moved between SoCs.
  //
  Root = (dt_node_t *)(UINTN)FixedPcdGet64 (PcdAdtPointer);
  if (Root == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: no ADT pointer; cannot survey\n"));
    return EFI_NOT_FOUND;
  }

  dt_parse (
    Root,
    0,
    NULL,
    SpiHidNodeCallback,
    NULL,
    SpiHidPropCallback,
    NULL
    );

  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: spi-hid-transport node: %a, spi controller: %a\n",
    mHidTransportNode != NULL ? "FOUND" : "ABSENT",
    mSpiNode != NULL ? "FOUND" : "ABSENT"
    ));

  //
  // Only our own parent controller may be probed. Matching "apple,spi" blindly
  // would pick whichever controller the ADT lists first -- on this machine that
  // is spi0, not spi3 -- and reading an unrelated, unpowered block is exactly
  // the kind of stray MMIO that has hung this platform before.
  //
  if (mHidTransportNode == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: no SPI-HID transport in this ADT; not probing any controller\n"));
    return EFI_NOT_FOUND;
  }

  mSpiNode = dt_node_parent (mHidTransportNode);
  if (mSpiNode == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: SPI-HID transport has no parent controller\n"));
    return EFI_NOT_FOUND;
  }

  //
  // The SPI-HID child carries the chip select, not the registers; the MMIO
  // window belongs to its parent controller.
  //
  if (dt_node_reg (mSpiNode, 0, &Base, &Size) != 0) {
    DEBUG ((DEBUG_ERROR, "SPIHID: SPI controller has no reg[0]\n"));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_ERROR, "SPIHID: SPI controller reg[0] = 0x%lx size 0x%lx\n", Base, Size));

  //
  // 2. What state is the block in? A handful of reads tells us whether the
  //    controller was left programmed by iBoot, which is the whole question.
  //
  Control   = MmioRead32 ((UINTN)Base + SPI_CTRL);
  Cfg       = MmioRead32 ((UINTN)Base + SPI_CFG);
  StatusReg = MmioRead32 ((UINTN)Base + SPI_STATUS);
  FifoStat  = MmioRead32 ((UINTN)Base + SPI_FIFOSTAT);
  RxCount   = MmioRead32 ((UINTN)Base + SPI_RXCNT);

  DEBUG ((DEBUG_ERROR, "SPIHID: CTRL=0x%08x CFG=0x%08x STATUS=0x%08x\n", Control, Cfg, StatusReg));
  DEBUG ((DEBUG_ERROR, "SPIHID: FIFOSTAT=0x%08x RXCNT=0x%08x CLKDIV=0x%08x\n",
          FifoStat, RxCount, MmioRead32 ((UINTN)Base + SPI_CLKDIV)));
  DEBUG ((DEBUG_ERROR, "SPIHID: SHIFTCFG=0x%08x PINCFG=0x%08x SCKCFG=0x%08x XFSTATUS=0x%08x\n",
          MmioRead32 ((UINTN)Base + SPI_SHIFTCFG),
          MmioRead32 ((UINTN)Base + SPI_PINCFG),
          MmioRead32 ((UINTN)Base + SPI_SCKCFG),
          MmioRead32 ((UINTN)Base + SPI_XFSTATUS)));

  if ((Control & BIT0) == 0) {
    DEBUG ((DEBUG_ERROR, "SPIHID: CTRL.RUN is clear; iBoot left the block idle\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "SPIHID: CTRL.RUN is set; iBoot left the block running\n"));
  }

  //
  // 3. Watch the RX FIFO. Any movement proves the transport is live and that
  //    the remaining work is framing rather than power, clock and pinmux.
  //
  DEBUG ((DEBUG_ERROR, "SPIHID: watching RX FIFO for %u ms -- touch the trackpad or press a key now\n",
          SPI_WATCH_MILLISECONDS));

  Baseline = SPI_FIFOSTAT_LEVEL_RX (FifoStat);
  Peak     = Baseline;
  Samples  = 0;
  Changed  = FALSE;

  for (Index = 0; Index < (SPI_WATCH_MILLISECONDS / SPI_WATCH_INTERVAL_MS); Index++) {
    gBS->Stall (SPI_WATCH_INTERVAL_MS * 1000);

    FifoStat = MmioRead32 ((UINTN)Base + SPI_FIFOSTAT);
    Level    = SPI_FIFOSTAT_LEVEL_RX (FifoStat);
    Samples++;

    if (Level != Baseline) {
      Changed = TRUE;
    }

    if (Level > Peak) {
      Peak = Level;
    }
  }

  RxCount = MmioRead32 ((UINTN)Base + SPI_RXCNT);

  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: samples=%u baseline=%u peak=%u changed=%a RXCNT=0x%08x\n",
    Samples,
    Baseline,
    Peak,
    Changed ? "YES" : "NO",
    RxCount
    ));

  if (Changed) {
    DEBUG ((DEBUG_ERROR, "SPIHID: transport is streaming; the driver needs framing, not bring-up\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "SPIHID: no FIFO movement seen. Either the block is unpowered, the\n"));
    DEBUG ((DEBUG_ERROR, "SPIHID: pad muxes are off, or SPI-HID needs its reset handshake first.\n"));
    DEBUG ((DEBUG_ERROR, "SPIHID: that is the difference between a reader and a bring-up port.\n"));
  }

  DEBUG ((DEBUG_ERROR, "SPIHID: ===== end of survey =====\n"));

  return EFI_SUCCESS;
}
