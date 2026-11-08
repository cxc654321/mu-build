/* SPDX-License-Identifier: GPL-2.0-only
 * Synthetic MMIO model; exercises production transport + production PIO.
 * Not a hardware or waveform validation.
 */
#include "../J313SpiTransport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SPI 0x23510c000ULL
#define AP 0x23c100000ULL
#define NUB 0x23d1f0000ULL
#define PM 0x23b700000ULL
#define MINI 0x23d280000ULL
struct mock {
 uint32_t spi[0x170/4],ap[212],nub[23],pm[0x14000/4],mini[0x4000/4];
 unsigned reads,writes,power,assertions,deassertions,tn,rn,produced,consumed,sent;
 unsigned enable_count, enable_value[8], runs;
 uint64_t now,enable_time[8],run_time[8],stop_time[8];
 bool power_stuck,stuck,frozen,bad_ack,ready,reverse;
 unsigned fail_power;
 bool finish_between_reads,premature_tx;
 unsigned read_completions;
};
static uint32_t *reg(struct mock *m,uint64_t a) {
 assert(!(a&3));
 if(a>=SPI && a<SPI+sizeof(m->spi)) return &m->spi[(a-SPI)/4];
 if(a>=AP && a<AP+sizeof(m->ap)) return &m->ap[(a-AP)/4];
 if(a>=NUB && a<NUB+sizeof(m->nub)) return &m->nub[(a-NUB)/4];
 if(a>=PM && a<PM+sizeof(m->pm)) return &m->pm[(a-PM)/4];
 if(a>=MINI && a<MINI+sizeof(m->mini)) return &m->mini[(a-MINI)/4];
 assert(0); return NULL;
}
static uint32_t rd(void *c,uint64_t a) {
 struct mock *m=c; ++m->reads;
 if(a==NUB+52) return (*reg(m,a)&~1u)|!m->ready;
 if(a==SPI+P_FIFOSTAT||a==SPI+P_IF_XFER) {
  uint32_t v=a==SPI+P_FIFOSTAT ? m->tn<<8|m->rn<<24 : m->spi[P_IF_XFER/4];
  /* Snapshot the first read, then retire the actual last queued byte before
   * the other MMIO read. Independent of which register is sampled first. */
  if(m->finish_between_reads&&(m->spi[P_CTRL/4]&P_RUN)&&m->tn==1&&
     m->sent+1==m->spi[P_TXCNT/4]) {
   --m->tn;++m->sent;m->spi[P_IF_XFER/4]|=P_TX_DONE;++m->read_completions;
  }
  return v;
 }
 if(a==SPI+P_RXDATA) {
  static const uint8_t ack[]={0xac,0x27,0x68,0xd5};
  uint32_t v;
  assert(m->rn); --m->rn;
  v=m->spi[P_RXCNT/4]==4 ? ack[m->consumed] : (uint8_t)m->consumed;
  ++m->consumed; return m->bad_ack?0:v;
 }
 return *reg(m,a);
}
static void wr(void *c,uint64_t a,uint32_t v) {
 struct mock *m=c; uint32_t *p=reg(m,a); ++m->writes;
 if((a>=PM&&a<PM+sizeof(m->pm))||(a>=MINI&&a<MINI+sizeof(m->mini))) {
  ++m->power; *p=(m->power_stuck||m->power==m->fail_power)?v:((v&~255u)|255u); return;
 }
 /* All GPIO/controller access must follow all three mocked power nodes. */
 assert(m->power>=3);
 if(a==AP+195*4) {
  assert(m->enable_count<8);
  m->enable_value[m->enable_count]=v&1;
  m->enable_time[m->enable_count++]=m->now;
 }
 if(a==SPI+P_IF_XFER||a==SPI+P_IF_FIFO) {*p&=~v;return;}
 if(a==SPI+P_TXDATA) {assert(m->tn<16);++m->tn;return;}
 if(a==SPI+P_PIN && ((*p^v)&P_CS)) {
  if(v&P_CS) ++m->deassertions; else ++m->assertions;
 }
 if(a==SPI+P_CTRL) {
  if((*p&P_RUN)&&!(v&P_RUN)) m->stop_time[m->runs-1]=m->now;
  if(v&P_RESET) m->tn=m->rn=m->produced=m->consumed=m->sent=0;
  if(v&P_RUN) {
   assert(!(m->spi[P_PIN/4]&P_CS)); assert(m->runs<8);
   m->run_time[m->runs++]=m->now;
  }
 }
 *p=v;
}
static uint64_t now(void *c) {return ((struct mock*)c)->now;}
static void delay(void *c,uint32_t us) {
 struct mock *m=c;
 if(m->reverse)m->now-=us;
 else if(!m->frozen)m->now+=us;
 if(!(m->spi[P_CTRL/4]&P_RUN)||m->stuck)return;
 /* Leave the last byte for progress between the two MMIO snapshots. */
 if(m->tn&&!(m->finish_between_reads&&m->tn==1&&m->sent+1==m->spi[P_TXCNT/4])){--m->tn;++m->sent;}
 if(m->premature_tx&&m->sent)m->spi[P_IF_XFER/4]|=P_TX_DONE;
 if(m->produced<m->spi[P_RXCNT/4]&&m->rn<16){++m->rn;++m->produced;}
 if(m->spi[P_TXCNT/4]&&m->sent==m->spi[P_TXCNT/4])m->spi[P_IF_XFER/4]|=P_TX_DONE;
 if(m->spi[P_RXCNT/4]&&m->produced==m->spi[P_RXCNT/4])m->spi[P_IF_XFER/4]|=P_RX_DONE;
}
static J313_SPI_RESOURCES resources(void) {
 J313_SPI_RESOURCES r={0};
 memcpy(r.TargetType,"j313",5);r.ChipId=0x8103;
 r.SpiBase=SPI;r.SpiSize=0x4000;r.ApBase=AP;r.ApSize=0x100000;
 r.NubBase=NUB;r.NubSize=0x4000;r.PmgrBase=PM;r.PmgrSize=0x1b84000;
 r.ClockGate=0x34;r.PowerCount=3;
 r.PowerOffset[0]=0x130;r.PowerOffset[1]=0x1b80080;r.PowerOffset[2]=0x258;
 return r;
}
static J313_SPI_IO io(struct mock *m) {J313_SPI_IO i={m,rd,wr,now,delay};return i;}
static void clean(struct mock *m,J313_SPI_TRANSPORT *t) {
 assert(!t->Initialized);assert(!m->spi[P_CTRL/4]);assert(m->spi[P_PIN/4]&P_CS);
 assert(!m->spi[P_IE_XFER/4]&&!m->spi[P_IE_FIFO/4]);assert(!(m->ap[195]&1));
}
static void success(void) {
 struct mock m={0};J313_SPI_TRANSPORT t={0};J313_SPI_RESOURCES r=resources();J313_SPI_IO i=io(&m);
 uint8_t packet[256]={0};bool ready;unsigned k;
 m.ap[46]=0x8000;
 assert(J313SpiTransportInit(&t,&r,&i)==J313_SPI_OK);
 assert(m.power==3&&m.enable_count==3&&m.now==60000);
 assert(m.enable_value[0]==1&&m.enable_value[1]==0&&m.enable_value[2]==1);
 assert(m.enable_time[1]-m.enable_time[0]>=5000);
 assert(m.enable_time[2]-m.enable_time[1]>=5000);
 assert((m.ap[46]&0x8260)==0x8220);
 assert(J313SpiTransportReady(&t,&ready)==J313_SPI_OK&&!ready);
 assert(J313SpiTransportReadPacket(&t,packet)==J313_SPI_NOT_READY);
 assert(J313SpiTransportWritePacket(&t,packet)==J313_SPI_OK);
 assert(m.assertions==1&&m.runs==2&&m.run_time[1]-m.stop_time[0]>=200);
 assert(t.LastTransfer.segments_completed==2&&t.LastTransfer.tx_completed==256&&t.LastTransfer.rx_completed==4);
 m.ready=true;
 assert(J313SpiTransportReadPacket(&t,packet)==J313_SPI_OK);
 for(k=0;k<256;++k)assert(packet[k]==(uint8_t)k);
 assert(m.assertions==2&&m.runs==3);
 J313SpiTransportStop(&t);clean(&m,&t);
 k=m.writes;J313SpiTransportStop(&t);assert(m.writes==k);
}
static void invalid(void) {
 unsigned n;
 for(n=0;n<12;++n){
  struct mock m={0};J313_SPI_TRANSPORT t={0};J313_SPI_RESOURCES r=resources();J313_SPI_IO i=io(&m);
  switch(n){
  case 0:r.TargetType[3]='4';break;case 1:r.ChipId=0x8112;break;
  case 2:r.SpiBase+=4;break;case 3:r.ApSize=4;break;case 4:r.PowerCount=17;break;
  case 5:r.PowerOffset[0]=0x258;break;case 6:r.ClockGate=0x35;break;
  case 7:r.PowerOffset[1]=0x1b84000;break;case 8:r.PowerOffset[1]=0x14000;break;
  case 9:r.PowerOffset[1]=0x1b80081;break;case 10:r.PmgrSize=0x14000;break;
  default:i.Read32=NULL;break;}
  assert(J313SpiTransportInit(&t,&r,&i)==J313_SPI_INVALID);assert(!m.reads&&!m.writes);
 }
}
static void faults(void) {
 unsigned n;
 for(n=0;n<10;++n){
  struct mock m={0};J313_SPI_TRANSPORT t={0};J313_SPI_RESOURCES r=resources();J313_SPI_IO i=io(&m);
  uint8_t packet[256];memset(packet,0xa5,sizeof(packet));
  if(n>=7){m.fail_power=n-6;assert(J313SpiTransportInit(&t,&r,&i)!=J313_SPI_OK);assert(m.power==m.fail_power&&!m.enable_count&&!m.runs);continue;}
  if(n==0){m.power_stuck=true;assert(J313SpiTransportInit(&t,&r,&i)!=J313_SPI_OK);assert(m.power==1&&m.now<=10000);continue;}
  if(n==1){m.frozen=true;assert(J313SpiTransportInit(&t,&r,&i)!=J313_SPI_OK);clean(&m,&t);continue;}
  if(n==2){m.ap[46]=1u<<21;assert(J313SpiTransportInit(&t,&r,&i)!=J313_SPI_OK);clean(&m,&t);continue;}
  assert(J313SpiTransportInit(&t,&r,&i)==J313_SPI_OK);
  if(n==3){m.bad_ack=true;assert(J313SpiTransportWritePacket(&t,packet)==J313_SPI_ACK_ERROR);}
  else {m.ready=true;m.stuck=true;m.frozen=n==5;m.reverse=n==6;
   assert(J313SpiTransportReadPacket(&t,packet)==(n==6?J313_SPI_IO_ERROR:J313_SPI_TIMEOUT));
   for(unsigned k=0;k<256;++k)assert(packet[k]==0xa5);
  }
  clean(&m,&t);
 }
}
static void completion_between_reads(void) {
 for(unsigned bad=0;bad<2;++bad) {
  struct mock m={0};J313_SPI_TRANSPORT t={0};J313_SPI_RESOURCES r=resources();J313_SPI_IO i=io(&m);
  uint8_t packet[256]={0};
  assert(J313SpiTransportInit(&t,&r,&i)==J313_SPI_OK);
  m.finish_between_reads=true;m.bad_ack=bad;
  assert(J313SpiTransportWritePacket(&t,packet)==(bad?J313_SPI_ACK_ERROR:J313_SPI_OK));
  assert(m.read_completions==1);
  assert(t.LastTransfer.status==PIO_OK&&t.LastTransfer.tx_queued==256&&t.LastTransfer.tx_completed==256);
  assert(t.LastTransfer.segments_completed==2&&t.LastTransfer.rx_completed==4);
  if(bad)clean(&m,&t);else {J313SpiTransportStop(&t);clean(&m,&t);}
 }
}
static void premature_completion(void) {
 struct mock m={0};J313_SPI_TRANSPORT t={0};J313_SPI_RESOURCES r=resources();J313_SPI_IO i=io(&m);
 uint8_t packet[256]={0};
 assert(J313SpiTransportInit(&t,&r,&i)==J313_SPI_OK);m.premature_tx=true;
 assert(J313SpiTransportWritePacket(&t,packet)==J313_SPI_IO_ERROR);
 assert(t.LastTransfer.status==PIO_PREMATURE_TX&&t.LastTransfer.tx_queued<256);
 assert(!t.LastTransfer.segments_completed&&!t.LastTransfer.rx_completed&&m.runs==1);
 clean(&m,&t);
}
int main(void){success();invalid();faults();premature_completion();completion_between_reads();puts("production transport mock tests: PASS");return 0;}
