/* SPDX-License-Identifier: MIT */
#include "hid_keyboard.h"
#include "fixtures.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static hk_descriptor d;
static void exercise(const uint8_t *p,size_t n) {
 hk_state state, old;
 hk_result r;
 hk_state_init(&state);
 for(unsigned i=0;i<3;++i) {
  old=state;
  hk_status status=hk_decode(&d,&state,p,n,&r);
  if(status!=HK_OK) { assert(!memcmp(&old,&state,sizeof old)); assert(!r.count); }
  else {
   assert(r.count<=HK_MAX_EVENTS);
   if(i) assert(!r.count);
   for(size_t j=0;j<r.count;++j) {
    assert(r.events[j].usage>=4 && r.events[j].usage<HK_MAX_USAGES);
    assert(r.events[j].pressed<=1);
    if(j) assert(r.events[j-1].usage<r.events[j].usage);
   }
  }
 }
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t n) {
 if(hk_parse(data,n,&d)==HK_OK) exercise(data,n);
 if(n>=2) {
  size_t split=((size_t)data[0]|((size_t)data[1]<<8))%(n-1);
  if(hk_parse(data+2,split,&d)==HK_OK) exercise(data+2+split,n-2-split);
 }
 const uint8_t *f[]={boot,nonboot,nkro};
 const size_t z[]={sizeof boot,sizeof nonboot,sizeof nkro};
 for(unsigned i=0;i<3;++i) {
  assert(hk_parse(f[i],z[i],&d)==HK_OK); exercise(data,n);
  /* Force correctly framed payloads too, not just mostly length errors. */
  for(unsigned j=0;j<d.report_count;++j) {
   uint8_t report[HK_MAX_BITS/8+1]={0};
   size_t length=(d.reports[j].bits+7u)/8u+d.numbered;
   for(size_t k=0;k<length;++k) report[k]=n?data[k%n]:0;
   if(d.numbered) report[0]=d.reports[j].id;
   exercise(report,length);
   for(size_t k=0;k<length;++k) exercise(report,k);
  }
 }
 return 0;
}
#ifdef HK_FUZZ_SMOKE
static uint32_t rng=0x313;
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
int main(void) {
 uint8_t b[HK_MAX_DESCRIPTOR+1];
 const uint8_t *f[]={boot,nonboot,nkro};
 const size_t z[]={sizeof boot,sizeof nonboot,sizeof nkro};
 for(unsigned i=0;i<20000;++i) {
  size_t n=next()%sizeof b;
  if(i%2) {
   unsigned k=next()%3; n=z[k]; memcpy(b,f[k],n);
   for(unsigned j=0;j<1+i%8;++j) b[next()%n]=(uint8_t)next();
  } else for(size_t j=0;j<n;++j) b[j]=(uint8_t)next();
  LLVMFuzzerTestOneInput(b,n);
 }
 puts("PASS: 20000 deterministic descriptor/report fuzz cases"); return 0;
}
#endif
