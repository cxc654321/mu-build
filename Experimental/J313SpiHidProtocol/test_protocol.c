/* SPDX-License-Identifier: GPL-2.0-only */
#include "spihid_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void le(uint8_t *p, size_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static void crc(uint8_t *p, size_t n) { le(p+n, sh_crc16(p,n)); }
static void message(uint8_t *m, size_t n)
{
    size_t i;
    memset(m,0,n); m[0]=0x10; m[1]=2; m[3]=0xff;
    le(m+6,n-10);
    for(i=8;i<n-2;++i) m[i]=(uint8_t)i;
    crc(m,n-2);
}
static void packet(uint8_t *p, const uint8_t *m, size_t off, size_t len, size_t total)
{
    memset(p,0,256); p[0]=0x20; p[1]=2;
    le(p+2,off); le(p+4,total-off-len); le(p+6,len);
    memcpy(p+8,m+off,len); crc(p,254);
}
static void clean(const struct sh_rx *rx, const struct sh_message *out)
{
    size_t i;
    assert(rx->used==0 && rx->total==0 && rx->flags==0 && rx->device==0);
    assert(out->payload==NULL && out->length==0);
    for(i=0;i<SH_MESSAGE_MAX;++i) assert(rx->bytes[i]==0);
}
int main(void)
{
    struct sh_rx rx;
    struct sh_message out;
    uint8_t m[2048], p[256], q[256], tiny[1]={0}, payload[237]={2,1};
    size_t n,off,len;
    unsigned mode;
    sh_reset(&rx);
    assert(sh_crc16((const uint8_t *)"123456789",9)==0xbb3d);
    assert(sh_crc16(NULL,0)==0);
    puts("PASS CRC known vector");
    for(n=10;n<=2048;++n) {
        message(m,n);
        for(off=0;off<n;off+=len) {
            len=n-off; if(len>246) len=246;
            packet(p,m,off,len,n);
            assert(sh_feed(&rx,p,256,&out)==(off+len==n?SH_MESSAGE:SH_MORE));
        }
        assert(out.length==n-10 && out.id==255 && out.device==2 && out.flags==0x20);
        assert(memcmp(out.payload,m+8,n-10)==0);
    }
    puts("PASS normal single/multiple packets, every total 10..2048 including short tails");
    message(m,300); packet(p,m,0,246,300); packet(q,m,246,54,300);
    for(mode=0;mode<10;++mode) {
        assert(sh_feed(&rx,p,256,&out)==SH_MORE);
        packet(q,m,246,54,300);
        switch(mode) {
        case 0: q[254]^=1; break;
        case 1: le(q+2,245); crc(q,254); break;
        case 2: q[1]=1; crc(q,254); break;
        case 3: q[0]=0x40; crc(q,254); break;
        case 4: le(q+4,1); crc(q,254); break;
        case 5: le(q+6,247); crc(q,254); break;
        case 6: le(q+4,65535); crc(q,254); break;
        case 7: le(q+2,65535); crc(q,254); break;
        case 8: q[0]=0; crc(q,254); break;
        case 9: le(q+6,0); crc(q,254); break;
        }
        assert(sh_feed(&rx,q,256,&out)<0); clean(&rx,&out);
        packet(q,m,246,54,300);
        assert(sh_feed(&rx,q,256,&out)==SH_SEQUENCE); clean(&rx,&out);
    }
    puts("PASS badcrc/outoforder/device-flags-total mismatch/oversize/overflow/zero length/reset");
    assert(sh_feed(&rx,p,256,&out)==SH_MORE);
    assert(sh_feed(&rx,p,256,&out)==SH_SEQUENCE); clean(&rx,&out);
    for(n=0;n<256;++n) {
        assert(sh_feed(&rx,p,256,&out)==SH_MORE);
        assert(sh_feed(&rx,n==1?tiny:p,n,&out)==SH_PACKET_LENGTH); clean(&rx,&out);
    }
    assert(sh_feed(&rx,p,257,&out)==SH_PACKET_LENGTH);
    assert(sh_feed(&rx,NULL,256,&out)==SH_ARGUMENT); clean(&rx,&out);
    assert(sh_feed(NULL,p,256,&out)==SH_ARGUMENT);
    assert(sh_feed(&rx,p,256,NULL)==SH_ARGUMENT);
    puts("PASS duplicate first fragment/truncation 0..255/excess bytes/null arguments");
    message(m,20); m[8]^=1; packet(p,m,0,20,20);
    assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE_CRC); clean(&rx,&out);
    message(m,20); le(m+6,65535); crc(m,18); packet(p,m,0,20,20);
    assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE_LENGTH); clean(&rx,&out);
    message(m,20); le(m+6,9); crc(m,18); packet(p,m,0,20,20);
    assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE_LENGTH); clean(&rx,&out);
    packet(p,m,0,9,9);
    assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE_LENGTH); clean(&rx,&out);
    packet(p,m,0,1,2049);
    assert(sh_feed(&rx,p,256,&out)==SH_BOUNDS); clean(&rx,&out);
    puts("PASS inner badcrc/inner exact-length mismatch/short message/2049 rejected");
    for(n=0;n<=236;++n) {
        assert(sh_request(p,256,0xd0,0x20,2,1,7,512,payload,n)==SH_MESSAGE);
        assert(p[0]==0x40 && p[1]==0xd0 && p[2]==0 && p[4]==0);
        assert(p[8]==0x20 && p[9]==2 && p[10]==1 && p[11]==7);
        assert(p[12]==0 && p[13]==2);
        assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE);
        assert(out.length==n && out.response_length==512);
        assert(memcmp(out.payload,payload,n)==0);
        for(off=18+n;off<254;++off) assert(p[off]==0);
    }
    memcpy(q,p,256);
    assert(sh_request(p,256,1,0,0,0,0,0,payload,237)==SH_BOUNDS);
    assert(sh_request(p,256,1,0,0,0,0,0,payload,SIZE_MAX)==SH_BOUNDS);
    assert(sh_request(p,255,1,0,0,0,0,0,NULL,0)==SH_BOUNDS);
    assert(sh_request(p,256,1,0,0,0,0,0,NULL,1)==SH_ARGUMENT);
    assert(memcmp(p,q,256)==0);
    assert(sh_request(p,256,0xd0,0x20,1,0xd0,0,0,NULL,0)==SH_MESSAGE);
    assert(sh_feed(&rx,p,256,&out)==SH_MESSAGE && out.length==0);
    message(m,300); packet(p,m,0,246,300);
    assert(sh_feed(&rx,p,256,&out)==SH_MORE);
    sh_reset(&rx); memset(&out,0,sizeof(out)); clean(&rx,&out);
    puts("PASS request sizes 0..236/LE/padding/limits/no mutation on error/explicit cancellation");
    puts("ALL TESTS PASSED");
    return 0;
}
