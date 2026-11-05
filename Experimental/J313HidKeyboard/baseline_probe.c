#include "hid_keyboard.h"
#include <stdio.h>
static hk_descriptor d;
int main(int argc,char **argv) { unsigned char b[4096]; if(argc!=2)return 2; FILE *f=fopen(argv[1],"rb"); if(!f)return 3; size_t n=fread(b,1,sizeof b,f); fclose(f); hk_status s=hk_parse(b,n,&d); printf("bytes=%zu status=%d valid=%u reports=%u\n",n,s,d.valid,d.report_count); for(unsigned i=0;i<d.report_count;++i) printf("id=%02x input=%u output=%u feature=%u fields=%u\n",d.reports[i].id,d.reports[i].bits,d.reports[i].output_bits,d.reports[i].feature_bits,d.reports[i].field_count); return 0; }
