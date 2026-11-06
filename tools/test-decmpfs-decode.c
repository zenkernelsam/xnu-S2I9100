#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../bsd/kern/ios7lab_decmpfs_decode.h"
int main(int argc,char **argv)
{
    unsigned sizes[]={1,4096,65536};
    for(unsigned i=0;i<3;i++) {
        unsigned n=sizes[i];uLongf compressed_size=compressBound(n);
        unsigned char *raw=malloc(n+1),*compressed=malloc(compressed_size),*out=malloc(n);
        raw[0]=0xff;for(unsigned j=0;j<n;j++)raw[j+1]=(unsigned char)(j*31+7);
        z_stream z={0};uInt produced=0;
        if(ios7lab_decmpfs_decode(&z,raw,n+1,out,n,&produced)||produced!=n||memcmp(out,raw+1,n))return 1;
        memset(&z,0,sizeof(z));
        if(ios7lab_decmpfs_decode(&z,raw,n+1,out,n-1,&produced)==Z_OK)return 2;
        if(compress2(compressed,&compressed_size,raw+1,n,9)!=Z_OK)return 3;
        memset(&z,0,sizeof(z));
        if(ios7lab_decmpfs_decode(&z,compressed,compressed_size,out,n,&produced)||produced!=n||memcmp(out,raw+1,n))return 4;
        memset(&z,0,sizeof(z));
        if(ios7lab_decmpfs_decode(&z,compressed,compressed_size-1,out,n,&produced)==Z_OK)return 5;
        free(raw);free(compressed);free(out);
    }
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");if(!f)return 6;
        fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
        unsigned char *raw=malloc(n),*out=malloc(n);
        if(fread(raw,1,n,f)!=(size_t)n||raw[0]!=0xff)return 7;
        fclose(f);z_stream z={0};uInt produced=0;
        if(ios7lab_decmpfs_decode(&z,raw,n,out,n-1,&produced)||produced!=n-1||memcmp(out,raw+1,n-1))return 8;
        free(raw);free(out);
    }
    puts("PASS decmpfs deflate/stored blocks, bounds, truncation, real image fixture");
    return 0;
}
