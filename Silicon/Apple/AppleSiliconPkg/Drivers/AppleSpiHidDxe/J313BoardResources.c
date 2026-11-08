/* SPDX-License-Identifier: MIT
 * Bounded reader adapted from Experimental/J313RawAdt/raw_adt.[ch] (MIT).
 * PMGR layout/address and ID selection follow m1n1 src/pmgr.c (MIT).
 * This is production ADT discovery, not host BI_CONFIRMED evidence.
 */
#include "J313BoardResources.h"
#ifdef J313_HOST_TEST
#include <stdlib.h>
#define BoardAllocate(n) calloc(1,(n))
#define BoardFree(p) free(p)
#else
#define BoardAllocate(n) AllocateZeroPool(n)
#define BoardFree(p) FreePool(p)
#endif
#ifndef J313_HOST_TEST
#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/HobLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Guid/AppleAdtInfoHob.h>
#define strcmp AsciiStrCmp
#define strlen AsciiStrLen
static const char *strchr(const char *s, int c) {
  while (*s && *s != c) ++s;
  return *s == c ? s : NULL;
}
#endif
/* SPDX-License-Identifier: MIT
 * Production adaptation of the bounded raw ADT reader (MIT).
 */
#ifndef J313_RAW_ADT_H
#define J313_RAW_ADT_H
#include "Core/J313CoreCompat.h"
#define RADT_DEPTH 64u
#define RADT_BYTES (16u * 1024u * 1024u)
typedef enum { RADT_OK, RADT_ARGUMENT, RADT_FORMAT, RADT_TRUNCATED,
    RADT_BUDGET, RADT_UNSUPPORTED, RADT_DUPLICATE, RADT_NOT_FOUND,
    RADT_DENIED, RADT_INCOMPATIBLE } radt_status;
typedef struct { const unsigned char *data; size_t size; } radt_view;
typedef struct { size_t parent, first, count, name; } radt_node;
typedef struct { size_t key, data, size; uint32_t flags; } radt_property;
typedef struct {
    J313_ADT_DIAGNOSTIC *diagnostic;
    radt_node *nodes;
    radt_property *props;
    size_t node_count, prop_count, node_capacity, prop_capacity;
} radt_workspace;
/* b must be an immutable mapped ordinary-memory span of exactly size bytes.
 * w and out must not overlap b or each other. No allocation or MMIO.
 * Workspace contents are scratch, invalid on error, not an export interface.
 */
radt_status radt_validate(const void *b, size_t size, radt_workspace *w);
/* Exact absolute paths only; explicitly allowlisted metadata only.
 * Revalidates entire blob on each call; zeroes out on every failure.
 * Returned bytes borrow b; no address translation or numeric reinterpretation.
 */
radt_status radt_lookup(const void *b, size_t size, radt_workspace *w,
                        const char *path, const char *key, radt_view *out);
#endif
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int eq(const unsigned char *b, size_t off, const char *s)
{
    return strcmp((const char *)b + off, s) == 0;
}
/* Require exactly one terminating NUL, printable nonempty path component. */
static int node_name(const unsigned char *p, size_t n)
{
    size_t i;
    if (n < 2 || n > 256 || p[n - 1] != 0) return 0;
    for (i = 0; i + 1 < n; ++i)
        if (p[i] < 33 || p[i] > 126 || p[i] == '/') return 0;
    if ((n == 2 && p[0] == '.') ||
        (n == 3 && p[0] == '.' && p[1] == '.')) return 0;
    return 1;
}
static int strings(const unsigned char *p, size_t n)
{
    size_t i, start = 0;
    if (!n || p[n - 1]) return 0;
    for (i = 0; i < n; ++i) {
        if (!p[i]) { if (i == start) return 0; start = i + 1; }
        else if (p[i] < 32 || p[i] > 126) return 0;
    }
    return 1;
}
/* Only called with a checked span. Never scan a key outside the input. */
static radt_status fail(radt_workspace *w, radt_status status, uint32_t reason,
    size_t off, size_t node, const unsigned char *b, size_t size, size_t key)
{
    size_t i;
    J313_ADT_DIAGNOSTIC *d = w ? w->diagnostic : NULL;
    if (!d) return status;
    memset(d, 0, sizeof(*d));
    d->Reason = reason; d->Offset = off; d->NodeIndex = node;
    d->Size = size;
    if (b && key < size) {
        for (i = 0; i < 32 && i < size - key && b[key+i]; ++i)
            d->Property[i] = b[key+i] >= 33 && b[key+i] <= 126 ? (char)b[key+i] : '?';
    }
    return status;
}
static radt_status validate_root(const void *buffer, size_t size, radt_workspace *w, int ignore_tail)
{
    const unsigned char *b = buffer;
    struct { size_t index; uint32_t left; } stack[RADT_DEPTH];
    size_t depth = 0, off = 0;
    radt_status result = RADT_OK;
#define BAD(status, reason, at, node, key) fail(w, status, reason, at, node, b, size, key)
    if (!w) return RADT_ARGUMENT;
    w->node_count = w->prop_count = 0;
    if (!b) return BAD(RADT_ARGUMENT, J313_ADT_ARGUMENT, 0, SIZE_MAX, SIZE_MAX);
    if (size > RADT_BYTES) return BAD(RADT_BUDGET, J313_ADT_SIZE_BUDGET, 0, SIZE_MAX, SIZE_MAX);
    if (size >= 4 && b[0] == 0xd0 && b[1] == 0x0d &&
        b[2] == 0xfe && b[3] == 0xed) return BAD(RADT_UNSUPPORTED, J313_ADT_FDT, 0, SIZE_MAX, SIZE_MAX);
    do {
        uint32_t np, nc;
        size_t idx, parent, i, j;
        radt_node *node;
        if (depth == RADT_DEPTH)
            return BAD(RADT_BUDGET, J313_ADT_DEPTH_BUDGET, off, w->node_count, SIZE_MAX);
        if (w->node_count == w->node_capacity)
            return BAD(RADT_BUDGET, J313_ADT_NODE_BUDGET, off, w->node_count, SIZE_MAX);
        if (size - off < 8) return BAD(RADT_TRUNCATED, J313_ADT_NODE_HEADER, off, w->node_count, SIZE_MAX);
        np = le32(b + off); nc = le32(b + off + 4); off += 8;
        if (np > w->prop_capacity - w->prop_count ||
            nc > w->node_capacity - w->node_count - 1) return BAD(RADT_BUDGET, J313_ADT_COUNT_BUDGET, off-8, w->node_count, SIZE_MAX);
        idx = w->node_count++;
        parent = depth ? stack[depth - 1].index : SIZE_MAX;
        node = &w->nodes[idx];
        *node = (radt_node){parent, w->prop_count, np, SIZE_MAX};
        for (i = 0; i < np; ++i) {
            size_t k, len, pad, keyoff = off;
            uint32_t flags;
            radt_property *p;
            if (size - off < 36) return BAD(RADT_TRUNCATED, J313_ADT_PROPERTY_HEADER, off, idx, SIZE_MAX);
            for (k = 0; k < 32 && b[off + k]; ++k)
                if (b[off + k] < 33 || b[off + k] > 126)
                    return BAD(RADT_FORMAT, J313_ADT_PROPERTY_KEY, off+k, idx, keyoff);
            if (!k || k == 32) return BAD(RADT_FORMAT, J313_ADT_PROPERTY_KEY, off, idx, keyoff);
            /* Trailing key bytes are not semantically significant. */
            for (j = node->first; j < w->prop_count; ++j)
                if (eq(b, w->props[j].key, (const char *)b + off))
                    return BAD(RADT_DUPLICATE, J313_ADT_DUPLICATE_PROPERTY, off, idx, keyoff);
            len = le32(b + off + 32);
            /* m1n1 ADTPropertyStruct: bit31 is template, lower31 length.
             * Unrelated template values remain opaque, never interpreted. */
            flags = (uint32_t)len & UINT32_C(0x80000000);
            len &= UINT32_C(0x7fffffff);
            off += 36;
            if (len > size - off) return BAD(RADT_TRUNCATED, J313_ADT_VALUE_LENGTH, off, idx, keyoff);
            pad = (4 - (len & 3)) & 3;
            if (pad > size - off - len) return BAD(RADT_TRUNCATED, J313_ADT_VALUE_PADDING, off+len, idx, keyoff);
            p = &w->props[w->prop_count++];
            *p = (radt_property){keyoff, off, len, flags};
            if (eq(b, keyoff, "name")) {
                if (flags) return BAD(RADT_UNSUPPORTED, J313_ADT_NAME_TEMPLATE, keyoff+32, idx, keyoff);
                if (!(idx == 0 && len == 2 && b[off] == '/' && b[off+1] == 0) &&
                    !node_name(b + off, len)) return BAD(RADT_FORMAT, J313_ADT_NAME_VALUE, off, idx, keyoff);
                node->name = off;
            }
            /* Values are opaque until consumed. m1n1 ADTStringList permits
             * empty strings/lists; do not enforce our identity policy globally. */
            off += len + pad;
        }
        if (node->name == SIZE_MAX) return BAD(RADT_FORMAT, J313_ADT_NAME_MISSING, off, idx, SIZE_MAX);
        for (j = 1; j < idx; ++j)
            if (w->nodes[j].parent == parent &&
                eq(b, w->nodes[j].name, (const char *)b + node->name))
                return BAD(RADT_DUPLICATE, J313_ADT_DUPLICATE_NODE, node->name, idx, SIZE_MAX);
        if (depth) --stack[depth - 1].left;
        stack[depth].index = idx; stack[depth].left = nc; ++depth;
        while (depth && !stack[depth - 1].left) --depth;
    } while (depth);
    /* PrePi AdtParser.c publishes bootargs devtree_size, discarding dt_check
     * consumed length. This is a copied container span, not proof of an exact
     * tree length. m1n1 ADTNodeStruct is count-delimited with per-property
     * 4-byte alignment; it specifies no whole-container tail padding rule.
     * Strict callers reject any tail. The explicit experimental container
     * caller ignores it only after this entire counted root validates. Never
     * parse a second root or expand any mapping; reserve the entire HOB copy.
     * Record failure before filling span: fail() clears the diagnostic. */
    if (off != size && !ignore_tail)
        result = BAD(RADT_FORMAT, J313_ADT_TRAILING_BYTES, off, SIZE_MAX, SIZE_MAX);
    if (w->diagnostic) {
        size_t i;
        J313_ADT_DIAGNOSTIC *d = w->diagnostic;
        d->Size = size; d->Offset = off; d->Consumed = off;
        d->TailSize = size - off; d->TreeComplete = 1; d->TailAllZero = 1;
        /* Only aggregate ordinary copied memory within the checked span.
         * No bytes, names, addresses or property values are exported. */
        for (i = off; i < size; ++i)
            if (b[i]) d->TailAllZero = 0;
    }
#undef BAD
    return result;
}
radt_status radt_validate(const void *buffer, size_t size, radt_workspace *w)
{
    return validate_root(buffer, size, w, 0);
}
static const radt_property *prop(const unsigned char *b,
    const radt_workspace *w, size_t node, const char *key)
{
    size_t i;
    for (i = w->nodes[node].first;
         i < w->nodes[node].first + w->nodes[node].count; ++i)
        if (eq(b, w->props[i].key, key)) return w->props[i].flags ? NULL : &w->props[i];
    return NULL;
}
static size_t exact_path(const unsigned char *b, const radt_workspace *w,
                         const char *path)
{
    size_t node = 0;
    const char *s = path + 1;
    while (*s) {
        const char *end = strchr(s, '/');
        size_t len = end ? (size_t)(end - s) : strlen(s), i;
        if (!len) return SIZE_MAX;
        for (i = 1; i < w->node_count; ++i)
            if (w->nodes[i].parent == node &&
                strlen((const char *)b + w->nodes[i].name) == len &&
                !memcmp(b + w->nodes[i].name, s, len)) break;
        if (i == w->node_count) return SIZE_MAX;
        node = i;
        if (!end) return node;
        s = end + 1;
        if (!*s) return SIZE_MAX;
    }
    return node;
}
static int compatible(const unsigned char *b, const radt_workspace *w,
                       size_t node, const char *wanted)
{
    const radt_property *p = prop(b, w, node, "compatible");
    size_t i = 0;
    if (!p || !strings(b + p->data, p->size)) return 0;
    while (i < p->size) {
        if (eq(b, p->data + i, wanted)) return 1;
        i += strlen((const char *)b + p->data + i) + 1;
    }
    return 0;
}

static uint64_t le64(const unsigned char *p) { return le32(p) | ((uint64_t)le32(p+4)<<32); }
static const radt_property *get(const unsigned char *b, const radt_workspace *w,
 const char *path, const char *key) {
 size_t n=exact_path(b,w,path); return n==SIZE_MAX ? NULL : prop(b,w,n,key);
}
static int reg_at(const unsigned char *b,const radt_workspace *w,
 const char *path,size_t index,uint64_t *base,uint64_t *size) {
 const radt_property *r=get(b,w,path,"reg"), *ranges=get(b,w,"/arm-io","ranges");
 uint64_t addr,len; size_t i; unsigned found=0;
 if(!r || r->size%16 || index>=r->size/16 || !ranges || !ranges->size || ranges->size%24) return 0;
 addr=le64(b+r->data+index*16); len=le64(b+r->data+index*16+8);
 if(!len || addr>UINT64_MAX-len) return 0;
 for(i=0;i<ranges->size;i+=24) {
  uint64_t child=le64(b+ranges->data+i), cpu=le64(b+ranges->data+i+8), span=le64(b+ranges->data+i+16);
  if(!span || child>UINT64_MAX-span || cpu>UINT64_MAX-span) return 0;
  if(addr>=child && len<=span && addr-child<=span-len) {
   *base=cpu+(addr-child); *size=len; ++found;
  }
 }
 return found==1;
}
struct power_walk {
 const unsigned char *b; const radt_workspace *w;
 const radt_property *dev,*ps;
 uint16_t active[16],done[16]; unsigned depth,done_count; int u8;
 J313_SPI_RESOURCES *out;
};
static uint16_t dev_id(struct power_walk *p,const unsigned char *d) {
 return p->u8?d[3]:(uint16_t)(d[26]|((uint16_t)d[27]<<8));
}
static int power(struct power_walk *p,uint16_t id) {
 const unsigned char *d=NULL; size_t i; unsigned matches=0; uint64_t base,size,off,addr;
 uint32_t reg,ps_off; uint16_t parent[2];
 if(!id) return 1;
 for(i=0;i<p->depth;i++) if(p->active[i]==id) return 0;
 for(i=0;i<p->done_count;i++) if(p->done[i]==id) return 1;
 if(p->depth==16 || p->done_count==16) return 0;
 for(i=0;i<p->dev->size;i+=48) if(dev_id(p,p->b+p->dev->data+i)==id) { d=p->b+p->dev->data+i; matches++; }
 if(matches!=1) return 0;
 p->active[p->depth++]=id;
 parent[0]=p->u8?d[4]:(uint16_t)(d[4]|((uint16_t)d[5]<<8));
 parent[1]=p->u8?d[5]:(uint16_t)(d[6]|((uint16_t)d[7]<<8));
 if(!power(p,parent[0]) || !power(p,parent[1])) return 0;
 --p->depth;
 if(!(d[0]&0x10)) {
  if(d[11]>=p->ps->size/12) return 0;
  reg=le32(p->b+p->ps->data+d[11]*12); ps_off=le32(p->b+p->ps->data+d[11]*12+4);
  if(!reg_at(p->b,p->w,"/arm-io/pmgr",reg,&base,&size)) return 0;
  off=(uint64_t)ps_off+((uint64_t)d[10]<<3);
  if(size<4 || off>size-4 || (off&3)) return 0;
  addr=base+off;
  /* Physical J313 PMGR main/mini apertures, not a gate-ID-derived address. */
  if(!((addr>=UINT64_C(0x23b700000) && addr<UINT64_C(0x23b800000)) ||
       (addr>=UINT64_C(0x23d280000) && addr<UINT64_C(0x23d284000)))) return 0;
  if(addr<p->out->PmgrBase || addr-p->out->PmgrBase>UINT32_MAX) return 0;
  for(i=0;i<p->out->PowerCount;i++) if(p->out->PowerOffset[i]==addr-p->out->PmgrBase) return 0;
  if(p->out->PowerCount==J313_SPI_POWER_MAX) return 0;
  p->out->PowerOffset[p->out->PowerCount++]=(uint32_t)(addr-p->out->PmgrBase);
  if(addr-p->out->PmgrBase+4>p->out->PmgrSize) p->out->PmgrSize=addr-p->out->PmgrBase+4;
 }
 if(p->done_count==16) return 0;
 p->done[p->done_count++]=id;
 return 1;
}
static int gates(struct power_walk *w,const char *path) {
 const radt_property *p=get(w->b,w->w,path,"clock-gates"); size_t i;
 if(!p || !p->size || p->size%4 || p->size>64) return 0;
 for(i=0;i<p->size;i+=4) {
  uint32_t id=le32(w->b+p->data+i);
  if(!id || id>0xffff || !power(w,(uint16_t)id)) return 0;
 }
 return 1;
}
static int ParseResources(const void *blob,size_t size,J313_SPI_RESOURCES *out, radt_workspace workspace,
 int ignore_tail, int bypass_identity, uint32_t *identity_mask) {
 const unsigned char *b=blob; const radt_property *p; J313_SPI_RESOURCES r;
 struct power_walk walk; size_t n; uint32_t mask=0;
 if(!out) return 1;
 memset(out,0,sizeof(*out)); memset(&r,0,sizeof(r));
 if(validate_root(blob,size,&workspace,ignore_tail)!=RADT_OK) return 1;
 p=get(b,&workspace,"/","target-type");
 if(!p || p->size!=5 || memcmp(b+p->data,"j313",5)) mask|=J313_IDENTITY_TARGET_TYPE;
 p=get(b,&workspace,"/chosen","chip-id");
 if(!p || p->size!=4 || le32(b+p->data)!=0x8103) mask|=J313_IDENTITY_CHIP_ID;
 n=exact_path(b,&workspace,"/arm-io");
 if(n==SIZE_MAX || !compatible(b,&workspace,n,"arm-io,t8103")) mask|=J313_IDENTITY_ARM_IO;
 n=exact_path(b,&workspace,"/arm-io/spi3");
 if(n==SIZE_MAX || !compatible(b,&workspace,n,"spi-1,spimc")) mask|=J313_IDENTITY_SPI;
 n=exact_path(b,&workspace,"/arm-io/spi3/ipd");
 if(n==SIZE_MAX || !compatible(b,&workspace,n,"hid-transport,spi")) mask|=J313_IDENTITY_IPD;
 if(identity_mask) *identity_mask=mask;
 if(mask && !bypass_identity) return 2;
 if(!reg_at(b,&workspace,"/arm-io/spi3",0,&r.SpiBase,&r.SpiSize) ||
    !reg_at(b,&workspace,"/arm-io/gpio",0,&r.ApBase,&r.ApSize) ||
    !reg_at(b,&workspace,"/arm-io/nub-gpio",0,&r.NubBase,&r.NubSize) ||
    !reg_at(b,&workspace,"/arm-io/pmgr",0,&r.PmgrBase,&r.PmgrSize)) return 3;
 if(r.SpiBase!=UINT64_C(0x23510c000) || r.SpiSize!=0x4000 ||
    r.ApBase!=UINT64_C(0x23c100000) || r.ApSize<196*4 || r.ApSize>0x100000 ||
    r.NubBase!=UINT64_C(0x23d1f0000) || r.NubSize<14*4 || r.NubSize>0x10000 ||
    r.PmgrBase!=UINT64_C(0x23b700000) || r.PmgrSize>0x100000) return 3;
 p=get(b,&workspace,"/arm-io/spi3","clock-gates");
 if(!p || p->size!=4 || le32(b+p->data)!=0x34) return 4;
 memset(&walk,0,sizeof(walk)); walk.b=b; walk.w=&workspace; walk.out=&r;
 walk.dev=get(b,&workspace,"/arm-io/pmgr","devices"); walk.ps=get(b,&workspace,"/arm-io/pmgr","ps-regs");
 if(!walk.dev || walk.dev->size<96 || walk.dev->size%48 || !walk.ps || !walk.ps->size || walk.ps->size%12) return 4;
 walk.u8=b[walk.dev->data+3]!=b[walk.dev->data+48+3];
 if(!gates(&walk,"/arm-io/gpio") || !gates(&walk,"/arm-io/nub-gpio") || !power(&walk,0x34) || !r.PowerCount) return 4;
 /* In the experimental branch these constants are assumptions, NOT verified
  * identity, even when mask is zero. Resource/address checks above still apply. */
 memcpy(r.TargetType,"j313",5); r.ChipId=0x8103; r.ClockGate=0x34;
 *out=r; return 0;
}
static int ParseWithPolicy(const void *blob,size_t size,J313_SPI_RESOURCES *out,
 J313_ADT_DIAGNOSTIC *diagnostic, int ignore_tail, int bypass_identity, uint32_t *identity_mask) {
 radt_workspace w; int result;
 if(identity_mask) *identity_mask=0;
 memset(&w,0,sizeof(w)); w.diagnostic=diagnostic;
 if(diagnostic) { memset(diagnostic,0,sizeof(*diagnostic)); diagnostic->NodeIndex=SIZE_MAX; diagnostic->Size=size; }
 if(!out) { fail(&w,RADT_ARGUMENT,J313_ADT_ARGUMENT,0,SIZE_MAX,NULL,size,SIZE_MAX); return 1; }
 memset(out,0,sizeof(*out));
 if(!blob || size<8 || size>RADT_BYTES) {
  fail(&w,RADT_ARGUMENT,!blob?J313_ADT_ARGUMENT:(size<8?J313_ADT_NODE_HEADER:J313_ADT_SIZE_BUDGET),
       0,size<8 && blob?0:SIZE_MAX,NULL,size,SIZE_MAX); return 1;
 }
 /* Every named node needs >=48 bytes; every property >=36. Derive memory
  * budgets from the actual HOB length, not host-fixture population limits. */
 w.node_capacity=size/48+1; w.prop_capacity=size/36+1;
 w.nodes=BoardAllocate(w.node_capacity*sizeof(*w.nodes));
 w.props=BoardAllocate(w.prop_capacity*sizeof(*w.props));
 if(!w.nodes || !w.props) { fail(&w,RADT_BUDGET,J313_ADT_ALLOCATION,0,SIZE_MAX,NULL,size,SIZE_MAX); if(w.nodes) BoardFree(w.nodes); if(w.props) BoardFree(w.props); return 5; }
 result=ParseResources(blob,size,out,w,ignore_tail,bypass_identity,identity_mask);
 BoardFree(w.props); BoardFree(w.nodes);
 return result;
}
int J313BoardResourcesParseEx(const void *blob,size_t size,J313_SPI_RESOURCES *out,
 J313_ADT_DIAGNOSTIC *diagnostic) {
 return ParseWithPolicy(blob,size,out,diagnostic,0,0,NULL);
}
int J313BoardResourcesParseContainer(const void *blob,size_t size,J313_SPI_RESOURCES *out,
 J313_ADT_DIAGNOSTIC *diagnostic) {
 return ParseWithPolicy(blob,size,out,diagnostic,1,0,NULL);
}
int J313BoardResourcesParseExperimentalIdentityBypass(const void *blob,size_t size,
 J313_SPI_RESOURCES *out,J313_ADT_DIAGNOSTIC *diagnostic,uint32_t *identity_mask) {
 return ParseWithPolicy(blob,size,out,diagnostic,1,1,identity_mask);
}
int J313BoardResourcesParse(const void *blob,size_t size,J313_SPI_RESOURCES *out) {
 return J313BoardResourcesParseEx(blob,size,out,NULL);
}
#ifndef J313_HOST_TEST
EFI_STATUS J313BoardResourcesLoadEx(J313_SPI_RESOURCES *Resources, J313_BOOT_DIAGNOSTIC *Snapshot) {
 VOID *Hob; APPLE_ADT_INFO_HOB *Info; VOID *Copy; int Result;
 J313_ADT_DIAGNOSTIC Diagnostic; uint32_t IdentityMask;
 if(Snapshot) Snapshot->Valid &= ~(J313_DIAG_RESOURCE_VALID | J313_DIAG_IDENTITY_VALID);
 if(Resources==NULL) return EFI_INVALID_PARAMETER;
 ZeroMem(Resources,sizeof(*Resources));
 Hob=GetFirstGuidHob(&gAppleSiliconPkgAdtInfoHobGuid);
 if(Hob==NULL || GET_GUID_HOB_DATA_SIZE(Hob)!=sizeof(*Info)) return EFI_NOT_FOUND;
 Info=GET_GUID_HOB_DATA(Hob);
 if(Info->Version!=APPLE_ADT_INFO_VERSION || Info->Flags!=APPLE_ADT_INFO_RESERVED ||
    !Info->AdtSize || Info->AdtSize>RADT_BYTES || Info->AdtPagesBase>MAX_UINT64-Info->AdtPagesSize ||
    Info->AdtBase<Info->AdtPagesBase || Info->AdtSize>Info->AdtPagesSize ||
    Info->AdtBase-Info->AdtPagesBase>Info->AdtPagesSize-Info->AdtSize) return EFI_COMPROMISED_DATA;
 Copy=AllocateCopyPool((UINTN)Info->AdtSize,(VOID *)(UINTN)Info->AdtBase);
 if(Copy==NULL) return EFI_OUT_OF_RESOURCES;
 /* J313-only EXPERIMENTAL: assume identity, retain all resource checks and
  * the full reserved HOB span; ignore only the counted root tail. */
 Result=J313BoardResourcesParseExperimentalIdentityBypass(Copy,(UINTN)Info->AdtSize,Resources,&Diagnostic,&IdentityMask);
 if(Snapshot) {
  Snapshot->ResourceCode=(UINT32)Result; Snapshot->Valid |= J313_DIAG_RESOURCE_VALID;
  if(Diagnostic.TreeComplete) { Snapshot->IdentityMask=IdentityMask; Snapshot->Valid |= J313_DIAG_IDENTITY_VALID; }
 }
 DEBUG((DEBUG_ERROR,"J313 ADT EXPERIMENTAL IDENTITY-BYPASS mask=0x%x (assumed j313/t8103; not verified)\n",IdentityMask));
 FreePool(Copy);
 if(Diagnostic.TreeComplete) DEBUG((DEBUG_ERROR,"J313 ADT EXPERIMENTAL TAIL-IGNORED size=%Lu consumed=%Lu tail=%Lu zero=%u\n",
  (UINT64)Diagnostic.Size,(UINT64)Diagnostic.Consumed,(UINT64)Diagnostic.TailSize,Diagnostic.TailAllZero));
 DEBUG((DEBUG_ERROR,"J313 ADT span size=%Lu consumed=%Lu tail=%Lu tree_complete=%u tail_all_zero=%a\n",
  (UINT64)Diagnostic.Size,(UINT64)Diagnostic.Consumed,(UINT64)Diagnostic.TailSize,
  Diagnostic.TreeComplete,!Diagnostic.TreeComplete?"unknown":(Diagnostic.TailAllZero?"yes":"no")));
 if(Result) DEBUG((DEBUG_ERROR,"J313 ADT resource failure %d: 1=structure, 2=identity/compatible, 3=reg/ranges, 4=clock-gates/pmgr devices/ps-regs, 5=allocation\n",Result));
 if(Result==1) DEBUG((DEBUG_ERROR,"J313 ADT detail reason=%u offset=%Lu node=%Lu property=%a\n",
  Diagnostic.Reason,(UINT64)Diagnostic.Offset,(UINT64)Diagnostic.NodeIndex,Diagnostic.Property));
 return Result==0?EFI_SUCCESS:(Result==5?EFI_OUT_OF_RESOURCES:EFI_UNSUPPORTED);
}
EFI_STATUS J313BoardResourcesLoad(J313_SPI_RESOURCES *Resources) {
 return J313BoardResourcesLoadEx(Resources,NULL);
}
#endif
