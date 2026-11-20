/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_layout.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t le64(const uint8_t *p){return le32(p)|(uint64_t)le32(p+4)<<32;}
static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t crc_update(uint32_t crc,const uint8_t *p,uint32_t n)
{
 while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^((0U-(crc&1U))&0xedb88320U);}
 return crc;
}
static int read_sector(const struct i9100_layout_io *io,uint64_t lba,uint8_t out[512])
{return !io||!io->read512||lba>=io->sectors||io->read512(io->cookie,lba,out)?I9100_LAYOUT_IO:0;}
static int nonzero(const uint8_t *p,unsigned n){unsigned v=0;for(unsigned i=0;i<n;i++)v|=p[i];return v!=0;}
struct header {
 uint64_t self,alternate,first,last,array;
 uint32_t count,entry_size,array_crc,crc;
 uint8_t guid[16];
};
static int header_read(const struct i9100_layout_io *io,uint64_t lba,struct header *h)
{
 uint8_t b[512];int e=read_sector(io,lba,b);if(e)return e;
 uint32_t size=le32(b+12),expected=le32(b+16);
 if(memcmp(b,"EFI PART",8)||le32(b+8)!=0x10000U||size<92||size>512||le32(b+20))return I9100_LAYOUT_HEADER;
 memset(b+16,0,4);if((crc_update(0xffffffffU,b,size)^0xffffffffU)!=expected)return I9100_LAYOUT_CRC;
 h->self=le64(b+24);h->alternate=le64(b+32);h->first=le64(b+40);h->last=le64(b+48);
 memcpy(h->guid,b+56,16);h->array=le64(b+72);h->count=le32(b+80);h->entry_size=le32(b+84);h->array_crc=le32(b+88);h->crc=expected;
 if(h->self!=lba||h->alternate>=io->sectors||h->self==h->alternate||!nonzero(h->guid,16)||
    h->first<2||h->first>h->last||h->last>=io->sectors-1)return I9100_LAYOUT_GEOMETRY;
 if(!h->count||h->count>128||h->entry_size!=128)return I9100_LAYOUT_LIMIT;
 uint64_t sectors=((uint64_t)h->count*128U+511U)/512U;
 if(h->array<2||h->array>=io->sectors||sectors>io->sectors-h->array)return I9100_LAYOUT_GEOMETRY;
 if(lba==1){if(h->alternate!=io->sectors-1||h->array+sectors>h->first)return I9100_LAYOUT_GEOMETRY;}
 else if(lba==io->sectors-1){if(h->alternate!=1||h->array<=h->last||h->array+sectors>lba)return I9100_LAYOUT_GEOMETRY;}
 else return I9100_LAYOUT_GEOMETRY;
 return 0;
}
static int parse_entry(const uint8_t *b,unsigned number,const struct header *h,struct i9100_emmc_layout *out)
{
 if(!nonzero(b,16))return 0;
 if(out->count>=I9100_EMMC_PARTITIONS)return I9100_LAYOUT_LIMIT;
 uint64_t first=le64(b+32),last=le64(b+40);
 if(!nonzero(b+16,16)||first<h->first||last>h->last||first>last)return I9100_LAYOUT_ENTRY;
 for(unsigned n=0;n<out->count;n++){
  const struct i9100_emmc_partition *p=&out->partitions[n];
  if(!memcmp(b+16,p->unique_guid,16))return I9100_LAYOUT_ENTRY;
  if(first<p->start_lba+p->sectors&&p->start_lba<=last)return I9100_LAYOUT_OVERLAP;
 }
 struct i9100_emmc_partition *p=&out->partitions[out->count];
 memcpy(p->type_guid,b,16);memcpy(p->unique_guid,b+16,16);
 p->start_lba=first;p->sectors=last-first+1;p->attributes=le64(b+48);p->gpt_number=number;p->pit_identifier=UINT32_MAX;
 unsigned ended=0;
 for(unsigned n=0;n<36;n++){
  unsigned ch=(unsigned)b[56+n*2]|(unsigned)b[57+n*2]<<8;
  if(!ch)ended=1;
  else if(ended||ch<32||ch>126)return I9100_LAYOUT_ENTRY;
  p->name[n]=(char)ch;
 }
 p->name[36]=0;
 for(unsigned n=0;n<out->count;n++)if(p->name[0]&&!strcmp(p->name,out->partitions[n].name))return I9100_LAYOUT_ENTRY;
 out->count++;return 0;
}
static int table_read(const struct i9100_layout_io *io,const struct header *h,struct i9100_emmc_layout *out)
{
 uint8_t b[512];uint32_t crc=0xffffffffU,bytes=h->count*128U,done=0;
 while(done<bytes){
  int e=read_sector(io,h->array+done/512U,b);if(e)return e;
  uint32_t n=bytes-done;if(n>512)n=512;crc=crc_update(crc,b,n);
  if(out)for(unsigned at=0;at<n;at+=128){e=parse_entry(b+at,(done+at)/128U+1U,h,out);if(e)return e;}
  done+=n;
 }
 return (crc^0xffffffffU)==h->array_crc?0:I9100_LAYOUT_CRC;
}
int i9100_emmc_read_layout(const struct i9100_layout_io *io,struct i9100_emmc_layout *out)
{
 if(!io||!out||io->sectors<68||io->sectors>UINT64_MAX/512U)return I9100_LAYOUT_GEOMETRY;
 memset(out,0,sizeof(*out));uint8_t mbr[512];int e=read_sector(io,0,mbr);if(e)return e;
 if(mbr[510]!=0x55||mbr[511]!=0xaa)return I9100_LAYOUT_HEADER;
 unsigned protective=0;
 for(unsigned n=0;n<4;n++){
  const uint8_t *p=mbr+446+n*16;
  if(p[4]==0xee){uint32_t size=io->sectors-1>UINT32_MAX?UINT32_MAX:(uint32_t)(io->sectors-1);
   if(protective++||le32(p+8)!=1||le32(p+12)!=size)return I9100_LAYOUT_GEOMETRY;
  }else if(p[4]||le32(p+12))return I9100_LAYOUT_HEADER;
 }
 if(protective!=1)return I9100_LAYOUT_HEADER;
 struct header primary,backup;e=header_read(io,1,&primary);if(e)return e;
 e=header_read(io,io->sectors-1,&backup);if(e)return e;
 if(memcmp(primary.guid,backup.guid,16)||primary.first!=backup.first||primary.last!=backup.last||
    primary.count!=backup.count||primary.entry_size!=backup.entry_size||primary.array_crc!=backup.array_crc)return I9100_LAYOUT_GEOMETRY;
 e=table_read(io,&primary,out);if(e)return e;
 e=table_read(io,&backup,0);if(e)return e;
 out->sectors=io->sectors;out->first_usable=primary.first;out->last_usable=primary.last;
 out->primary_array=primary.array;out->backup_array=backup.array;
 memcpy(out->disk_guid,primary.guid,16);out->primary_crc=primary.crc;out->backup_crc=backup.crc;out->entries_crc=primary.array_crc;
 out->valid=1;return 0;
}
const struct i9100_emmc_partition *i9100_emmc_find_partition(const struct i9100_emmc_layout *l,const char *name)
{
 if(!l||!name||!l->valid)return 0;
 for(unsigned n=0;n<l->count;n++)if(!strcmp(l->partitions[n].name,name))return &l->partitions[n];
 return 0;
}
static int read_bytes(const struct i9100_layout_io *io,uint64_t offset,uint8_t *out,unsigned bytes)
{
 uint8_t sector[512];
 while(bytes){
  int e=read_sector(io,offset/512U,sector);if(e)return e;
  unsigned at=(unsigned)(offset&511U),n=512U-at;if(n>bytes)n=bytes;
  memcpy(out,sector+at,n);out+=n;offset+=n;bytes-=n;
 }
 return 0;
}
int i9100_emmc_check_pit(const struct i9100_layout_io *io,struct i9100_emmc_layout *l)
{
 if(!io||!l||!l->valid||l->sectors!=io->sectors||!l->count||l->count>I9100_EMMC_PARTITIONS)return I9100_LAYOUT_PIT;
 l->pit_valid=0;uint8_t h[28],entry[132];
 int e=read_bytes(io,0x4400U,h,sizeof(h));if(e)return e;
 uint32_t count=le32(h+4);if(le32(h)!=0x12349876U||!count||count>32)return I9100_LAYOUT_PIT;
 uint32_t found=0;
 for(unsigned n=0;n<l->count;n++)l->partitions[n].pit_identifier=UINT32_MAX;
 for(unsigned n=0;n<count;n++){
  e=read_bytes(io,0x4400ULL+28U+(uint64_t)n*132U,entry,sizeof(entry));if(e)return e;
  char name[33];memcpy(name,entry+36,32);name[32]=0;
  unsigned terminated=0;
  for(unsigned j=0;j<32;j++)if(!name[j]){terminated=1;break;}
  if(!terminated)return I9100_LAYOUT_PIT;
  for(unsigned p=0;p<l->count;p++)if(!strcmp(name,l->partitions[p].name)){
   if((found&(1U<<p))||le32(entry)!=0||le32(entry+4)!=2||
      le32(entry+20)!=l->partitions[p].start_lba||le32(entry+24)!=l->partitions[p].sectors)return I9100_LAYOUT_PIT;
   uint32_t id=le32(entry+8);
   for(unsigned j=0;j<l->count;j++)if((found&(1U<<j))&&l->partitions[j].pit_identifier==id)return I9100_LAYOUT_PIT;
   l->partitions[p].pit_identifier=id;found|=1U<<p;
  }
 }
 uint32_t expected=l->count==32?UINT32_MAX:(1U<<l->count)-1U;
 if(found!=expected)return I9100_LAYOUT_PIT;
 l->pit_valid=1;return 0;
}
static int hfs_header(const uint8_t *h,uint64_t capacity,struct i9100_hfs_volume *v)
{
 v->signature=be16(h);v->version=be16(h+2);v->attributes=be32(h+4);
 v->create_date=be32(h+16);v->journal_info_block=be32(h+12);memcpy(v->finder_volume_id,h+104,8);
 if(!((v->signature==0x482bU&&v->version==4)||(v->signature==0x4858U&&v->version==5)))return I9100_LAYOUT_HFS;
 v->allocation_block=be32(h+40);v->total_blocks=be32(h+44);v->free_blocks=be32(h+48);
 if(v->allocation_block<512U||v->allocation_block>1048576U||
    (v->allocation_block&(v->allocation_block-1U))||!v->total_blocks||v->free_blocks>v->total_blocks)return I9100_LAYOUT_HFS;
 v->bytes=(uint64_t)v->allocation_block*v->total_blocks;
 if(v->bytes<4096U||v->bytes>capacity)return I9100_LAYOUT_HFS;
 return 0;
}
int i9100_emmc_hfs_volume(const struct i9100_layout_io *io,const struct i9100_emmc_partition *p,struct i9100_hfs_volume *v)
{
 if(!io||!p||!v||p->start_lba>=io->sectors||p->sectors>io->sectors-p->start_lba||p->sectors<8||p->sectors>UINT64_MAX/512U)return I9100_LAYOUT_GEOMETRY;
 uint8_t h[512];int e=read_sector(io,p->start_lba+2U,h);if(e)return e;
 e=hfs_header(h,p->sectors*512U,v);if(e)return e;
 e=read_sector(io,p->start_lba+v->bytes/512U-2U,h);if(e)return e;
 struct i9100_hfs_volume alternate;e=hfs_header(h,p->sectors*512U,&alternate);if(e)return e;
 if(alternate.signature!=v->signature||alternate.version!=v->version||
    alternate.allocation_block!=v->allocation_block||alternate.total_blocks!=v->total_blocks||
    alternate.create_date!=v->create_date||memcmp(alternate.finder_volume_id,v->finder_volume_id,8))return I9100_LAYOUT_HFS;
 return 0;
}
