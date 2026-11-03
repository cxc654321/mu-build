/* SPDX-License-Identifier: GPL-2.0-only
 * Host-only synthetic MMIO model, not a hardware specification.
 */
#include "j313_spi_pio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum fault { NORMAL, STICKY, EARLY_TX, LATE_RX, SHORT_RX, OVERFLOW,
 UNDERRUN, EXTRA_RX, BAD_LEVEL, REVERSE, FROZEN, SLOW, NO_DONE };
struct model {
 uint32_t regs[0x170/4], tq[16], rq[16];
 unsigned tn, rn, th, rh, produced, sent, ticks, runs, reads, writes;
 enum fault fault;
 uint64_t now, asserted, deasserted, run_at[4], stop_at[4];
 unsigned assertions, deassertions, empty_reads, now_calls;
};
static uint8_t data(unsigned i) { return (uint8_t)(i * 37u + 11u); }
static void push_rx(struct model *m) {
 assert(m->rn < 16);
 m->rq[(m->rh + m->rn++) % 16] = data(m->produced++);
}
static void tick(struct model *m) {
 uint32_t tx, rx;
 if (!(m->regs[P_CTRL/4] & P_RUN)) return;
 ++m->ticks;
 if (m->fault == STICKY) return;
 if (m->fault == OVERFLOW) m->regs[P_IF_FIFO/4] |= P_TX_OVERFLOW;
 if (m->fault == UNDERRUN) m->regs[P_IF_FIFO/4] |= P_RX_UNDERRUN;
 if (m->fault == EARLY_TX) { m->regs[P_IF_XFER/4] |= P_TX_DONE; return; }
 if (m->fault == SLOW && m->ticks % 20) return;
 tx = m->regs[P_TXCNT/4]; rx = m->regs[P_RXCNT/4];
 if (m->tn) {
  assert(m->tq[m->th] == data(m->sent));
  m->th = (m->th + 1) % 16; --m->tn; ++m->sent;
 }
 if (rx && m->produced < rx) {
  if ((m->fault == LATE_RX || m->fault == SHORT_RX) && m->produced == rx-1) {
   m->regs[P_IF_XFER/4] |= P_RX_DONE;
   if (m->fault == LATE_RX && m->ticks >= rx+7) push_rx(m);
  } else if (m->rn < 16) push_rx(m);
 }
 if (m->fault != NO_DONE) {
  if (tx && m->sent == tx) m->regs[P_IF_XFER/4] |= P_TX_DONE;
  if (rx && m->produced == rx) m->regs[P_IF_XFER/4] |= P_RX_DONE;
 }
 if (m->fault == EXTRA_RX && rx && m->produced == rx) push_rx(m);
}
static uint32_t read32(void *ctx, uint32_t reg) {
 struct model *m = ctx; ++m->reads;
 assert(reg < sizeof(m->regs) && !(reg % 4));
 if (reg == P_FIFOSTAT) {
  if (m->fault == BAD_LEVEL) return 17u << 24;
  return (m->tn << 8) | (m->rn << 24) | (m->tn == 16 ? 16u : 0u) |
         (m->rn == 0 ? 1u << 20 : 0u);
 }
 if (reg == P_RXDATA) {
  uint32_t v;
  if (!m->rn) { ++m->empty_reads; assert(0); }
  v = m->rq[m->rh]; m->rh = (m->rh + 1) % 16; --m->rn; return v;
 }
 if (reg == P_IF_FIFO) {
  /* W1C latches plus combinational level flags: clearing cannot remove level. */
  return m->regs[reg/4] | (m->tn <= 8 ? 1u << 5 : 0u) |
         (m->rn >= 8 ? 1u << 4 : 0u) |
         (m->fault == STICKY ? (1u << 4) | (1u << 5) : 0u);
 }
 return m->regs[reg/4];
}
static void write32(void *ctx, uint32_t reg, uint32_t v) {
 struct model *m = ctx; ++m->writes;
 assert(reg < sizeof(m->regs) && !(reg % 4));
 if (reg == P_IF_XFER || reg == P_IF_FIFO) { m->regs[reg/4] &= ~v; return; }
 if (reg == P_TXDATA) {
  assert(m->tn < 16); m->tq[(m->th + m->tn++) % 16] = v; return;
 }
 if (reg == P_PIN && ((m->regs[reg/4] ^ v) & P_CS)) {
  if (v & P_CS) { ++m->deassertions; m->deasserted = m->now; }
  else { ++m->assertions; m->asserted = m->now; }
 }
 if (reg == P_CTRL) {
  if ((m->regs[reg/4] & P_RUN) && !(v & P_RUN)) {
   assert(m->runs && m->runs <= 4); m->stop_at[m->runs-1] = m->now;
  }
  if (v & P_RESET) { m->tn=m->rn=m->th=m->rh=m->produced=m->sent=m->ticks=0; }
  if (v & P_RUN) {
   assert(!(m->regs[P_PIN/4] & P_CS));
   assert(m->regs[P_IE_XFER/4] == 0 && m->regs[P_IE_FIFO/4] == 0);
   assert(m->runs < 4); m->run_at[m->runs++] = m->now;
  }
 }
 m->regs[reg/4] = v;
}
static uint64_t now_us(void *ctx) {
 struct model *m = ctx;
 if (m->fault == REVERSE && ++m->now_calls == 20) return m->now-2;
 return m->now;
}
static void delay_us(void *ctx, uint32_t us) {
 struct model *m = ctx;
 if (m->fault == FROZEN) return;
 m->now += us; tick(m);
}
static struct pio_config config(void) {
 struct pio_config c = {true,true,true,false,false,15,65,65,250,10000,100000};
 return c;
}
static struct pio_io io_for(struct model *m) {
 struct pio_io io = {m,read32,write32,now_us,delay_us}; return io;
}
static void init(struct model *m, enum fault f) {
 memset(m, 0, sizeof(*m)); m->fault=f; m->now=100;
 m->regs[P_PIN/4] = P_CS | 1u;
 m->regs[P_SHIFTCFG/4] = (1u << 24) | (63u << 16) | (1u << 10);
 m->regs[P_PINCFG/4] = (1u << 9) | (1u << 2);
}
static void clean(const struct model *m, struct pio_result r) {
 assert(r.started); assert(m->regs[P_CTRL/4] == 0);
 assert(m->regs[P_PIN/4] == (P_CS | 1u));
 assert(m->regs[P_IE_XFER/4] == 0 && m->regs[P_IE_FIFO/4] == 0);
 assert(m->empty_reads == 0);
 assert(m->regs[P_SHIFTCFG/4] == ((8u << 16) | (1u << 10)));
 assert(m->regs[P_PINCFG/4] == ((1u << 1) | (1u << 2)));
}
static void success_lengths(void) {
 const size_t sizes[] = {1,4,15,16,17,31,32,255,256,513};
 uint8_t tx[513], rx[515]; unsigned i, mode, pol;
 for (i=0;i<sizeof(tx);++i) tx[i]=data(i);
 for (pol=0;pol<4;++pol) for (mode=0;mode<3;++mode)
 for (i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
  struct model m; struct pio_io io; struct pio_config c=config();
  struct pio_result r; size_t j, len=sizes[i];
  struct pio_segment s = {mode != 1 ? tx : NULL, mode != 0 ? rx+1 : NULL,len,0};
  init(&m,NORMAL); io=io_for(&m); memset(rx,0xa5,sizeof(rx));
  c.cpol=!!(pol&2); c.cpha=!!(pol&1);
  r=pio_transaction(&io,&c,&s,1); clean(&m,r);
  assert(r.status==PIO_OK && r.segments_completed==1);
  assert(r.tx_queued==(s.tx?len:0) && r.tx_completed==(s.tx?len:0));
  assert(r.rx_completed==(s.rx?len:0));
  if(s.rx) for(j=0;j<len;++j) assert(rx[j+1]==data((unsigned)j));
  assert(rx[0]==0xa5 && rx[len+1]==0xa5);
  assert(m.regs[P_CFG/4]==(P_MODE_IRQ | (c.cpol?4u:0u) | (c.cpha?2u:0u)));
  assert(m.regs[P_CLKDIV/4]==15 && m.assertions==1 && m.deassertions==1);
  assert(m.run_at[0]-m.asserted>=65 && m.deasserted-m.stop_at[0]>=65);
 }
 puts("PASS 120 success cases: 10 lengths x TX/RX/full-duplex x 4 modes");
}
static void multi(void) {
 uint8_t tx[256],rx[4]; unsigned i; struct model m; struct pio_io io;
 struct pio_config c=config(); struct pio_result r;
 struct pio_segment s[2]={{tx,NULL,256,200},{NULL,rx,4,0}};
 for(i=0;i<256;++i) tx[i]=data(i);
 init(&m,NORMAL); io=io_for(&m); r=pio_transaction(&io,&c,s,2); clean(&m,r);
 assert(r.status==PIO_OK && r.segments_completed==2);
 assert(r.tx_completed==256 && r.rx_completed==4);
 assert(m.assertions==1 && m.deassertions==1 && m.runs==2);
 assert(m.run_at[1]-m.stop_at[0]>=200);
 assert(m.asserted-100>=250 && m.now-m.deasserted>=250);
 /* Deadline also covers inter-segment delay and hold/inactive. */
 init(&m,NORMAL); io=io_for(&m); c.timeout_us=700;
 r=pio_transaction(&io,&c,s,2); clean(&m,r); assert(r.status==PIO_TIMEOUT);
 assert(r.segments_completed==1);
 init(&m,NORMAL); io=io_for(&m); c=config(); c.timeout_us=900;
 r=pio_transaction(&io,&c,s,2); clean(&m,r); assert(r.status==PIO_TIMEOUT);
 assert(r.segments_completed==2);
 init(&m,NORMAL); io=io_for(&m); c=config();
 c.setup_us=3; c.hold_us=7; c.inactive_us=11; s[0].delay_after_us=13;
 r=pio_transaction(&io,&c,s,2); clean(&m,r); assert(r.status==PIO_OK);
 assert(m.run_at[0]-m.asserted==3 && m.deasserted-m.stop_at[1]==7);
 assert(m.asserted-100==11 && m.now-m.deasserted==11);
 assert(m.run_at[1]-m.stop_at[0]==13);
 puts("PASS TX256 + 200us + RX4, continuous CS, configurable timing and global delay bounds");
}
static void failures(void) {
 const enum fault faults[]={STICKY,EARLY_TX,LATE_RX,SHORT_RX,OVERFLOW,UNDERRUN,
 EXTRA_RX,BAD_LEVEL,REVERSE,FROZEN,SLOW,NO_DONE};
 const enum pio_status expect[]={PIO_TIMEOUT,PIO_PREMATURE_TX,PIO_OK,PIO_TIMEOUT,
 PIO_FIFO_ERROR,PIO_FIFO_ERROR,PIO_EXTRA_RX,PIO_BAD_FIFO_LEVEL,
 PIO_CLOCK_REVERSED,PIO_STEP_LIMIT,PIO_TIMEOUT,PIO_TIMEOUT};
 uint8_t tx[32],rx[34]; unsigned i,j;
 for(j=0;j<32;++j) tx[j]=data(j);
 for(i=0;i<sizeof(faults)/sizeof(faults[0]);++i) {
  struct model m; struct pio_io io; struct pio_config c=config(); struct pio_result r;
  struct pio_segment s={tx,rx+1,32,0};
  init(&m,faults[i]); io=io_for(&m); memset(rx,0xa5,sizeof(rx));
  c.timeout_us=500; c.step_budget=5000;
  /* Late tail needs enough budget for final hold+inactive too. */
  if(faults[i]==LATE_RX) c.timeout_us=2000;
  if(faults[i]==FROZEN) c.setup_us=c.inactive_us=0;
  r=pio_transaction(&io,&c,&s,1); clean(&m,r);
  if(r.status!=expect[i]) fprintf(stderr,"fault %u got %d expected %d\n",i,r.status,expect[i]);
  assert(r.status==expect[i]); assert(rx[0]==0xa5 && rx[33]==0xa5);
  assert(r.steps<=c.step_budget);
  if(faults[i]==STICKY) assert(r.tx_completed==0 && r.rx_completed==0);
  if(faults[i]==SHORT_RX) assert(r.rx_completed==31);
  if(faults[i]==LATE_RX) assert(r.rx_completed==32);
  if(faults[i]==OVERFLOW) assert(r.fifo_error_flags & P_TX_OVERFLOW);
  if(faults[i]==UNDERRUN) assert(r.fifo_error_flags & P_RX_UNDERRUN);
 }
 puts("PASS 12 fault/tail scenarios, error flags, bounded waits, guards and cleanup");
}
static void invalid(void) {
 struct model m; struct pio_io io; struct pio_config c; struct pio_result r;
 uint8_t b=0; struct pio_segment s={&b,NULL,1,0}; unsigned i;
 for(i=0;i<9;++i) {
  init(&m,NORMAL); io=io_for(&m); c=config(); s.len=1;
  switch(i) {
   case 0:c.exclusive_owner=false;break; case 1:c.resources_validated=false;break;
   case 2:c.bus_config_validated=false;break; case 3:c.clock_divider=0;break;
   case 4:c.clock_divider=2048;break; case 5:c.timeout_us=0;break;
   case 6:c.step_budget=0;break; case 7:s.len=0;break; case 8:io.read32=NULL;break;
  }
  r=pio_transaction(&io,&c,&s,1); assert(r.status==PIO_INVALID && !r.started);
  assert(!m.reads && !m.writes);
 }
 puts("PASS 9 invalid configurations: zero IO");
}
int main(void) { success_lengths(); multi(); failures(); invalid(); puts("ALL HOST TESTS PASSED"); return 0; }
