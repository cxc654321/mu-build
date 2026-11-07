/* SPDX-License-Identifier: GPL-2.0-only
 * Production MMIO binding of Experimental/J313SpiPio (GPL-2.0-only).
 * GPIO/enable sequencing derived from Asahi Linux Contributors' GPL sources:
 * linux 414e55da7a0afaca2a97b29603be88ce0a6fd6de pinctrl-apple-gpio.c
 * linux 549e8b442b485c03e3e766635e2203c75362e312 spi-hid-apple-of.c/core.c
 * GPIO upstream also credits Corellium LLC, Imagination Technologies, Google.
 * PMGR mask/poll follows MIT m1n1 src/pmgr.c at LOCAL pinned revision
 * 69affa5a867a44cb281c6cf8716f5a38e3b0b063 (not the older GPL snapshot).
 * Board constants: dual MIT/GPL t8103{,-j313,-pmgr}.dts{i} at 414e55da7.
 * No code or clock assumptions copied from quarantined BSD-labelled driver.
 */
#include "J313SpiTransport.h"
#ifndef J313_HOST_TEST
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
static uint32_t RealRead(void *c, uint64_t a) {
  (void)c; return MmioRead32((UINTN)a);
}
static void RealWrite(void *c, uint64_t a, uint32_t v) {
  (void)c; MmioWrite32((UINTN)a, v);
}
static uint64_t RealNow(void *c) {
  (void)c; return GetTimeInNanoSecond(GetPerformanceCounter()) / 1000;
}
static void RealDelay(void *c, uint32_t us) {
  (void)c; MicroSecondDelay(us);
}
#endif

#define GPIO_PERIPH (3u << 5)
#define GPIO_INPUT (1u << 9)
#define GPIO_MODE (7u << 1)
#define GPIO_DATA 1u
#define GPIO_LOCK (1u << 21)
#define PMGR_CHANGE ((1u << 28) | (3u << 8) | 15u)

static uint32_t Rd(J313_SPI_TRANSPORT *t, uint64_t a) {
  return t->Io.Read32(t->Io.Context, a);
}
static void Wr(J313_SPI_TRANSPORT *t, uint64_t a, uint32_t v) {
  t->Io.Write32(t->Io.Context, a, v);
}
static void Mask(J313_SPI_TRANSPORT *t, uint64_t a, uint32_t mask, uint32_t v) {
  Wr(t, a, (Rd(t, a) & ~mask) | v);
}
static bool Window(uint64_t base, uint64_t size, uint64_t expected,
                   uint64_t min, uint64_t max) {
  return base == expected && size >= min && size <= max && !(size & 3);
}
static bool ResourcesValid(const J313_SPI_RESOURCES *r) {
  unsigned i, j;
  bool ap = false, nub = false;
  if (!r || r->TargetType[0] != 'j' || r->TargetType[1] != '3' ||
      r->TargetType[2] != '1' || r->TargetType[3] != '3' || r->TargetType[4] ||
      r->ChipId != 0x8103 || r->ClockGate != 0x34 ||
      !r->PowerCount || r->PowerCount > J313_SPI_POWER_MAX) return false;
  if (!Window(r->SpiBase,r->SpiSize,0x23510c000ULL,0x16c,0x4000) ||
      !Window(r->ApBase,r->ApSize,0x23c100000ULL,196*4,0x100000) ||
      !Window(r->NubBase,r->NubSize,0x23d1f0000ULL,14*4,0x4000) ||
      !Window(r->PmgrBase,r->PmgrSize,0x23b700000ULL,0x1b80084,0x1b84000)) return false;
  /* SPI3 PS register is independently cross-checked with public T8103 DTS.
   * The adapter must resolve the complete DAG, reject cycles/missing parents.
   * Accept no duplicate or out-of-range nodes even from that adapter. */
  if (r->PowerOffset[r->PowerCount - 1] != 0x258) return false;
  for (i = 0; i < r->PowerCount; ++i) {
    uint32_t off = r->PowerOffset[i];
    if ((off & 7) || off > r->PmgrSize - 4 ||
        !((off >= 0x100 && off <= 0x13ff8) ||
          (off >= 0x1b80000 && off <= 0x1b83ff8))) return false;
    if (off == 0x130) ap = true;
    if (off == 0x1b80080) nub = true;
    for (j = 0; j < i; ++j)
      if (r->PowerOffset[i] == r->PowerOffset[j]) return false;
  }
  return ap && nub;
}
/* Both elapsed time and iteration cap: frozen/reversed clocks fail closed.
 * The callbacks/MMIO interconnect must themselves return; software cannot
 * recover a synchronous external abort or bound a hung hardware bus. */
static bool Delay(J313_SPI_TRANSPORT *t, uint32_t us) {
  uint64_t start = t->Io.NowUs(t->Io.Context), last = start, now;
  uint32_t n;
  for (n = 0; n <= us; ++n) {
    now = t->Io.NowUs(t->Io.Context);
    if (now < last) return false;
    if (now - start >= us) return true;
    last = now;
    t->Io.DelayUs(t->Io.Context, 1);
  }
  return false;
}
static bool PowerOn(J313_SPI_TRANSPORT *t, uint64_t a) {
  uint64_t start = t->Io.NowUs(t->Io.Context), last = start, now;
  unsigned n;
  uint32_t v = Rd(t, a);
  /* Never defeat a reset/disable/parent-off indication by guessing more bits. */
  if (v & ((1u << 31) | (1u << 11) | (1u << 10))) return false;
  Wr(t, a, (v & ~PMGR_CHANGE) | 15u);
  for (n = 0; n < 10000; ++n) {
    now = t->Io.NowUs(t->Io.Context);
    if (now < last || now - start >= 10000) return false;
    last = now;
    v = Rd(t, a);
    if (v & ((1u << 31) | (1u << 11) | (1u << 10))) return false;
    if ((v & 255u) == 255u) return true;
    t->Io.DelayUs(t->Io.Context, 1);
  }
  return false;
}
static bool GpioSet(J313_SPI_TRANSPORT *t, uint64_t a, uint32_t mask, uint32_t val) {
  uint32_t old = Rd(t, a);
  if ((old & GPIO_LOCK) && (old & mask) != val) return false;
  Wr(t, a, (old & ~mask) | val);
  /* Input DATA is live, never compare it to the written value. */
  return (Rd(t, a) & mask) == val;
}
static void Quiesce(J313_SPI_TRANSPORT *t) {
  if (t->ControllerLive) {
    Wr(t,t->Resources.SpiBase+P_CTRL,0);
    Mask(t,t->Resources.SpiBase+P_PIN,P_CS,P_CS);
    Wr(t,t->Resources.SpiBase+P_IE_XFER,0);
    Wr(t,t->Resources.SpiBase+P_IE_FIFO,0);
  }
  if (t->GpioLive) Mask(t,t->Resources.ApBase+195*4,GPIO_DATA,0);
  t->Initialized = false;
  t->ControllerLive = t->GpioLive = false;
}
void J313SpiTransportStop(J313_SPI_TRANSPORT *t) {
  if (!t || t->Busy) return;
  Quiesce(t);
}
J313_SPI_STATUS J313SpiTransportInit(J313_SPI_TRANSPORT *t,
                        const J313_SPI_RESOURCES *r, const J313_SPI_IO *io) {
  unsigned i;
  J313_SPI_IO selected;
  if (!t || !ResourcesValid(r)) return J313_SPI_INVALID;
  if (t->Busy || t->Initialized || t->ControllerLive || t->GpioLive) return J313_SPI_BUSY;
  if (io) selected = *io;
  else {
#ifdef J313_HOST_TEST
    return J313_SPI_INVALID;
#else
    selected.Context = NULL; selected.Read32 = RealRead; selected.Write32 = RealWrite;
    selected.NowUs = RealNow; selected.DelayUs = RealDelay;
#endif
  }
  if (!selected.Read32 || !selected.Write32 || !selected.NowUs || !selected.DelayUs)
    return J313_SPI_INVALID;
  t->Resources = *r; t->Io = selected; t->Busy = true;
  for (i = 0; i < r->PowerCount; ++i)
    if (!PowerOn(t,r->PmgrBase+r->PowerOffset[i])) goto fail;
  t->ControllerLive = true;
  Wr(t,r->SpiBase+P_CTRL,0);
  Wr(t,r->SpiBase+P_IE_XFER,0); Wr(t,r->SpiBase+P_IE_FIFO,0);
  Mask(t,r->SpiBase+P_PIN,P_CS,P_CS);
  for (i = 46; i <= 49; ++i)
    if (!GpioSet(t,r->ApBase+i*4,GPIO_PERIPH|GPIO_INPUT,(1u<<5)|GPIO_INPUT)) goto fail;
  /* NUB13 is polled, IRQ mode OFF; mode3 would drive an unhandled level IRQ. */
  if (!GpioSet(t,r->NubBase+13*4,GPIO_PERIPH|GPIO_MODE|GPIO_INPUT,
               GPIO_MODE|GPIO_INPUT)) goto fail;
  /* Mark cleanup ownership before the first enable write: a failed readback
   * may still mean the peripheral was enabled. */
  t->GpioLive = true;
  if (!GpioSet(t,r->ApBase+195*4,GPIO_PERIPH|GPIO_MODE|GPIO_DATA,3u)) goto fail;
  if (!Delay(t,5000)) goto fail;
  if (!GpioSet(t,r->ApBase+195*4,GPIO_PERIPH|GPIO_MODE|GPIO_DATA,2u)) goto fail;
  if (!Delay(t,5000)) goto fail;
  if (!GpioSet(t,r->ApBase+195*4,GPIO_PERIPH|GPIO_MODE|GPIO_DATA,3u)) goto fail;
  if (!Delay(t,50000)) goto fail;
  t->Initialized = true; t->Busy = false;
  return J313_SPI_OK;
fail:
  Quiesce(t); t->Busy = false; return J313_SPI_IO_ERROR;
}
J313_SPI_STATUS J313SpiTransportReady(J313_SPI_TRANSPORT *t, bool *ready) {
  if (!ready) return J313_SPI_INVALID;
  *ready = false;
  if (!t || !t->Initialized) return J313_SPI_NOT_READY;
  if (t->Busy) return J313_SPI_BUSY;
  *ready = !(Rd(t,t->Resources.NubBase+13*4) & GPIO_DATA);
  return J313_SPI_OK;
}
static uint32_t PioRead(void *c, uint32_t off) {
  J313_SPI_TRANSPORT *t = c; return Rd(t,t->Resources.SpiBase+off);
}
static void PioWrite(void *c, uint32_t off, uint32_t val) {
  J313_SPI_TRANSPORT *t = c; Wr(t,t->Resources.SpiBase+off,val);
}
static uint64_t PioNow(void *c) {
  J313_SPI_TRANSPORT *t = c; return t->Io.NowUs(t->Io.Context);
}
static void PioDelay(void *c, uint32_t us) {
  J313_SPI_TRANSPORT *t = c; t->Io.DelayUs(t->Io.Context,us);
}
static J313_SPI_STATUS Transaction(J313_SPI_TRANSPORT *t, const struct pio_segment *s, size_t n) {
  struct pio_io io = {t,PioRead,PioWrite,PioNow,PioDelay};
  /* 120MHz SPI3 reference. Raw divider 120 is <=1MHz for BOTH published
   * divisor interpretations (raw, raw+1); do not claim measured SCK. */
  const struct pio_config cfg = {true,true,true,false,false,120,65,65,250,5000,20000};
  if (!t || !t->Initialized) return J313_SPI_NOT_READY;
  if (t->Busy) return J313_SPI_BUSY;
  t->Busy = true;
  t->LastTransfer = pio_transaction(&io,&cfg,s,n);
  t->Busy = false;
  if (t->LastTransfer.status == PIO_OK) return J313_SPI_OK;
  /* Partial frames are never published or retried automatically. */
  Quiesce(t);
  return t->LastTransfer.status == PIO_TIMEOUT || t->LastTransfer.status == PIO_STEP_LIMIT ?
         J313_SPI_TIMEOUT : J313_SPI_IO_ERROR;
}
J313_SPI_STATUS J313SpiTransportReadPacket(J313_SPI_TRANSPORT *t, uint8_t packet[256]) {
  uint8_t scratch[256];
  struct pio_segment s = {NULL,scratch,sizeof(scratch),0};
  J313_SPI_STATUS status;
  bool ready;
  unsigned i;
  if (!packet) return J313_SPI_INVALID;
  status = J313SpiTransportReady(t,&ready);
  if (status != J313_SPI_OK) return status;
  if (!ready) return J313_SPI_NOT_READY;
  status = Transaction(t,&s,1);
  if (status == J313_SPI_OK) for (i=0;i<256;++i) packet[i]=scratch[i];
  return status;
}
J313_SPI_STATUS J313SpiTransportWritePacket(J313_SPI_TRANSPORT *t, const uint8_t packet[256]) {
  uint8_t ack[4] = {0};
  struct pio_segment s[2] = {{packet,NULL,256,200},{NULL,ack,4,0}};
  J313_SPI_STATUS status;
  if (!packet) return J313_SPI_INVALID;
  status = Transaction(t,s,2);
  if (status != J313_SPI_OK) return status;
  if (ack[0]!=0xac || ack[1]!=0x27 || ack[2]!=0x68 || ack[3]!=0xd5) {
    Quiesce(t); return J313_SPI_ACK_ERROR;
  }
  return J313_SPI_OK;
}
