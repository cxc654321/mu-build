/** @file
 *  AppleSpiHidDxe - Apple SPI-HID transport for the internal keyboard and
 *  trackpad on M1 (J313). Stage 2: controller programming and a raw transfer.
 *
 *  Stage 1 located the hardware and printed what iBoot left behind. This stage
 *  drives the block:
 *
 *    - Controller programming follows drivers/spi/spi-apple.c in mainline Linux
 *      (register map, FIFO handling, polled transfer).
 *    - One 256-byte transfer is clocked with a zero payload and the MISO bytes
 *      are dumped, which measures the SPI-HID framing instead of guessing it.
 *
 *  STILL NOT IMPLEMENTED: the SPI-HID message protocol and HID report decoding,
 *  so this produces no input. It answers one question with evidence -- does the
 *  transport answer at all, and what does its framing look like.
 *
 *  Safety: every wait is bounded and every failure is reported rather than
 *  retried. The only writes are the controller's own registers, in the order
 *  Linux uses. No power domain, clock gate or pinmux is programmed; if iBoot
 *  left those off, the transfer reads back idle bytes and says so.
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
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

//
// Apple SPI -- drivers/spi/spi-apple.c (mainline) and m1n1 hw/spi.py agree.
//
#define SPI_CTRL        0x000
#define SPI_CTRL_RUN        BIT0
#define SPI_CTRL_TX_RESET   BIT2
#define SPI_CTRL_RX_RESET   BIT3

#define SPI_CFG         0x004
#define SPI_CFG_MODE_POLLED 0
#define SPI_CFG_WORD_SIZE_8B 0
#define SPI_CFG_FIFO_THRESH_8B 0

#define SPI_STATUS      0x008
#define SPI_PIN         0x00C
#define SPI_PIN_CS          BIT1
#define SPI_TXDATA      0x010
#define SPI_RXDATA      0x020
#define SPI_CLKDIV      0x030
#define SPI_RXCNT       0x034
#define SPI_TXCNT       0x04C
#define SPI_FIFOSTAT    0x10C
#define SPI_FIFOSTAT_LEVEL_TX(v)  (((v) >> 8) & 0xFF)
#define SPI_FIFOSTAT_LEVEL_RX(v)  (((v) >> 24) & 0xFF)
#define SPI_IE_XFER     0x130
#define SPI_IF_XFER     0x134
#define SPI_IF_XFER_RXCOMPLETE BIT0
#define SPI_IF_XFER_TXCOMPLETE BIT1
#define SPI_IE_FIFO     0x138
#define SPI_IF_FIFO     0x13C
#define SPI_IF_FIFO_TXTHRESH BIT5
#define SPI_IF_FIFO_RXTHRESH BIT4
#define SPI_SHIFTCFG    0x150
#define SPI_SHIFTCFG_CS_ENABLE  BIT1
#define SPI_PINCFG      0x154
#define SPI_PINCFG_KEEP_CS      BIT1
#define SPI_PINCFG_CS_IDLE_VAL  BIT9
#define SPI_DELAY_PRE   0x160
#define SPI_DELAY_POST  0x168
#define SPI_XFSTATUS    0x1C0

#define SPI_FIFO_DEPTH  16
#define SPI_TIMEOUT_MS  200
#define SPI_POLL_MS     1

//
// 200 MHz is the reference clock the ADT gives spi3 ("clocks = <&clk_200m>").
// A 1 MHz bus is far below anything the keyboard needs and is the safest
// starting point while the pads are still configured by iBoot.
//
#define SPI_REFCLK_HZ   200000000
#define SPI_BUS_HZ      1000000

#define SPI_TRANSFER_LEN  256

STATIC UINTN      mSpiBase;
STATIC dt_node_t  *mHidTransportNode;

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

  if ((Key == NULL) || (Val == NULL) || (mHidTransportNode != NULL)) {
    return 0;
  }

  if (AsciiStrCmp (Key, "compatible") != 0) {
    return 0;
  }

  if (ListContains ((CONST CHAR8 *)Val, Len, "apple,spi-hid-transport")) {
    mHidTransportNode = Node;
  }

  return 0;
}

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

STATIC
UINT32
SpiRead (
  IN UINT32  Offset
  )
{
  return MmioRead32 (mSpiBase + Offset);
}

STATIC
VOID
SpiWrite (
  IN UINT32  Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (mSpiBase + Offset, Value);
}
/**
  Program the controller the way spi-apple.c does and leave it idle.
**/
STATIC
EFI_STATUS
SpiControllerInit (
  VOID
  )
{
  //
  // CS high (inactive), auto-CS off, CS driven between transfers.
  //
  SpiWrite (SPI_PIN, SPI_PIN_CS);
  SpiWrite (SPI_SHIFTCFG, SpiRead (SPI_SHIFTCFG) & ~(UINT32)SPI_SHIFTCFG_CS_ENABLE);
  SpiWrite (SPI_PINCFG, SPI_PINCFG_CS_IDLE_VAL | SPI_PINCFG_KEEP_CS);

  //
  // Reset both FIFOs.
  //
  SpiWrite (SPI_CTRL, SPI_CTRL_TX_RESET | SPI_CTRL_RX_RESET);

  //
  // Polled mode, 8-bit words, 8-byte FIFO threshold.
  //
  SpiWrite (SPI_CFG, SPI_CFG_FIFO_THRESH_8B | SPI_CFG_MODE_POLLED | SPI_CFG_WORD_SIZE_8B);

  SpiWrite (SPI_IE_XFER, 0);
  SpiWrite (SPI_IE_FIFO, 0);
  SpiWrite (SPI_DELAY_PRE, 0);
  SpiWrite (SPI_DELAY_POST, 0);
  SpiWrite (SPI_CLKDIV, SPI_REFCLK_HZ / SPI_BUS_HZ);

  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: init CFG=0x%08x SHIFTCFG=0x%08x PIN=0x%08x PINCFG=0x%08x CLKDIV=%u\n",
    SpiRead (SPI_CFG),
    SpiRead (SPI_SHIFTCFG),
    SpiRead (SPI_PIN),
    SpiRead (SPI_PINCFG),
    SpiRead (SPI_CLKDIV)
    ));

  return EFI_SUCCESS;
}

/**
  Poll until any requested FIFO or transfer bit is set. Bounded in wall time:
  there is no interrupt path here and a stuck controller must report, not hang.
**/
STATIC
EFI_STATUS
SpiWait (
  IN  UINT32  FifoBits,
  IN  UINT32  XferBits,
  OUT UINT32  *FifoSeen,
  OUT UINT32  *XferSeen
  )
{
  UINTN   ElapsedUs;
  UINT32  Fifo;
  UINT32  Xfer;

  for (ElapsedUs = 0; ElapsedUs < SPI_TIMEOUT_MS * 1000; ElapsedUs += SPI_POLL_MS * 1000) {
    Fifo = SpiRead (SPI_IF_FIFO);
    Xfer = SpiRead (SPI_IF_XFER);
    if (((Fifo & FifoBits) != 0) || ((Xfer & XferBits) != 0)) {
      *FifoSeen = Fifo;
      *XferSeen = Xfer;
      return EFI_SUCCESS;
    }

    gBS->Stall (SPI_POLL_MS * 1000);
  }

  *FifoSeen = SpiRead (SPI_IF_FIFO);
  *XferSeen = SpiRead (SPI_IF_XFER);
  return EFI_TIMEOUT;
}

/**
  One full-duplex transfer, 8-bit words, polled: the shape of
  apple_spi_transfer_one() with interrupt waits replaced by SpiWait().
**/
STATIC
EFI_STATUS
SpiTransfer (
  IN  CONST UINT8  *Tx,
  OUT UINT8        *Rx,
  IN  UINTN        Len
  )
{
  EFI_STATUS  Status;
  UINT32      FifoSeen;
  UINT32      XferSeen;
  UINT32      Pending;
  UINT32      Level;
  UINT32      FifoStat;
  UINTN       TxLeft;
  UINTN       RxLeft;
  UINTN       Index;
  UINTN       Words;

  if ((Tx == NULL) || (Rx == NULL) || (Len == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  SpiWrite (SPI_CTRL, SPI_CTRL_TX_RESET | SPI_CTRL_RX_RESET);
  SpiWrite (SPI_IF_XFER, 0xFFFFFFFF);
  SpiWrite (SPI_IF_FIFO, 0xFFFFFFFF);

  SpiWrite (SPI_TXCNT, (UINT32)Len);
  SpiWrite (SPI_RXCNT, (UINT32)Len);

  TxLeft  = Len;
  RxLeft  = Len;
  Pending = SPI_IF_XFER_TXCOMPLETE | SPI_IF_XFER_RXCOMPLETE;

  //
  // Prime the TX FIFO, start, then top it up: the controller pops a few words
  // the moment RUN is set, which is why spi-apple.c transmits twice.
  //
  Words = MIN (TxLeft, (UINTN)SPI_FIFO_DEPTH);
  for (Index = 0; Index < Words; Index++) {
    SpiWrite (SPI_TXDATA, Tx[Index]);
  }

  TxLeft -= Words;

  SpiWrite (SPI_CTRL, SPI_CTRL_RUN);

  while (Pending != 0) {
    Status = SpiWait (
               ((TxLeft != 0) ? SPI_IF_FIFO_TXTHRESH : 0) |
               ((RxLeft != 0) ? SPI_IF_FIFO_RXTHRESH : 0),
               Pending,
               &FifoSeen,
               &XferSeen
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "SPIHID: transfer timed out: tx_left=%u rx_left=%u IF_XFER=0x%08x IF_FIFO=0x%08x\n",
        (UINT32)TxLeft,
        (UINT32)RxLeft,
        XferSeen,
        FifoSeen
        ));
      return Status;
    }

    Pending &= ~XferSeen;

    FifoStat = SpiRead (SPI_FIFOSTAT);
    while ((TxLeft != 0) &&
           (SPI_FIFOSTAT_LEVEL_TX (FifoStat) < SPI_FIFO_DEPTH))
    {
      SpiWrite (SPI_TXDATA, Tx[Len - TxLeft]);
      TxLeft--;
      FifoStat = SpiRead (SPI_FIFOSTAT);
    }

    Level = SPI_FIFOSTAT_LEVEL_RX (SpiRead (SPI_FIFOSTAT));
    while ((RxLeft != 0) && (Level != 0)) {
      Rx[Len - RxLeft] = (UINT8)SpiRead (SPI_RXDATA);
      RxLeft--;
      Level--;
    }
  }

  while (RxLeft != 0) {
    Rx[Len - RxLeft] = (UINT8)SpiRead (SPI_RXDATA);
    RxLeft--;
  }

  return EFI_SUCCESS;
}
/**
  Dump a byte buffer as hex, a line per 16 bytes. Only used for the probe.
**/
STATIC
VOID
DumpBytes (
  IN CONST UINT8  *Data,
  IN UINTN        Len
  )
{
  UINTN  Offset;
  UINTN  Index;
  CHAR8  Line[3 * 16 + 1];

  for (Offset = 0; Offset < Len; Offset += 16) {
    for (Index = 0; (Index < 16) && (Offset + Index < Len); Index++) {
      Line[Index * 3 + 0] = "0123456789ABCDEF"[Data[Offset + Index] >> 4];
      Line[Index * 3 + 1] = "0123456789ABCDEF"[Data[Offset + Index] & 0xF];
      Line[Index * 3 + 2] = ' ';
    }

    Line[Index * 3] = '\0';
    DEBUG ((DEBUG_ERROR, "SPIHID:   %04x: %a\n", (UINT32)Offset, Line));
  }
}

/**
  Locate, program and probe the SPI-HID transport. See the file header.
**/
EFI_STATUS
EFIAPI
AppleSpiHidDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  uint64_t    Base;
  uint64_t    Size;
  dt_node_t   *Root;
  dt_node_t   *SpiNode;
  UINT8       *TxBuf;
  UINT8       *RxBuf;
  UINT32      FifoStat;
  UINT32      IfXfer;
  UINT32      IfFifo;
  UINT32      Ctrl;
  UINTN       Index;
  UINTN       Distinct;
  UINT8       First;

  DEBUG ((DEBUG_ERROR, "SPIHID: ===== SPI-HID transport, stage 2: controller + raw transfer =====\n"));

  Root = (dt_node_t *)(UINTN)FixedPcdGet64 (PcdAdtPointer);
  if (Root == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: no ADT pointer; nothing to do\n"));
    return EFI_SUCCESS;
  }

  dt_parse (Root, 0, NULL, SpiHidNodeCallback, NULL, SpiHidPropCallback, NULL);

  if (mHidTransportNode == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: no apple,spi-hid-transport node in this ADT\n"));
    return EFI_SUCCESS;
  }

  SpiNode = dt_node_parent (mHidTransportNode);
  if (SpiNode == NULL) {
    DEBUG ((DEBUG_ERROR, "SPIHID: transport node has no parent controller\n"));
    return EFI_SUCCESS;
  }

  if (dt_node_reg (SpiNode, 0, &Base, &Size) != 0) {
    DEBUG ((DEBUG_ERROR, "SPIHID: controller has no reg[0]\n"));
    return EFI_SUCCESS;
  }

  mSpiBase = (UINTN)Base;
  DEBUG ((DEBUG_ERROR, "SPIHID: controller at 0x%lx size 0x%lx\n", Base, Size));

  //
  // What iBoot left behind, before this driver touches anything.
  //
  Ctrl = SpiRead (SPI_CTRL);
  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: before CTRL=0x%08x CFG=0x%08x PIN=0x%08x CLKDIV=0x%08x\n",
    Ctrl,
    SpiRead (SPI_CFG),
    SpiRead (SPI_PIN),
    SpiRead (SPI_CLKDIV)
    ));
  DEBUG ((DEBUG_ERROR, "SPIHID: before CTRL.RUN is %a\n", (Ctrl & SPI_CTRL_RUN) != 0 ? "SET" : "CLEAR"));

  Status = SpiControllerInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SPIHID: controller init failed (%r)\n", Status));
    return EFI_SUCCESS;
  }

  TxBuf = AllocateZeroPool (SPI_TRANSFER_LEN);
  RxBuf = AllocateZeroPool (SPI_TRANSFER_LEN);
  if ((TxBuf == NULL) || (RxBuf == NULL)) {
    DEBUG ((DEBUG_ERROR, "SPIHID: out of memory\n"));
    if (TxBuf != NULL) {
      FreePool (TxBuf);
    }

    if (RxBuf != NULL) {
      FreePool (RxBuf);
    }

    return EFI_SUCCESS;
  }

  //
  // Clock one buffer through. The payload is not a real SPI-HID request yet --
  // this is the measure-first step: if MISO returns anything structured, the
  // framing can be read off the dump instead of guessed from another OS.
  //
  DEBUG ((DEBUG_ERROR, "SPIHID: transferring %u bytes at %u Hz\n", SPI_TRANSFER_LEN, SPI_BUS_HZ));

  Status = SpiTransfer (TxBuf, RxBuf, SPI_TRANSFER_LEN);

  FifoStat = SpiRead (SPI_FIFOSTAT);
  IfXfer   = SpiRead (SPI_IF_XFER);
  IfFifo   = SpiRead (SPI_IF_FIFO);

  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: after transfer status=%r FIFOSTAT=0x%08x IF_XFER=0x%08x IF_FIFO=0x%08x\n",
    Status,
    FifoStat,
    IfXfer,
    IfFifo
    ));

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SPIHID: transfer did not complete; nothing was received\n"));
    FreePool (TxBuf);
    FreePool (RxBuf);
    return EFI_SUCCESS;
  }

  Distinct = 0;
  First    = RxBuf[0];
  for (Index = 0; Index < SPI_TRANSFER_LEN; Index++) {
    if (RxBuf[Index] != First) {
      Distinct++;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "SPIHID: MISO first=0x%02x distinct=%u/256\n",
    First,
    (UINT32)Distinct
    ));

  if (Distinct == 0) {
    DEBUG ((DEBUG_ERROR, "SPIHID: MISO is a constant 0x%02x. The block is either not\n", First));
    DEBUG ((DEBUG_ERROR, "SPIHID: powered, its pads are still muxed away, or it needs the\n"));
    DEBUG ((DEBUG_ERROR, "SPIHID: SPI-HID reset handshake before it drives MISO.\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "SPIHID: MISO varies; the transport answers. First 64 bytes:\n"));
    DumpBytes (RxBuf, 64);
  }

  FreePool (TxBuf);
  FreePool (RxBuf);

  DEBUG ((DEBUG_ERROR, "SPIHID: ===== end of stage 2 =====\n"));

  return EFI_SUCCESS;
}
