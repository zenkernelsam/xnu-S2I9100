/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_WRITER_MAIN writer_fixture_main
#include "i9100_emmc_writer_test.c"
#include "emmc-read-stream.h"
#include <unistd.h>
static struct i9ro_stream capture;
static uint8_t chunk[I9RO_CHUNK_BYTES];
static FILE *stream_out;
static struct disk source_disk;
static unsigned reads,emit_calls,identities,read_failure,emit_failure,identity_failure,table_drift;
static uint32_t identity_cid[4]={0x15010000,0x53324939,0x31303000,0x9a73abcd};
static const uint8_t model_producer[32]={1,2,3,4,5,6,7,8};
static int ro_identity(void *cookie,struct i9ro_identity *identity)
{
 assert(cookie==&source_disk);if(++identities==identity_failure)return -1;
 identity->sectors=source_disk.sectors;memcpy(identity->cid,identity_cid,sizeof(identity_cid));
 identity->major=179;identity->minor=0;identity->flags=I9RO_SAFE_FLAGS|I9RO_MODEL_FLAG;return 0;
}
static int ro_read(void *cookie,uint64_t offset,uint32_t bytes,uint8_t *out)
{
 assert(cookie==&source_disk&&!(offset&511U)&&!(bytes&511U));
 if(++reads==read_failure)return -1;
 for(unsigned i=0;i<bytes/512U;i++)if(disk_read(&source_disk,offset/512U+i,out+i*512U)){
  if(source_disk.sectors==30777344U)return -1;
  // Small explicit model disk: deterministic data sectors fill the body.
  for(unsigned j=0;j<512U;j++)out[i*512U+j]=(uint8_t)((offset/512U+i)*37U+j*13U);
 }
 if(table_drift&&identities>=2&&offset==512)out[34]^=1;
 return 0;
}
static int emit(void *cookie,const uint8_t *bytes,uint32_t n)
{
 assert(cookie==&source_disk);if(++emit_calls==emit_failure)return -1;
 return stream_out&&fwrite(bytes,1,n,stream_out)==n?0:-1;
}
static const char *texts[]={"179:0\n","1501000053324939313030009a73abcd\n","", "MMC\n",
 "10 1 0:10 / / rw - tmpfs tmpfs rw\n","Filename\t\t\tType\t\tSize\t\tUsed\t\tPriority\n",
 "12550000-12550fff : s3c-mshci\n","UNAVAILABLE: modeled clock summary\n","512\n"};
static char sectors_text[32];
static int metadata_read(void *cookie,uint32_t kind,const uint8_t **out,uint32_t *n)
{assert(cookie==&source_disk&&kind>=16&&kind<=24);*out=(const uint8_t *)texts[kind-16];*n=strlen(texts[kind-16]);return 0;}
static void setup_read(const char *path)
{
 memset(&source_disk,0,sizeof(source_disk));source_disk.head=malloc(1048576);assert(source_disk.head);
 FILE *f=fopen(path,"rb");assert(f&&fread(source_disk.head,1,1048576,f)==1048576&&!fclose(f));
 source_disk.sectors=30777344U;reseal(&source_disk);
 // The large FIFO/card model only reads backed sector content; the transfer
 // stream's complete small-disk case is injected through a separate adapter.
 reads=emit_calls=identities=read_failure=emit_failure=identity_failure=table_drift=0;
 snprintf(sectors_text,sizeof(sectors_text),"%llu\n",(unsigned long long)source_disk.sectors);texts[2]=sectors_text;
}
static void finish_read(){assert(stream_out&&!fclose(stream_out));stream_out=0;free(source_disk.head);source_disk.head=0;}
static void setup_small(const char *path)
{
 setup_read(path);source_disk.sectors=8192;
 uint8_t *h=source_disk.head+512,*array=source_disk.head+get64(h+72)*512U;
 put64(h+32,source_disk.sectors-1);put64(h+48,source_disk.sectors-34);
 uint8_t *mbr=source_disk.head+446;put32(mbr+12,(uint32_t)source_disk.sectors-1);
 for(unsigned i=0;i<get32(h+80);i++)if(memcmp(array+i*128U,(uint8_t[16]){0},16)){
  unsigned start=64U+i*128U;put64(array+i*128U+32,start);put64(array+i*128U+40,start+63U);
  for(unsigned j=0;j<get32(source_disk.head+0x4404);j++){
   uint8_t *pit=source_disk.head+0x4400+28+j*132;
   char name[37]={0};for(unsigned k=0;k<36;k++)name[k]=array[i*128U+56+k*2];
   if(!strcmp((char *)pit+36,name)){put32(pit+20,start);put32(pit+24,64);}
  }
 }
 reseal(&source_disk);snprintf(sectors_text,sizeof(sectors_text),"%llu\n",(unsigned long long)source_disk.sectors);
}
static uint8_t stream_nonce[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static unsigned terminal_frames(const uint8_t *raw,unsigned n)
{unsigned at=128,ends=0;while(at+32<=n){unsigned count=get32(raw+at+12);if(count>n-at-32)break;
 if((get32(raw+at+8)&65535U)==I9RO_END)ends++;at+=32+count;}return ends;}
int main(int argc,char **argv)
{
 alarm(20);assert(argc==3);const struct i9ro_io io={&source_disk,ro_identity,ro_read,emit,metadata_read,model_producer};
 setup_read(argv[1]);assert(i9ro_emit(0,&capture,&io,I9RO_INVENTORY,stream_nonce,0,chunk,sizeof(chunk))==I9RO_DISABLED&&!reads&&!identities);
 char path[1024];snprintf(path,sizeof(path),"%s/inventory.stream",argv[2]);stream_out=fopen(path,"wb");assert(stream_out);
 assert(!i9ro_emit(1,&capture,&io,I9RO_INVENTORY,stream_nonce,0,chunk,sizeof(chunk))&&!capture.data_bytes&&identities==2);
 assert(capture.sequence==22);finish_read();
 for(unsigned fault=1;fault<=4;fault++){
  setup_read(argv[1]);snprintf(path,sizeof(path),"%s/fault-%u.stream",argv[2],fault);stream_out=fopen(path,"w+b");assert(stream_out);
  if(fault==1)read_failure=4;if(fault==2)emit_failure=5;if(fault==3)identity_failure=2;if(fault==4)table_drift=1;
  assert(i9ro_emit(1,&capture,&io,I9RO_INVENTORY,stream_nonce,0,chunk,sizeof(chunk))&&capture.error);
  assert(!fflush(stream_out));long n=ftell(stream_out);assert(n>=0);rewind(stream_out);
  uint8_t *raw=malloc((size_t)n+1);assert(raw);assert(fread(raw,1,n,stream_out)==(size_t)n);assert(!terminal_frames(raw,(unsigned)n));free(raw);finish_read();
 }
 setup_read(argv[1]);snprintf(path,sizeof(path),"%s/wrong-cid.stream",argv[2]);stream_out=fopen(path,"w+b");assert(stream_out);uint32_t wrong[4];memcpy(wrong,identity_cid,sizeof(wrong));wrong[0]^=1;
 assert(i9ro_emit(1,&capture,&io,I9RO_BACKUP_USER,stream_nonce,wrong,chunk,sizeof(chunk))==I9RO_IDENTITY&&!reads&&!emit_calls);finish_read();
 setup_small(argv[1]);snprintf(path,sizeof(path),"%s/backup-small.stream",argv[2]);stream_out=fopen(path,"w+b");assert(stream_out);
 assert(!i9ro_emit(1,&capture,&io,I9RO_BACKUP_USER,stream_nonce,identity_cid,chunk,sizeof(chunk))&&capture.data_bytes==4194304U);finish_read();
 for(unsigned fault=1;fault<=3;fault++){
  setup_small(argv[1]);snprintf(path,sizeof(path),"%s/backup-fault-%u.stream",argv[2],fault);stream_out=fopen(path,"w+b");assert(stream_out);
  if(fault==1)read_failure=10; // after initial tables and two body chunks.
  if(fault==2)identity_failure=3; // final identity; no END published.
  if(fault==3)emit_failure=33; // failure while outputting an actual data chunk.
  assert(i9ro_emit(1,&capture,&io,I9RO_BACKUP_USER,stream_nonce,identity_cid,chunk,sizeof(chunk))&&capture.error);finish_read();
 }
 puts("PASS actual read-only stream: historical primary+explicit backup fixture, CID/CRC/order/pre-post tables, wrong CID/default OFF/read-output-identity-table faults denied, no END on failure; no device writes/contacts");return 0;
}
