/** @file
 *  AppleMtpHidDxe - internal keyboard transport for J704 (M5), stage 1.
 *
 *  J704's internal keyboard is not USB and not SPI. It hangs off the MTP
 *  (Multi-Touch Processor), the M2-and-later arrangement:
 *
 *      /arm-io/mtp              iop,ascwrap-v6      RTKit coprocessor
 *      /arm-io/mtp/iop-mtp-nub  iop-nub,rtbuddy-v2
 *      /arm-io/dart-mtp         dart,t8110          IOMMU for its DMA
 *      /arm-io/dockchannel-mtp  dockchannel,t8002   the HID data path
 *      /arm-io/mtp-aop-mux      hid-transport,mux
 *
 *  The end goal is a HID_IO_PROTOCOL producer, at which point Project Mu's
 *  HidKeyboardDxe (already in AppleSiliconPkg.dsc.inc) turns it into
 *  SimpleTextInputEx for free -- no keycode translation ever gets written here.
 *
 *  THIS STAGE DOES NOT PRODUCE HID_IO YET, AND DELIBERATELY SO.
 *
 *  How big the remaining work is depends on one unmeasured fact: whether iBoot
 *  leaves the MTP running and streaming HID reports into the dockchannel FIFO.
 *
 *    - If it does, the driver is a FIFO reader plus MTP framing. Small.
 *    - If it does not, UEFI must boot the coprocessor first: ASC mailbox, RTKit
 *      handshake with DART-mapped buffers, SMC endpoint 0x20, then per-interface
 *      init -- a port of m1n1/fw/mtp.py, m1n1/fw/asc/, m1n1/fw/smc.py. Large.
 *
 *  Roughly a 5x difference in scope, so this stage measures it instead of
 *  guessing. It reads the ADT topology, reports whether the MTP coprocessor is
 *  running, and watches the dockchannel RX FIFO for traffic. Everything here is
 *  READ-ONLY: no register is written, no coprocessor is started, no DART is
 *  programmed.
 *
 *  Register definitions are taken from m1n1, which is the only description of
 *  this hardware that exists: proxyclient/m1n1/hw/dockchannel.py and
 *  proxyclient/m1n1/hw/asc.py, cross-checked against
 *  proxyclient/experiments/mtp.py for which reg indices carry what.
 *
 *  Copyright (c) 2026 Aurora Silicon
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/AppleDTLib.h>

//
// ASC (coprocessor wrapper) -- proxyclient/m1n1/hw/asc.py, class ASCRegs.
//
#define ASC_CPU_CONTROL  0x0044
#define ASC_CPU_STATUS   0x0048

#define ASC_CPU_CONTROL_RUN  BIT4

#define ASC_CPU_STATUS_RUNNING  BIT0
#define ASC_CPU_STATUS_STOPPED  BIT1
#define ASC_CPU_STATUS_IDLE     BIT5

#define ASC_INBOX_CTRL   0x8110
#define ASC_OUTBOX_CTRL  0x8114

#define ASC_MBOX_CTRL_FULL   BIT16
#define ASC_MBOX_CTRL_EMPTY  BIT17

//
// DockChannel -- proxyclient/m1n1/hw/dockchannel.py.
//
// experiments/mtp.py builds its DockChannel from reg index 1 (IRQ block) and
// reg index 2 (FIFO block), with the data registers at fifo_base + 0x4000.
//
#define DOCKCHANNEL_IRQ_REG_INDEX   1
#define DOCKCHANNEL_FIFO_REG_INDEX  2

#define DOCKCHANNEL_IRQ_MASK  0x00
#define DOCKCHANNEL_IRQ_FLAG  0x04

#define DOCKCHANNEL_CFG_TX_THRESH  0x00
#define DOCKCHANNEL_CFG_RX_THRESH  0x04

#define DOCKCHANNEL_DATA_OFFSET  0x4000
#define DOCKCHANNEL_DATA_TX_FREE   0x14
#define DOCKCHANNEL_DATA_RX_8      0x1C
#define DOCKCHANNEL_DATA_RX_COUNT  0x2C

//
// R_RX_DATA: DATA is bits 31:8, COUNT is bits 7:0.
//
#define DOCKCHANNEL_RX_DATA(v)  (((v) >> 8) & 0xFF)

//
// How long to watch the RX FIFO, and how often to sample. The internal keyboard
// should be typed on during this window.
//
#define MTP_WATCH_MILLISECONDS  8000
#define MTP_WATCH_INTERVAL_MS   100

STATIC
VOID
ReportNode (
  IN CONST CHAR8  *Path,
  IN BOOLEAN      DumpRegs
  )
{
  dt_node_t  *Node;
  UINT64     Base;
  UINT64     Size;
  UINT32     Index;

  Node = dt_get (Path);
  if (Node == NULL) {
    DEBUG ((DEBUG_ERROR, "MTPHID:   %-40a ABSENT\n", Path));
    return;
  }

  DEBUG ((DEBUG_ERROR, "MTPHID:   %-40a present\n", Path));

  if (!DumpRegs) {
    return;
  }

  for (Index = 0; Index < 8; Index++) {
    if (dt_node_reg (Node, Index, &Base, &Size) != 0) {
      break;
    }

    DEBUG ((
      DEBUG_ERROR,
      "MTPHID:       reg[%u] = 0x%lx  size 0x%lx\n",
      Index,
      Base,
      Size
      ));
  }
}

/**
  Stage 1: survey only. See the file header for why this does not produce
  HID_IO_PROTOCOL yet.
**/
EFI_STATUS
EFIAPI
AppleMtpHidDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  dt_node_t  *MtpNode;
  dt_node_t  *DockChannelNode;
  dt_node_t  *KeyboardNode;
  UINT64     MtpBase;
  UINT64     IrqBase;
  UINT64     FifoBase;
  UINT32     CpuControl;
  UINT32     CpuStatus;
  UINT32     InboxCtrl;
  UINT32     OutboxCtrl;
  UINT32     RxCount;
  UINT32     Baseline;
  UINT32     Peak;
  UINT32     Elapsed;
  BOOLEAN    Changed;

  DEBUG ((DEBUG_ERROR, "MTPHID: ===== internal keyboard (MTP) survey, stage 1 =====\n"));

  //
  // 1. Topology. The platform-data captures under platform-data/ come from macOS
  //    `ioreg -p IODeviceTree` (tools/adt_extract.py), which is macOS's processed
  //    view rather than the ADT m1n1 hands us -- it showed no keyboard node under
  //    mtp-transport, which may or may not be true here. This reads the live ADT.
  //
  DEBUG ((DEBUG_ERROR, "MTPHID: --- ADT topology ---\n"));
  ReportNode ("/arm-io/mtp", TRUE);
  ReportNode ("/arm-io/mtp/iop-mtp-nub", FALSE);
  ReportNode ("/arm-io/dart-mtp", TRUE);
  ReportNode ("/arm-io/dockchannel-mtp", TRUE);
  ReportNode ("/arm-io/dockchannel-mtp/mtp-transport", FALSE);
  ReportNode ("/arm-io/mtp-aop-mux", FALSE);
  ReportNode ("/arm-io/smc", FALSE);

  //
  // The path m1n1's kboot.c:886 expects on M2-class machines. If this is absent,
  // the node moved on M5 and the walk above is what tells us where to look.
  //
  KeyboardNode = dt_get ("/arm-io/dockchannel-mtp/mtp-transport/keyboard");
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID: keyboard node at the M2 path: %a\n",
    KeyboardNode != NULL ? "FOUND" : "ABSENT"
    ));

  //
  // 2. Is the coprocessor already running? This is the question that decides
  //    whether UEFI needs the whole RTKit boot path.
  //
  MtpNode = dt_get ("/arm-io/mtp");
  if ((MtpNode == NULL) || (dt_node_reg (MtpNode, 0, &MtpBase, NULL) != 0)) {
    DEBUG ((DEBUG_ERROR, "MTPHID: no usable /arm-io/mtp reg[0] -- cannot probe the ASC\n"));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "MTPHID: --- MTP coprocessor state (ASC @ 0x%lx) ---\n", MtpBase));

  CpuStatus  = MmioRead32 ((UINTN)(MtpBase + ASC_CPU_STATUS));
  CpuControl = MmioRead32 ((UINTN)(MtpBase + ASC_CPU_CONTROL));
  InboxCtrl  = MmioRead32 ((UINTN)(MtpBase + ASC_INBOX_CTRL));
  OutboxCtrl = MmioRead32 ((UINTN)(MtpBase + ASC_OUTBOX_CTRL));

  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   CPU_CONTROL = 0x%08x  RUN=%d\n",
    CpuControl,
    (CpuControl & ASC_CPU_CONTROL_RUN) ? 1 : 0
    ));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   CPU_STATUS  = 0x%08x  RUNNING=%d STOPPED=%d IDLE=%d\n",
    CpuStatus,
    (CpuStatus & ASC_CPU_STATUS_RUNNING) ? 1 : 0,
    (CpuStatus & ASC_CPU_STATUS_STOPPED) ? 1 : 0,
    (CpuStatus & ASC_CPU_STATUS_IDLE) ? 1 : 0
    ));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   INBOX_CTRL  = 0x%08x  EMPTY=%d FULL=%d\n",
    InboxCtrl,
    (InboxCtrl & ASC_MBOX_CTRL_EMPTY) ? 1 : 0,
    (InboxCtrl & ASC_MBOX_CTRL_FULL) ? 1 : 0
    ));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   OUTBOX_CTRL = 0x%08x  EMPTY=%d FULL=%d\n",
    OutboxCtrl,
    (OutboxCtrl & ASC_MBOX_CTRL_EMPTY) ? 1 : 0,
    (OutboxCtrl & ASC_MBOX_CTRL_FULL) ? 1 : 0
    ));

  if (CpuStatus & ASC_CPU_STATUS_STOPPED) {
    DEBUG ((DEBUG_ERROR, "MTPHID:   -> STOPPED. UEFI must boot it: ASC + RTKit + DART. LARGE.\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "MTPHID:   -> RUNNING. UEFI may be able to skip RTKit boot entirely.\n"));
  }

  if ((OutboxCtrl & ASC_MBOX_CTRL_EMPTY) == 0) {
    DEBUG ((DEBUG_ERROR, "MTPHID:   -> OUTBOX not empty: it has queued a message for the AP.\n"));
  }

  //
  // 3. Is HID traffic already flowing? This is the measurement that sizes the
  //    rest of the driver.
  //
  DockChannelNode = dt_get ("/arm-io/dockchannel-mtp");
  if (DockChannelNode == NULL) {
    DEBUG ((DEBUG_ERROR, "MTPHID: no /arm-io/dockchannel-mtp -- cannot watch the FIFO\n"));
    return EFI_SUCCESS;
  }

  if (  (dt_node_reg (DockChannelNode, DOCKCHANNEL_IRQ_REG_INDEX, &IrqBase, NULL) != 0)
     || (dt_node_reg (DockChannelNode, DOCKCHANNEL_FIFO_REG_INDEX, &FifoBase, NULL) != 0))
  {
    DEBUG ((DEBUG_ERROR, "MTPHID: dockchannel reg[1]/reg[2] unavailable\n"));
    return EFI_SUCCESS;
  }

  DEBUG ((
    DEBUG_ERROR,
    "MTPHID: --- dockchannel (irq 0x%lx, fifo 0x%lx) ---\n",
    IrqBase,
    FifoBase
    ));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   IRQ_MASK = 0x%08x  IRQ_FLAG = 0x%08x\n",
    MmioRead32 ((UINTN)(IrqBase + DOCKCHANNEL_IRQ_MASK)),
    MmioRead32 ((UINTN)(IrqBase + DOCKCHANNEL_IRQ_FLAG))
    ));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   TX_THRESH = 0x%08x  RX_THRESH = 0x%08x  TX_FREE = %u\n",
    MmioRead32 ((UINTN)(FifoBase + DOCKCHANNEL_CFG_TX_THRESH)),
    MmioRead32 ((UINTN)(FifoBase + DOCKCHANNEL_CFG_RX_THRESH)),
    MmioRead32 ((UINTN)(FifoBase + DOCKCHANNEL_DATA_OFFSET + DOCKCHANNEL_DATA_TX_FREE))
    ));

  Baseline = MmioRead32 ((UINTN)(FifoBase + DOCKCHANNEL_DATA_OFFSET + DOCKCHANNEL_DATA_RX_COUNT));
  Peak     = Baseline;
  Changed  = FALSE;

  DEBUG ((DEBUG_ERROR, "MTPHID:   RX_COUNT baseline = %u\n", Baseline));
  DEBUG ((
    DEBUG_ERROR,
    "MTPHID: *** TYPE ON THE INTERNAL KEYBOARD NOW -- watching for %u ms ***\n",
    MTP_WATCH_MILLISECONDS
    ));

  for (Elapsed = 0; Elapsed < MTP_WATCH_MILLISECONDS; Elapsed += MTP_WATCH_INTERVAL_MS) {
    RxCount = MmioRead32 ((UINTN)(FifoBase + DOCKCHANNEL_DATA_OFFSET + DOCKCHANNEL_DATA_RX_COUNT));

    if (RxCount != Baseline) {
      Changed = TRUE;
    }

    if (RxCount > Peak) {
      Peak = RxCount;
    }

    if ((Elapsed % 1000) == 0) {
      DEBUG ((
        DEBUG_ERROR,
        "MTPHID:   t=%ums RX_COUNT=%u IRQ_FLAG=0x%08x\n",
        Elapsed,
        RxCount,
        MmioRead32 ((UINTN)(IrqBase + DOCKCHANNEL_IRQ_FLAG))
        ));
    }

    MicroSecondDelay (MTP_WATCH_INTERVAL_MS * 1000);
  }

  DEBUG ((
    DEBUG_ERROR,
    "MTPHID:   baseline=%u peak=%u changed=%a\n",
    Baseline,
    Peak,
    Changed ? "YES" : "no"
    ));

  if (Changed || (Peak > Baseline)) {
    DEBUG ((DEBUG_ERROR, "MTPHID: *** REPORTS ARE FLOWING with no bring-up from us. ***\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     Stage 2 = dockchannel FIFO reader + MTP framing -> HID_IO.\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     No RTKit, no SMC, no DART needed. Small driver.\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "MTPHID: FIFO silent. The MTP has to be brought up first.\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     Stage 2 = port m1n1/fw/mtp.py + fw/asc/ + fw/smc.py. Large.\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     Confirm the protocol first with m1n1's own\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     proxyclient/experiments/mtp.py -- if wait_init(\"keyboard\")\n"));
    DEBUG ((DEBUG_ERROR, "MTPHID:     succeeds there, only the transport port remains.\n"));
  }

  DEBUG ((DEBUG_ERROR, "MTPHID: ===== survey complete =====\n"));

  return EFI_SUCCESS;
}
