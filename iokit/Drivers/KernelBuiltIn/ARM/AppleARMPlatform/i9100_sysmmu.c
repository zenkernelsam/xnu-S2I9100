/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sysmmu.h"
#include <string.h>
static int valid(struct i9100_dma_buffer *b)
{return b&&b->self==(uintptr_t)b&&b->state==I9100_DMA_CPU;}
static int collect(struct i9100_dma_buffer *table,const struct i9100_sysmmu_mapping *maps,unsigned count,unsigned sections[16],unsigned *section_count)
{
 if(!valid(table)||!maps||!count||count>16U||table->bytes<32768U||(table->physical&16383U)||(table->cpu&3U))return I9100_MMU_ARGUMENT;
 *section_count=0;
 for(unsigned n=0;n<count;n++){
  const struct i9100_sysmmu_mapping *r=&maps[n];
  if(!valid(r->memory)||!r->bytes||(r->offset&4095U)||(r->bytes&4095U)||
     r->offset>r->memory->bytes||r->bytes>r->memory->bytes-r->offset||
     (r->memory->physical&4095U)||
     (table->physical<(uint64_t)r->memory->physical+r->memory->bytes&&r->memory->physical<(uint64_t)table->physical+table->bytes))return I9100_MMU_ARGUMENT;
  uint32_t first=r->memory->physical+r->offset;
  for(unsigned old=0;old<n;old++){
   uint32_t prev=maps[old].memory->physical+maps[old].offset;
   if(first<(uint64_t)prev+maps[old].bytes&&prev<(uint64_t)first+r->bytes)return I9100_MMU_ARGUMENT;
  }
  for(unsigned section=first>>20;section<=((uint64_t)first+r->bytes-1U)>>20;section++){
   unsigned index=0;while(index<*section_count&&sections[index]!=section)index++;
   if(index==*section_count){if(*section_count==16U)return I9100_MMU_ARGUMENT;sections[(*section_count)++]=section;}
  }
 }
 return 0;
}
int i9100_sysmmu_build(struct i9100_dma_buffer *table,const struct i9100_sysmmu_mapping *maps,unsigned count)
{
 unsigned sections[16],section_count=0;int e=collect(table,maps,count,sections,&section_count);if(e)return e;
 uint32_t *words=(uint32_t *)table->cpu;memset(words,0,32768);
 for(unsigned n=0;n<section_count;n++)words[sections[n]]=(table->physical+16384U+n*1024U)|1U;
 for(unsigned n=0;n<count;n++){
  uint32_t first=maps[n].memory->physical+maps[n].offset;
  for(unsigned off=0;off<maps[n].bytes;off+=4096U){
   uint32_t pa=first+off;unsigned section=0;while(sections[section]!=(pa>>20))section++;
   words[4096U+section*256U+((pa>>12)&255U)]=pa|2U;
  }
 }
 return 0;
}
static int validate_table(struct i9100_dma_buffer *table,const struct i9100_sysmmu_mapping *maps,unsigned count)
{
 unsigned sections[16],number=0;int e=collect(table,maps,count,sections,&number);if(e)return e;
 const uint32_t *words=(const uint32_t *)table->cpu;
 for(unsigned n=0;n<4096U;n++){
  unsigned slot=0;while(slot<number&&sections[slot]!=n)slot++;
  uint32_t expected=slot==number?0U:(table->physical+16384U+slot*1024U)|1U;
  if(words[n]!=expected)return I9100_MMU_ARGUMENT;
 }
 for(unsigned slot=0;slot<16U;slot++)for(unsigned n=0;n<256U;n++){
  uint32_t expected=0;
  if(slot<number){uint32_t pa=(sections[slot]<<20)|(n<<12);
   for(unsigned r=0;r<count;r++){
    uint32_t start=maps[r].memory->physical+maps[r].offset;
    if(pa>=start&&(uint64_t)pa<(uint64_t)start+maps[r].bytes){expected=pa|2U;break;}
   }
  }
  if(words[4096U+slot*256U+n]!=expected)return I9100_MMU_ARGUMENT;
 }
 return 0;
}
static uint32_t rd(struct i9100_sysmmu *s,unsigned unit,unsigned off){return s->io.read32(s->io.cookie,unit,off);}
static void wr(struct i9100_sysmmu *s,unsigned unit,unsigned off,uint32_t value)
{s->io.write32(s->io.cookie,unit,off,value);s->io.barrier(s->io.cookie);}
int i9100_sysmmu_init(struct i9100_sysmmu *s,const struct i9100_sysmmu_io *io,unsigned units)
{
 if(!s||s->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->masters_idle||!io->barrier||!units||units>2U)
  return I9100_MMU_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->initialized=1;s->units=units;return 0;
}
int i9100_sysmmu_enable(struct i9100_sysmmu *s,int enabled)
{
 if(!s||!s->initialized||(enabled!=0&&enabled!=1))return I9100_MMU_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_MMU_BUSY;
 int e=s->attached||s->table||s->fault?I9100_MMU_BUSY:0;
 if(!e)s->enabled=enabled;s->io.unlock(s->io.cookie);return e;
}
int i9100_sysmmu_idle(void *cookie)
{
 struct i9100_sysmmu *s=cookie;if(!s||!s->initialized||s->io.masters_idle(s->io.cookie))return -1;
 for(unsigned n=0;n<s->units;n++)if(rd(s,n,0)&5U)return -1;return 0;
}
static int unpin(struct i9100_sysmmu *s)
{
 for(unsigned n=0;n<s->mapped_count;n++)if(s->mapped[n]){
  if(i9100_dma_unhold(s->mapped[n]))return I9100_MMU_IO;
  s->mapped[n]=0;
 }
 s->mapped_count=0;return 0;
}
static int detach_locked(struct i9100_sysmmu *s)
{
 if(!s->table)return I9100_MMU_DISABLED;
 if(s->io.masters_idle(s->io.cookie))return I9100_MMU_BUSY;
 for(unsigned n=0;n<s->units;n++)wr(s,n,0,0);
 if(i9100_sysmmu_idle(s)){s->fault=1;return I9100_MMU_IO;}
 int e=0;
 for(unsigned n=0;n<s->units;n++){
  wr(s,n,4,s->saved[n][0]);wr(s,n,0x14,s->saved[n][1]);
  if(rd(s,n,4)!=s->saved[n][0]||rd(s,n,0x14)!=s->saved[n][1])e=I9100_MMU_IO;
 }
 if(s->table->state==I9100_DMA_DEVICE||s->table->state==I9100_DMA_QUARANTINED)
  if(i9100_dma_abort(s->table))e=I9100_MMU_IO;
 if(e){s->fault=1;return e;}
 e=unpin(s);if(e){s->fault=1;return e;}
 s->table=0;s->attached=s->fault=0;return 0;
}
int i9100_sysmmu_attach(struct i9100_sysmmu *s,struct i9100_dma_buffer *table,const struct i9100_sysmmu_mapping *maps,unsigned count,int verified)
{
 if(!s||!s->initialized||!valid(table)||table->bytes<32768U||(table->physical&16383U)||verified!=1)return I9100_MMU_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_MMU_BUSY;
 int e=validate_table(table,maps,count);if(e)goto done;
 if(!s->enabled){e=I9100_MMU_DISABLED;goto done;}if(s->table||s->mapped_count||s->fault||s->io.masters_idle(s->io.cookie)){e=I9100_MMU_BUSY;goto done;}
 for(unsigned n=0;n<s->units;n++){
  unsigned version=rd(s,n,0x34)>>28;
  if(version!=1U&&version!=2U){e=I9100_MMU_VERSION;goto done;}
  if(rd(s,n,0)||rd(s,n,0x18)){e=I9100_MMU_BUSY;goto done;}
  s->saved[n][0]=rd(s,n,4);s->saved[n][1]=rd(s,n,0x14);
 }
 for(unsigned n=0;n<count;n++){
  unsigned old=0;while(old<s->mapped_count&&s->mapped[old]!=maps[n].memory)old++;
  if(old==s->mapped_count){
   if(i9100_dma_hold(maps[n].memory)){e=I9100_MMU_IO;if(unpin(s))s->fault=1;goto done;}
   s->mapped[s->mapped_count++]=maps[n].memory;
  }
 }
 s->table=table;e=i9100_dma_prepare(table,I9100_DMA_TO_DEVICE);
 if(e){e=I9100_MMU_IO;(void)detach_locked(s);goto done;}
 for(unsigned n=0;n<s->units;n++){
  wr(s,n,4,1);wr(s,n,0x14,table->physical);wr(s,n,0x0c,1);wr(s,n,0,5);
  if(rd(s,n,0)!=5U||rd(s,n,0x14)!=table->physical){e=I9100_MMU_IO;s->fault=1;(void)detach_locked(s);goto done;}
 }
 s->attached=1;
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_sysmmu_detach(struct i9100_sysmmu *s)
{
 if(!s||!s->initialized)return I9100_MMU_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_MMU_BUSY;
 int e=detach_locked(s);s->io.unlock(s->io.cookie);return e;
}
int i9100_sysmmu_fault(struct i9100_sysmmu *s)
{
 if(!s||!s->initialized)return I9100_MMU_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_MMU_BUSY;
 int e=0;if(!s->attached){e=I9100_MMU_DISABLED;goto done;}
 for(unsigned n=0;n<s->units;n++){
  uint32_t status=rd(s,n,0x18);if(status){s->fault_status[n]=status;s->fault_address[n]=rd(s,n,0x24);s->fault=1;e=I9100_MMU_FAULT;}
 }
done:
 s->io.unlock(s->io.cookie);return e;
}
