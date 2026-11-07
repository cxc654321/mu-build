"""Execute unchanged production reservation bodies with real PI HOB layouts.
Only HOB services/PCDs are mocked; ASan/UBSan cover the host execution.
"""
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c'
PREFIX = r'''
#include <PiPei.h>
#include <Library/HobLib.h>
#include <Guid/AppleAdtInfoHob.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static union { UINT64 align; unsigned char bytes[4096]; } arena;
static UINTN used;
static BOOLEAN present;
static UINTN metadata_size;
static struct { EFI_HOB_GUID_TYPE header; APPLE_ADT_INFO_HOB info; } metadata;
EFI_GUID gAppleSiliconPkgAdtInfoHobGuid;
static UINT64 ram_base=0x100000, ram_size=0x100000;
#define PcdSystemMemoryBase ram_base
#define PcdSystemMemorySize ram_size
#define PcdGet64(x) (x)
VOID *EFIAPI GetHobList(VOID) { return arena.bytes; }
VOID *EFIAPI GetFirstGuidHob(CONST EFI_GUID *guid) {
 assert(guid==&gAppleSiliconPkgAdtInfoHobGuid);
 metadata.header.Header.HobLength=(UINT16)(sizeof(EFI_HOB_GUID_TYPE)+metadata_size);
 return present?&metadata:NULL;
}
static VOID *append(UINT16 type, UINTN size) {
 EFI_HOB_GENERIC_HEADER *h;
 assert(used+size+sizeof(*h)<=sizeof(arena.bytes));
 h=(VOID *)(arena.bytes+used); memset(h,0,size);h->HobType=type;h->HobLength=(UINT16)size;used+=size;
 EFI_HOB_GENERIC_HEADER *end=(VOID *)(arena.bytes+used);
 memset(end,0,sizeof(*end));end->HobType=EFI_HOB_TYPE_END_OF_HOB_LIST;end->HobLength=sizeof(*end);
 return h;
}
static VOID resource(UINT64 base,UINT64 size,UINT32 type) {
 EFI_HOB_RESOURCE_DESCRIPTOR *h=append(EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,sizeof(*h));
 h->PhysicalStart=base;h->ResourceLength=size;h->ResourceType=type;
}
static VOID allocation(UINT64 base,UINT64 size) {
 EFI_HOB_MEMORY_ALLOCATION *h=append(EFI_HOB_TYPE_MEMORY_ALLOCATION,sizeof(*h));
 h->AllocDescriptor.MemoryBaseAddress=base;h->AllocDescriptor.MemoryLength=size;
}
static unsigned calls;
static UINT64 bases[2],sizes[2];
VOID EFIAPI BuildMemoryAllocationHob(UINT64 base,UINT64 size,EFI_MEMORY_TYPE type) {
 assert(calls<2 && type==EfiBootServicesData);
 assert(metadata.info.Flags==0); // only set RESERVED after both HOBs exist
 bases[calls]=base;sizes[calls]=size;calls++;allocation(base,size);
}
'''
SUFFIX = r'''
static void reset(void) {
 memset(&arena,0,sizeof(arena));memset(&metadata,0,sizeof(metadata));used=0;calls=0;
 EFI_HOB_HANDOFF_INFO_TABLE *h=append(EFI_HOB_TYPE_HANDOFF,sizeof(*h));
 h->EfiFreeMemoryBottom=0x1f0000;h->EfiFreeMemoryTop=0x200000;
 resource(ram_base,ram_size,EFI_RESOURCE_SYSTEM_MEMORY);
 present=TRUE;metadata_size=sizeof(metadata.info);
 metadata.info=(APPLE_ADT_INFO_HOB){1,0,0x120003,0x1800,0x120000,0x2000,0x130000,0x1000};
}
static unsigned cases;
static void expect(EFI_STATUS wanted) {
 cases++;
 UINT32 flags=metadata.info.Flags;
 assert(ReserveAdtCopy(0x1f0000,0x10000)==wanted);
 if(wanted!=EFI_SUCCESS) {assert(calls==0);assert(metadata.info.Flags==flags);}
}
#define BAD(change) do {reset();change;expect(EFI_COMPROMISED_DATA);} while(0)
int main(void) {
 reset();present=FALSE;expect(EFI_SUCCESS);assert(!calls && !metadata.info.Flags);
 BAD(metadata_size--);BAD(metadata.info.Version=2);BAD(metadata.info.Flags=1);
 BAD(metadata.info.AdtPagesSize=0);BAD(metadata.info.AdtPagesBase++);
 BAD(metadata.info.AdtPagesSize++);BAD(metadata.info.AdtPagesBase=0xff000);
 BAD(metadata.info.AdtPagesBase=0x200000);BAD(metadata.info.AdtPagesSize=0x200000);
 BAD(metadata.info.AdtPagesBase=0x1f0000);BAD(metadata.info.BootArgsPagesBase=0x1ff000);
 BAD(metadata.info.AdtBase=0x11ffff);BAD(metadata.info.AdtSize=0);
 BAD(metadata.info.AdtSize=0x2001);BAD(metadata.info.AdtBase=0x121fff);
 BAD(metadata.info.BootArgsPagesBase=0x121000);
 BAD(allocation(0x120000,0x1000));BAD(allocation(0x130000,0x1000));
 BAD(allocation(0x11f000,0x2000));BAD(allocation(MAX_UINT64-3,8));
 BAD(resource(0x121000,0x1000,EFI_RESOURCE_MEMORY_RESERVED));
 BAD(resource(0x130000,0x1000,EFI_RESOURCE_MEMORY_MAPPED_IO));
 BAD(((EFI_HOB_RESOURCE_DESCRIPTOR *)(arena.bytes+sizeof(EFI_HOB_HANDOFF_INFO_TABLE)))->ResourceLength=0x10000);
 BAD(resource(MAX_UINT64-3,8,EFI_RESOURCE_SYSTEM_MEMORY));
 reset();EFI_HOB_HANDOFF_INFO_TABLE *phit=GetHobList();
 UINT64 need=2*sizeof(EFI_HOB_MEMORY_ALLOCATION)+sizeof(EFI_HOB_GENERIC_HEADER);
 phit->EfiFreeMemoryTop=phit->EfiFreeMemoryBottom+need-1;expect(EFI_OUT_OF_RESOURCES);
 reset();phit=GetHobList();phit->EfiFreeMemoryTop=phit->EfiFreeMemoryBottom-1;expect(EFI_OUT_OF_RESOURCES);
 reset();phit=GetHobList();phit->EfiFreeMemoryTop=phit->EfiFreeMemoryBottom+need;
 // Adjacent existing allocations do not intersect either destination.
 allocation(0x11f000,0x1000);allocation(0x122000,0x1000);
 expect(EFI_SUCCESS);assert(calls==2 && metadata.info.Flags==APPLE_ADT_INFO_RESERVED);
 assert(bases[0]==0x120000 && sizes[0]==0x2000 && bases[1]==0x130000 && sizes[1]==0x1000);
 // A second reservation must fail rather than emit duplicate allocation HOBs.
 calls=0;expect(EFI_COMPROMISED_DATA);
 assert(!AdtPagesValid(MAX_UINT64-4095,4096,0,MAX_UINT64,0,0));
 assert(!AdtPagesValid(0x120000,4096,ram_base,ram_size,MAX_UINT64-1,4));
 assert(AdtPagesValid(0x120000,4096,ram_base,ram_size,0x121000,4096));
 printf("production ReserveAdtCopy: %u cases PASS\n",cases);
 return 0;
}
'''

class AdtReservationTests(unittest.TestCase):
    def test_production_bodies(self):
        source=SOURCE.read_text()
        start=source.index('STATIC BOOLEAN AdtPagesValid (')
        end=source.index('//Borrowed from ArmPlatformPkg',start)
        body=source[start:end]  # exact source, no algorithm substitution
        inc=ROOT/'MU_BASECORE/MdePkg/Include'
        arch={'arm64':'AArch64','aarch64':'AArch64','x86_64':'X64','AMD64':'X64'}[platform.machine()]
        with tempfile.TemporaryDirectory() as td:
            c=Path(td)/'reservation.c';exe=Path(td)/'reservation'
            c.write_text(PREFIX+body+SUFFIX)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fshort-wchar',
                '-fsanitize=address,undefined','-I'+str(inc),'-I'+str(inc/arch),
                '-I'+str(ROOT/'Silicon/Apple/AppleSiliconPkg/Include'),str(c),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            self.assertIn('cases PASS',result.stdout)
            print(result.stdout.strip())
if __name__=='__main__':unittest.main()
