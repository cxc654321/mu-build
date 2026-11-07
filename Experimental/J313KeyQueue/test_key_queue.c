/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Tests require assertions enabled
#endif
#include "key_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hk_result one(unsigned usage, unsigned pressed, unsigned mods)
{
    hk_result r = {0};
    r.count = 1;
    r.events[0].usage = (uint8_t)usage;
    r.events[0].pressed = (uint8_t)pressed;
    r.modifiers = (uint8_t)mods;
    return r;
}
static kq_key press(kq_queue *q, unsigned usage, unsigned mods)
{
    hk_result r = one(usage, 1, mods);
    kq_key k;
    assert(kq_feed(q, &r) == KQ_OK);
    assert(kq_pop(q, &k) == KQ_OK);
    return k;
}
static void mapping(void)
{
    const unsigned punct[] = {45,61,91,93,92,0,59,39,96,44,46,47};
    const unsigned upper[] = {95,43,123,125,124,0,58,34,126,60,62,63};
    const unsigned usages[] = {0x28,0x29,0x2a,0x2b,0x2c,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52};
    const unsigned scans[] = {0,23,0,0,0,7,5,9,8,6,10,3,4,2,1};
    const unsigned chars[] = {13,0,8,9,32,0,0,0,0,0,0,0,0,0,0};
    kq_queue q;
    unsigned caps, shift, i;
    for (caps = 0; caps < 2; ++caps) for (shift = 0; shift < 2; ++shift) {
        kq_init(&q);
        if (caps) { hk_result r = one(0x39,1,0); assert(kq_feed(&q,&r)==KQ_OK); }
        for (i=0; i<26; ++i) {
            kq_key k=press(&q,4+i,shift ? 2 : 0);
            assert(k.unicode_char == (caps != shift ? 'A' : 'a')+i && !k.scan_code);
            assert(k.toggle_state == (0x80u | (caps ? 4u : 0u)));
        }
        for (i=0; i<10; ++i) assert(press(&q,0x1e + i,shift ? 32 : 0).unicode_char ==
            (unsigned)(shift ? "!@#$%^&*()"[i] : "1234567890"[i]));
        for (i=0; i<12; ++i) if (punct[i])
            assert(press(&q,0x2d+i,shift ? 2 : 0).unicode_char == (shift ? upper[i] : punct[i]));
        for (i=0; i<sizeof(usages)/sizeof(usages[0]); ++i) {
            kq_key k=press(&q,usages[i],shift ? 2 : 0);
            assert(k.scan_code==scans[i] && k.unicode_char==chars[i]);
        }
        for (i=0; i<12; ++i) {
            kq_key k=press(&q,0x3a+i,0);
            assert(k.scan_code==11+i && !k.unicode_char);
        }
    }
    kq_init(&q);
    for (i=0; i<256; ++i) {
        const unsigned bits[]={8,2,32,128,4,1,16,64};
        uint32_t expected=KQ_SHIFT_VALID;
        unsigned j;
        kq_key k=press(&q,4,i);
        for(j=0;j<8;++j) if(i & (1u<<j)) expected |= bits[j];
        assert(k.shift_state==expected);
        assert(k.unicode_char==((i & 0x22) ? 'A' : 'a'));
    }
}
static void batches(void)
{
    kq_queue q, old;
    kq_key k={0}, sentinel={99,98,97,96};
    hk_result r={0};
    unsigned u;
    kq_init(&q);
    assert(kq_pop(&q,&sentinel)==KQ_EMPTY && sentinel.scan_code==99);
    for(u=0xe0;u<=0xe7;++u) { r=one(u,1,255); assert(kq_feed(&q,&r)==KQ_OK && !q.count); }
    r=one(4,0,0); assert(kq_feed(&q,&r)==KQ_OK && !q.count);
    r=one(0x32,1,0); assert(kq_feed(&q,&r)==KQ_OK && !q.count);
    r=one(0x65,1,0); assert(kq_feed(&q,&r)==KQ_OK && !q.count);
    r=one(4,1,0); r.count=2; r.events[1]=(hk_event){0x39,1};
    assert(kq_feed(&q,&r)==KQ_OK); assert(kq_pop(&q,&k)==KQ_OK && k.unicode_char=='A');
    r=one(0x39,0,0); assert(kq_feed(&q,&r)==KQ_OK && q.toggle_state==0x84);
    r=one(0x39,1,0); assert(kq_feed(&q,&r)==KQ_OK && q.toggle_state==0x80);
    r=one(0x47,1,0); assert(kq_feed(&q,&r)==KQ_OK);
    r=one(0x53,1,0); assert(kq_feed(&q,&r)==KQ_OK && q.toggle_state==0x83);
    old=q;
    r=one(4,1,255); r.count=HK_MAX_EVENTS+1;
    assert(kq_feed(&q,&r)==KQ_INVALID && !memcmp(&old,&q,sizeof q));
    r=one(4,2,255); assert(kq_feed(&q,&r)==KQ_INVALID);
    r=one(255,1,0); assert(kq_feed(&q,&r)==KQ_INVALID);
    r=one(1,1,0); assert(kq_feed(&q,&r)==KQ_INVALID);
    r=one(0x39,1,255); r.count=2; r.events[1]=r.events[0];
    assert(kq_feed(&q,&r)==KQ_INVALID && !memcmp(&old,&q,sizeof q));
    assert(kq_feed(NULL,&r)==KQ_INVALID && kq_feed(&q,NULL)==KQ_INVALID);
    assert(kq_pop(NULL,&k)==KQ_INVALID && kq_pop(&q,NULL)==KQ_INVALID);
    kq_init(NULL);
}
static void overflow(void)
{
    kq_queue q;
    kq_key k;
    hk_result r;
    unsigned round,i;
    kq_init(&q);
    for(round=0;round<100;++round) {
        for(i=0;i<KQ_CAPACITY;++i) { r=one(4+i%26,1,0); assert(kq_feed(&q,&r)==KQ_OK); }
        r=one(4,1,0); assert(kq_feed(&q,&r)==KQ_OVERFLOW);
        assert(q.count==KQ_CAPACITY && q.dropped==round+1);
        for(i=0;i<KQ_CAPACITY;++i) { assert(kq_pop(&q,&k)==KQ_OK); assert(k.unicode_char=='a'+i%26); }
    }
    r=(hk_result){0}; r.count=HK_MAX_USAGES-4;
    for(i=4;i<HK_MAX_USAGES;++i) r.events[i-4]=(hk_event){(uint8_t)i,1};
    assert(kq_feed(&q,&r)==KQ_OVERFLOW && q.count==KQ_CAPACITY);
    assert(q.toggle_state==0x87 && q.dropped>100);
    assert(kq_pop(&q,&k)==KQ_OK && k.unicode_char=='A');
    r=one(5,1,0); assert(kq_feed(&q,&r)==KQ_OK); /* wrap with one free slot */
    q.dropped=UINT64_MAX; /* white-box saturation boundary */
    assert(kq_feed(&q,&r)==KQ_OVERFLOW && q.dropped==UINT64_MAX);
    r=one(0x39,1,0); assert(kq_feed(&q,&r)==KQ_OK && q.toggle_state==0x83);
    kq_init(&q); assert(!q.count && !q.dropped && q.toggle_state==0x80);
}
static void decoder_integration(void)
{
    static const uint8_t descriptor[]={
        5,1,9,6,0xa1,1,5,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
        0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};
    static hk_descriptor d;
    hk_state s;
    hk_result r;
    kq_queue q;
    kq_key k;
    uint8_t report[8]={2,0,4,0x39,0,0,0,0};
    assert(hk_parse(descriptor,sizeof descriptor,&d)==HK_OK);
    hk_state_init(&s); kq_init(&q);
    assert(hk_decode(&d,&s,report,sizeof report,&r)==HK_OK);
    assert(kq_feed(&q,&r)==KQ_OK && q.count==1);
    assert(kq_pop(&q,&k)==KQ_OK && k.unicode_char=='a' && k.shift_state==0x80000002u);
    assert(hk_decode(&d,&s,report,sizeof report,&r)==HK_OK && !r.count);
    assert(kq_feed(&q,&r)==KQ_OK && !q.count && q.toggle_state==0x84);
    memset(report,0,sizeof report);
    assert(hk_decode(&d,&s,report,sizeof report,&r)==HK_OK);
    assert(kq_feed(&q,&r)==KQ_OK && !q.count && q.shift_state==KQ_SHIFT_VALID);
    report[2]=4;
    assert(hk_decode(&d,&s,report,sizeof report,&r)==HK_OK);
    assert(kq_feed(&q,&r)==KQ_OK);
    assert(kq_pop(&q,&k)==KQ_OK && k.unicode_char=='A');
}
int main(void)
{
    mapping(); batches(); overflow(); decoder_integration();
    puts("J313KeyQueue: mapping, modifiers, batches, bounds, FIFO and HID integration PASS");
    return 0;
}
