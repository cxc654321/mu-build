/* SPDX-License-Identifier: MIT */
#include "hid_keyboard.h"
#include "fixtures.h"
#include "observed_descriptor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static hk_descriptor d;
static hk_state s;
static hk_result result;
static void put(uint8_t *p, unsigned off, unsigned bits, unsigned v) {
 for (unsigned i=0;i<bits;++i) {
  unsigned mask=1u<<((off+i)%8);
  p[(off+i)/8]=(uint8_t)((p[(off+i)/8]&~mask)|(((v>>i)&1u)?mask:0));
 }
}
static void event(unsigned u, unsigned pressed) {
 unsigned found=0;
 for(size_t i=0;i<result.count;++i)
  found += result.events[i].usage==u && result.events[i].pressed==pressed;
 assert(found==1);
}
static void unchanged(const uint8_t *p,size_t n,hk_status expected) {
 hk_state old=s;
 assert(hk_decode(&d,&s,p,n,&result)==expected);
 assert(!memcmp(&old,&s,sizeof s)); assert(!result.count && !result.modifiers);
}
static void reject(const uint8_t *p,size_t n) {
 assert(hk_parse(p,n,&d)!=HK_OK); assert(!d.valid && !d.report_count);
}
static void boot_test(void) {
 uint8_t p[9]={2,0xff,4,4,5,0,0,0,0};
 assert(hk_parse(boot,sizeof boot,&d)==HK_OK); hk_state_init(&s);
 assert(d.reports[0].bits==64);
 assert(hk_decode(&d,&s,p,8,&result)==HK_OK);
 assert(result.count==3 && result.modifiers==2); event(4,1); event(5,1); event(0xe1,1);
 assert(hk_decode(&d,&s,p,8,&result)==HK_OK && !result.count);
 for(unsigned i=1;i<=3;++i) { p[2]=(uint8_t)i; unchanged(p,8,HK_ROLLOVER); }
 p[2]=0xff; unchanged(p,8,HK_INVALID);
 for(size_t i=0;i<8;++i) unchanged(p,i,HK_TRUNCATED);
 unchanged(p,9,HK_INVALID);
 memset(p,0,sizeof p);
 assert(hk_decode(&d,&s,p,8,&result)==HK_OK && result.count==3);
 event(4,0); event(5,0); event(0xe1,0);
}
static void nonboot_test(void) {
 uint8_t p[5]={5,0,0,0,0}, q[2]={9,1};
 assert(hk_parse(nonboot,sizeof nonboot,&d)==HK_OK); hk_state_init(&s);
 assert(d.report_count==2 && d.reports[0].bits==29);
 put(p+1,0,3,7); put(p+1,3,9,11); put(p+1,12,9,14); put(p+1,21,8,1);
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK);
 assert(result.count==3 && result.modifiers==0x81); event(4,1); event(0xe7,1); event(0xe0,1);
 assert(hk_decode(&d,&s,q,2,&result)==HK_OK && !result.count && result.modifiers==0x81);
 put(p+1,3,9,10); put(p+1,12,9,10); put(p+1,21,8,0);
 assert(hk_decode(&d,&s,p,5,&result)==HK_OK && result.count==2); event(0xe7,0); event(0xe0,0);
 q[1]=0; assert(hk_decode(&d,&s,q,2,&result)==HK_OK && result.count==1); event(4,0);
 put(p+1,3,9,15); unchanged(p,5,HK_ROLLOVER);
 put(p+1,3,9,9); unchanged(p,5,HK_INVALID);
 for(size_t i=0;i<5;++i) unchanged(p,i,HK_TRUNCATED);
 p[0]=42; unchanged(p,5,HK_UNKNOWN_REPORT);
}
static void nkro_test(void) {
 uint8_t p[30]; memset(p,0xff,sizeof p); p[0]=1;
 assert(hk_parse(nkro,sizeof nkro,&d)==HK_OK); hk_state_init(&s);
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK);
 assert(result.count==228 && result.modifiers==255);
 for(unsigned u=4;u<=231;++u) event(u,1);
 memset(p+1,0,sizeof p-1);
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK && result.count==228);
 for(unsigned u=4;u<=231;++u) event(u,0);
}
static void boundary_test(void) {
 /* 16-bit selector ending at exactly the last bit of a 4096-bit report. */
 const uint8_t desc[]={5,1,9,6,0xa1,1,5,7,
  0x75,16,0x96,255,0,0x81,1,
  0x15,0,0x25,1,9,0,9,4,0x95,1,0x81,0,0xc0};
 uint8_t report[513]={0};
 assert(hk_parse(desc,sizeof desc,&d)==HK_OK); hk_state_init(&s);
 assert(d.reports[0].bits==4096);
 report[510]=1;
 assert(hk_decode(&d,&s,report,512,&result)==HK_OK && result.count==1); event(4,1);
 unchanged(report,511,HK_TRUNCATED); unchanged(report,513,HK_INVALID);
 report[511]=1; unchanged(report,512,HK_INVALID);
 report[510]=0; report[511]=0;
 assert(hk_decode(&d,&s,report,512,&result)==HK_OK && result.count==1); event(4,0);
 /* Revisit an ID after another ID; offsets must append, not reset. */
 const uint8_t revisit[]={5,1,9,6,0xa1,1,5,7,0x15,0,0x25,1,0x75,1,0x95,1,
  0x85,1,9,4,0x81,2,0x85,2,9,5,0x81,2,0x85,1,9,6,0x81,2,0xc0};
 uint8_t p[]={1,2};
 assert(hk_parse(revisit,sizeof revisit,&d)==HK_OK); hk_state_init(&s);
 assert(d.reports[0].bits==2);
 assert(hk_decode(&d,&s,p,2,&result)==HK_OK && result.count==1); event(6,1);
}
static void rejection_test(void) {
 uint8_t b[HK_MAX_DESCRIPTOR+1];
 /* Output/Feature injections below lack local usages: malformed, not forbidden tags. */
 const uint8_t baditems[]={0xfe,0xa4,0xb4,0x91,0xb1,0x65,0x55,0x35,0x45,0x89,0x39,0x49,0x59};
 for(size_t i=0;i<sizeof boot;++i) reject(boot,i);
 for(size_t i=0;i<sizeof nonboot;++i) reject(nonboot,i);
 for(size_t i=0;i<sizeof nkro;++i) reject(nkro,i);
 for(size_t i=0;i<sizeof baditems;++i) {
  memcpy(b,boot,sizeof boot-1); b[sizeof boot-1]=baditems[i];
  b[sizeof boot]=0; b[sizeof boot+1]=0xc0; reject(b,sizeof boot+2);
 }
 memcpy(b,boot,sizeof boot); b[7]=8; reject(b,sizeof boot); /* no keyboard fields remain, not a blanket other-page ban */
 memcpy(b,boot,sizeof boot); b[17]=17; reject(b,sizeof boot); /* size */
 memcpy(b,boot,sizeof boot); b[21]=6; reject(b,sizeof boot); /* relative */
 memcpy(b,boot,sizeof boot); b[sizeof boot]=0; reject(b,sizeof boot+1);
 memset(b,0,sizeof b); reject(b,sizeof b);
 /* nine report IDs, each a one-bit key */
 const uint8_t head[]={5,1,9,6,0xa1,1,5,7,0x15,0,0x25,1,0x75,1,0x95,1};
 size_t n=sizeof head; memcpy(b,head,n);
 for(unsigned i=1;i<=9;++i) { b[n++]=0x85;b[n++]=(uint8_t)i;b[n++]=9;b[n++]=4;b[n++]=0x81;b[n++]=2; }
 b[n++]=0xc0; reject(b,n);
 /* 65 fields and report bit limit */
 n=sizeof head; memcpy(b,head,n);
 for(unsigned i=0;i<65;++i) { b[n++]=9;b[n++]=4;b[n++]=0x81;b[n++]=2; }
 b[n++]=0xc0; reject(b,n);
 n=sizeof head; memcpy(b,head,n);
 const uint8_t huge[]={0x75,16,0x96,1,1,0x81,1,0xc0};
 memcpy(b+n,huge,sizeof huge); reject(b,n+sizeof huge);
 /* Cannot introduce numbered report after an unnumbered input. */
 memcpy(b,boot,sizeof boot-1); n=sizeof boot-1;b[n++]=0x85;b[n++]=1;b[n++]=0xc0;reject(b,n);
 /* Usage page change with pending local usages must never reinterpret them. */
 const uint8_t pagechange[]={5,1,9,6,0xa1,1,9,4,5,7,0xc0}; reject(pagechange,sizeof pagechange);
}
static const hk_report *report_id(unsigned id) {
 for(unsigned i=0;i<d.report_count;++i) if(d.reports[i].id==id) return &d.reports[i];
 assert(0); return NULL;
}
static void observed_test(void) {
 /* Descriptor is observed; every report below is constructed, not captured. */
 uint8_t p[11]={1,2,0xff,4,4,0,0,0,0,0xff,0};
 uint8_t consumer[3]={0x52,0xff,0}, vendor[66], feature[]={9,0,0,0};
 assert(sizeof observed_descriptor==182);
 assert(hk_parse(observed_descriptor,sizeof observed_descriptor,&d)==HK_OK);
 assert(d.numbered && d.report_count==4); hk_state_init(&s);
 const hk_report *r=report_id(1);
 assert(r->bits==72 && r->output_bits==8 && !r->feature_bits && r->field_count==2);
 assert(r->fields[0].offset==0 && r->fields[0].count==8);
 /* LED Output between modifiers and array must not move the Input offset. */
 assert(r->fields[1].offset==16 && r->fields[1].count==6 && r->fields[1].usage_count==256);
 r=report_id(0x52); assert(r->bits==8 && !r->field_count && !r->output_bits && !r->feature_bits);
 r=report_id(0x3f); assert(r->bits==512 && !r->field_count && !r->output_bits && !r->feature_bits);
 r=report_id(9); assert(!r->bits && r->feature_bits==24 && !r->output_bits && !r->field_count);
 assert(hk_decode(&d,&s,p,10,&result)==HK_OK && result.count==2 && result.modifiers==2);
 event(4,1); event(0xe1,1);
 assert(hk_decode(&d,&s,p,10,&result)==HK_OK && !result.count && result.modifiers==2);
 hk_state held=s;
 memset(vendor,0xff,sizeof vendor); vendor[0]=0x3f;
 assert(hk_decode(&d,&s,consumer,2,&result)==HK_OK && !result.count && result.modifiers==2);
 assert(!memcmp(&held,&s,sizeof s));
 assert(hk_decode(&d,&s,vendor,65,&result)==HK_OK && !result.count && result.modifiers==2);
 assert(!memcmp(&held,&s,sizeof s));
 consumer[1]=0; memset(vendor+1,0,sizeof vendor-1);
 assert(hk_decode(&d,&s,consumer,2,&result)==HK_OK && !result.count && result.modifiers==2);
 assert(hk_decode(&d,&s,vendor,65,&result)==HK_OK && !result.count && result.modifiers==2);
 assert(!memcmp(&held,&s,sizeof s));
 for(size_t n=0;n<10;++n) unchanged(p,n,HK_TRUNCATED);
 unchanged(p,11,HK_INVALID);
 for(size_t n=0;n<2;++n) unchanged(consumer,n,HK_TRUNCATED);
 unchanged(consumer,3,HK_INVALID);
 for(size_t n=0;n<65;++n) unchanged(vendor,n,HK_TRUNCATED);
 unchanged(vendor,66,HK_INVALID);
 for(size_t n=1;n<=sizeof feature;++n) unchanged(feature,n,HK_UNKNOWN_REPORT);
 feature[0]=0x42; unchanged(feature,sizeof feature,HK_UNKNOWN_REPORT);
 /* Error in any of six slots preserves keys AND previous modifiers. */
 for(unsigned slot=3;slot<9;++slot) for(unsigned err=1;err<=3;++err) {
  uint8_t bad[10]={1,0,0,0,0,0,0,0,0,0}; bad[slot]=(uint8_t)err;
  unchanged(bad,sizeof bad,HK_ROLLOVER);
 }
 /* Full observed 00..FF selector table: E8..FF are ignored, not bitmap indexes. */
 for(unsigned u=0xe8;u<=0xff;++u) {
  p[4]=(uint8_t)u;
  assert(hk_decode(&d,&s,p,10,&result)==HK_OK && !result.count && result.modifiers==2);
 }
 memset(p+1,0,9);
 assert(hk_decode(&d,&s,p,10,&result)==HK_OK && result.count==2 && !result.modifiers);
 event(4,0); event(0xe1,0);
}
static void ignored_fields_test(void) {
 /* Legal LED/consumer/vendor fields consume length, never keyboard events.
  * Output and Feature interleave Input on the same ID with independent offsets. */
 const uint8_t desc[]={5,1,9,6,0xa1,1,0x85,1,0x15,0,0x25,1,0x75,1,0x95,1,
  5,7,9,4,0x81,2,
  5,8,9,1,0x91,2,9,2,0xb1,2,
  5,0x0c,9,0xcd,0x81,2,
  6,0,0xff,9,6,0x75,7,0x81,2,
  5,7,9,5,0x75,1,0x81,2,0xc0};
 uint8_t p[]={1,0xfe,1};
 assert(hk_parse(desc,sizeof desc,&d)==HK_OK); hk_state_init(&s);
 const hk_report *r=report_id(1);
 assert(r->bits==10 && r->output_bits==1 && r->feature_bits==1 && r->field_count==2);
 assert(r->fields[0].offset==0 && r->fields[1].offset==9);
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK && !result.count);
 p[1]|=1; p[2]|=2;
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK && result.count==2);
 event(4,1); event(5,1);
 p[1]=1; p[2]=2;
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK && !result.count);
 p[1]=0; p[2]=0;
 assert(hk_decode(&d,&s,p,sizeof p,&result)==HK_OK && result.count==2);
 event(4,0); event(5,0);
}
int main(void) {
 boot_test(); nonboot_test(); nkro_test(); boundary_test(); rejection_test(); observed_test(); ignored_fields_test();
 puts("PASS: boot/nonboot/NKRO + observed descriptor/synthetic reports, ignored streams, transactional errors, limits");
 return 0;
}
