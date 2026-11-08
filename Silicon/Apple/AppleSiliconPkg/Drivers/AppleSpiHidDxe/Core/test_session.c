/* SPDX-License-Identifier: GPL-2.0-only
 * Synthetic packets only: not hardware evidence. Links production state machine.
 */
#include "j313_session.h"
#include "key_queue.h"
#include <assert.h>
#include <stdio.h>
static struct j313_session s;
static struct sh_init_action a;
static hk_result keys;
static const uint8_t desc[] = {5,1,9,6,0xa1,1,5,7,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};
static void le(uint8_t *p, size_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static int packet(uint8_t flags, uint8_t dev, uint8_t type, uint8_t subtype,
 uint8_t iface, uint8_t id, uint16_t response, const uint8_t *data, size_t n)
{
 uint8_t p[256]={0};
 assert(n<=236); p[0]=flags;p[1]=dev;le(p+6,n+10);
 p[8]=type;p[9]=subtype;p[10]=iface;p[11]=id;le(p+12,response);le(p+14,n);
 memcpy(p+16,data,n);le(p+16+n,sh_crc16(p+8,n+8));le(p+254,sh_crc16(p,254));
 return js_packet(&s,1,p,&a,&keys);
}
static void start(void)
{
 uint8_t p[256]={0};p[6]=4;p[8]=0xa0;p[9]=0x80;le(p+254,sh_crc16(p,254));
 js_start(&s,0);assert(s.phase==JS_INIT);assert(js_packet(&s,0,p,&a,&keys)==JS_TX);
}
static void handshake(int invalid)
{
 uint8_t p[24]={0};int r;
 start();
 for(unsigned i=0;i<5;i++) {
  size_t n; const uint8_t *b=p;
  memset(p,0,sizeof(p));
  if(i==0){n=24;p[4]=3;p[6]=0xac;p[7]=5;}
  else if(i<3){n=16;p[1]=s.init.pending_iface;p[7]=8;}
  else {b=desc;n=sizeof(desc);if(invalid && i==3){b=p;n=1;}}
  r=packet(0x40,0xd0,0x20,s.init.pending_type,s.init.pending_iface,
   s.init.pending_id,s.init.pending_response,b,n);
  assert(r==(i<4?JS_TX:invalid?JS_ERROR:JS_PUBLISH));
 }
}
int main(void)
{
 uint8_t p[256]={0}, report[6]={4};kq_queue q;kq_key k;
 handshake(0);assert(s.phase==JS_READY);kq_init(&q);
 assert(packet(0x20,1,0x10,0,0,0,0,report,6)==JS_KEYS);
 assert(kq_feed(&q,&keys)==KQ_OK);assert(kq_pop(&q,&k)==KQ_OK && k.unicode_char=='a');
 assert(packet(0x20,1,0x10,0,0,0,0,report,6)==JS_KEYS && !keys.count);
 assert(packet(0x20,2,0x10,0,0,0,0,report,6)==JS_WAIT);
 for(unsigned i=0;i<8;i++)assert(js_packet(&s,2,p,&a,&keys)==(i==7?JS_ERROR:JS_DROP));
 assert(s.phase==JS_FAILED);assert(js_tick(&s,3)==JS_ERROR);
 handshake(1);assert(s.phase==JS_FAILED);
 assert(s.failure_domain==JS_DOMAIN_DESCRIPTOR && s.failure_phase==SH_INIT_DONE);
 {int detail=s.failure_detail;js_stop(&s);assert(js_tick(&s,2)==JS_ERROR);
 assert(s.failure_domain==JS_DOMAIN_DESCRIPTOR && s.failure_detail==detail);}
 /* no publish on invalid descriptor */
 js_start(&s,0);assert(js_tick(&s,1000)==JS_ERROR);
 assert(s.failure_phase==SH_INIT_BOOT && s.failure_domain==JS_DOMAIN_INIT && s.failure_detail==SH_INIT_TIMEOUT);
 js_stop(&s);assert(s.failure_phase==SH_INIT_BOOT);
 start();assert(js_tick(&s,1000)==JS_ERROR); /* request deadline */
 js_start(&s,10);assert(js_tick(&s,9)==JS_ERROR);
 handshake(0);js_stop(&s);assert(js_packet(&s,2,p,&a,&keys)==JS_ERROR && !keys.count);
 start();assert(packet(0x40,0xd0,0x20,1,0xd0,99,0,report,6)==JS_DROP); /* wrong ID: never accepted */
 assert(s.phase==JS_INIT && s.init.requests==1 && s.init.started_ms==0);
 assert(s.init.reject_detail==3 && s.init.reject_mask==32);
 assert(js_tick(&s,1000)==JS_ERROR); /* unrelated replies cannot extend deadline */
 start();
 assert(packet(0x20,2,0x10,0,0,0,0,report,6)==JS_DROP); /* touch report interleaved */
 memset(p,0,sizeof(p));p[4]=3;p[6]=0xac;p[7]=5;
 assert(packet(0x40,0xd0,0x20,1,0xd0,0,0,p,24)==JS_TX);
 assert(s.phase==JS_INIT && s.init.phase==SH_INIT_IFACE1);
 start();
 for(unsigned i=0;i<63;i++)
  assert(packet(0x20,2,0x10,0,0,0,0,report,6)==JS_DROP);
 assert(packet(0x20,2,0x10,0,0,0,0,report,6)==JS_ERROR); /* event budget retained */
 handshake(0);memset(p,0,sizeof(p));p[0]=0x20;p[1]=1;p[4]=10;p[6]=1;
 le(p+254,sh_crc16(p,254));assert(js_packet(&s,2,p,&a,&keys)==JS_WAIT);
 assert(js_tick(&s,102)==JS_ERROR); /* fragment cannot live indefinitely */
 assert(s.failure_domain==JS_DOMAIN_FRAGMENT);
 start();memset(p,0,sizeof(p));p[1]=1;
 assert(js_packet(&s,2,p,&a,&keys)==JS_ERROR);
 assert(s.failure_domain==JS_DOMAIN_PACKET && s.failure_detail==SH_PACKET_CRC && s.failure_phase==SH_INIT_DEVICE);
 js_start(&s,0);assert(js_packet(&s,0,p,&a,&keys)==JS_DROP);
 assert(s.init.reject_detail==2);assert(js_tick(&s,1000)==JS_ERROR);
 assert(s.failure_detail==SH_INIT_TIMEOUT && s.init.reject_detail==2);
 puts("production session: handshake, live descriptor, keys, bounds, stop PASS");
 return 0;
}
