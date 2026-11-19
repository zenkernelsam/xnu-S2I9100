/* SPDX-License-Identifier: BSD-2-Clause
 * Actual MSHCI/read/write policy with a historical primary GPT/PIT and
 * explicitly synthetic backup GPT + card/FIFO/persistent-media facade. */
#define main emmc_readonly_fixture_main
#include "i9100_emmc_test.c"
#undef main
#include "i9100_emmc_writer.h"
struct writer_model {
 struct model m;
 struct disk d;
 struct i9100_emmc_card card;
 uint8_t stored[16384];
 uint64_t stored_lba;
 uint32_t stored_count,write_position,expected_words;
 unsigned write_active,cache_enabled,flushes,deny_exclusive,claimed,full_stuck,corrupt_readback,counter_short;
 unsigned error_write,write_commands,legacy_switch,freeze_time,flush_error,drop_argument;
};
static struct writer_model wm;
static struct i9100_emmc_writer writer;
static uint8_t transfer_bytes[16384];
static int claim(void *v){struct writer_model *w=v;if(w->claimed)return -1;w->claimed=1;return 0;}
static void release_claim(void *v){struct writer_model *w=v;assert(w->claimed);w->claimed=0;}
static int exclusive(void *v,const struct i9100_emmc_card *c)
{struct writer_model *w=v;assert(w->claimed);return w->deny_exclusive||c!=&w->card?-1:0;}
static uint32_t wread(void *v,unsigned off)
{
 struct writer_model *w=v;
 if(off==EM_STATUS&&w->write_active)return w->full_stuck?1U<<3:0;
 return read32(&w->m,off);
}
static void wwrite(void *v,unsigned off,uint32_t value)
{
 struct writer_model *w=v;struct model *m=&w->m;
 if(off==EM_CMDARG&&w->drop_argument)return;
 if(off==EM_DATA){
  assert(w->write_active&&w->write_position<w->expected_words&&!w->full_stuck);
  unsigned at=w->write_position++*4U;
  for(unsigned i=0;i<4;i++)w->stored[at+i]=(uint8_t)(value>>(i*8U));
  if(w->write_position==w->expected_words){
   w->write_active=0;m->regs[EM_RINTSTS/4]|=1U<<3;m->state=w->stored_count==1U?4U:5U;
   m->regs[EM_TCBCNT/4]=w->expected_words*4U-(w->counter_short?4U:0U);m->regs[EM_TBBCNT/4]=w->expected_words*4U;
  }return;
 }
 if(off==EM_CMD&&!(value&(1U<<21))&&((value&63U)==24U||(value&63U)==25U)){
  assert(m->state==4&&value&(1U<<10)&&value&(1U<<9));unsigned cmd=value&63U;
  m->regs[off/4]=value&~(1U<<31);m->regs[EM_RINTSTS/4]|=1U<<2;m->commands++;w->write_commands++;
  response(m,(4U<<9)|(1U<<8));w->stored_lba=m->regs[EM_CMDARG/4];w->stored_count=m->regs[EM_BYTCNT/4]/512U;
  assert(w->stored_count&&w->stored_count<=32U&&(cmd==25U||w->stored_count==1U));
  w->write_position=0;w->expected_words=w->stored_count*128U;w->write_active=1;m->count=m->position=0;
  m->regs[EM_TCBCNT/4]=m->regs[EM_TBBCNT/4]=0;m->state=7U;
  if(w->error_write)m->regs[EM_RINTSTS/4]|=1U<<7;return;
 }
 if(off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==6U){
  assert(w->cache_enabled&&m->regs[EM_CMDARG/4]==0x03200100U&&!(value&(1U<<9)));
  m->regs[off/4]=value&~(1U<<31);m->regs[EM_RINTSTS/4]|=1U<<2;response(m,(4U<<9)|(1U<<8));
  w->flushes++;m->commands++;if(w->flush_error)response(m,1U<<7);return;
 }
 write32(m,off,value);
 if(off!=EM_CMD||value&(1U<<21))return;
 unsigned cmd=value&63U;
 if(cmd==8U){
  unsigned word=33U/4U;m->words[word]|=w->cache_enabled<<(8U*(33U%4U));
  m->words[249U/4U]|=64U<<(8U*(249U%4U));
 }
 if(cmd==17U||cmd==18U){
  uint64_t lba=m->regs[EM_CMDARG/4];unsigned count=m->regs[EM_BYTCNT/4]/512U;
  for(unsigned i=0;i<count;i++){
   uint8_t sector[512];
   if(w->stored_count&&lba+i>=w->stored_lba&&lba+i<w->stored_lba+w->stored_count){
    memcpy(sector,w->stored+(lba+i-w->stored_lba)*512U,512U);if(w->corrupt_readback)sector[0]^=1U;
   }else if(disk_read(&w->d,lba+i,sector))memset(sector,0,512);
   for(unsigned k=0;k<128U;k++)m->words[i*128U+k]=get32(sector+k*4U);
  }
 }
}
static uint64_t wnow(void *v){struct writer_model *w=v;return w->freeze_time?0:now(&w->m);}
static int wdelay(void *v,uint32_t us){return delay(&((struct writer_model *)v)->m,us);}
static struct i9100_emmc_writer_identity setup_writer(const char *path,unsigned cache)
{
 memset(&wm,0,sizeof(wm));wm.cache_enabled=cache;
 wm.d.head=malloc(1048576U);assert(wm.d.head);FILE *f=fopen(path,"rb");
 assert(f&&fread(wm.d.head,1,1048576,f)==1048576);assert(!fclose(f));
 wm.d.sectors=get64(wm.d.head+512+32)+1;reseal(&wm.d); /* Explicit synthetic backup sectors. */
 struct i9100_layout_io lio={&wm.d,disk_read,wm.d.sectors};struct i9100_emmc_layout layout={0};
 assert(!i9100_emmc_read_layout(&lio,&layout)&&!i9100_emmc_check_pit(&lio,&layout));
 const struct i9100_emmc_partition *sys=i9100_emmc_find_partition(&layout,"FACTORYFS"),*data=i9100_emmc_find_partition(&layout,"DATAFS");assert(sys&&data);
 wm.m.regs[EM_VERID/4]=0x5342220aU;wm.m.regs[EM_HCON/4]=1U<<7;wm.m.regs[EM_PWREN/4]=1;wm.m.inject_command=UINT32_MAX;
 struct i9100_emmc_io io={&wm,wread,wwrite,wnow,wdelay,100000000U,20000000U,5000U};
 i9100_emmc_init(&wm.card,&io);assert(!i9100_emmc_probe(&wm.card));
 struct i9100_emmc_writer_identity identity={0};memcpy(identity.cid,wm.card.cid,sizeof(identity.cid));
 memcpy(identity.disk_guid,layout.disk_guid,16);memcpy(identity.system_guid,sys->unique_guid,16);memcpy(identity.data_guid,data->unique_guid,16);
 identity.primary_crc=layout.primary_crc;identity.backup_crc=layout.backup_crc;identity.entries_crc=layout.entries_crc;identity.sectors=layout.sectors;
 memset(&writer,0,sizeof(writer));struct i9100_emmc_writer_io wio={&wm,claim,release_claim,exclusive};
 assert(!i9100_emmc_writer_init(&writer,&wio));return identity;
}
static void done(void){assert(!wm.claimed);free(wm.d.head);wm.d.head=0;}
static void tests(const char *path)
{
 for(unsigned i=0;i<sizeof(transfer_bytes);i++)transfer_bytes[i]=(uint8_t)(i*17U+9U);
 struct i9100_emmc_writer_identity id=setup_writer(path,0);
 assert(i9100_emmc_writer_open(&writer,0,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,12582912ULL)==I9100_EMMC_UNSUPPORTED&&!wm.write_commands);
 uint64_t high=3489792ULL+8388608ULL;
 assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32U));
 assert(wm.card.write_grant==&writer.grant&&i9100_emmc_probe(&wm.card)==I9100_EMMC_BUSY);
 assert(!i9100_emmc_writer_write(&writer,0,32U,transfer_bytes)&&writer.verified_sectors==32U);
 assert(writer.grant.dirty&&writer.grant.words_submitted==4096U&&writer.grant.ciu_bytes==16384U&&writer.grant.host_bytes==16384U);
 assert(!memcmp(wm.stored,transfer_bytes,16384)&&wm.stored_lba==high&&wm.write_commands==1U);
 unsigned writes=wm.write_commands;
 assert(i9100_emmc_writer_write(&writer,32U,1U,transfer_bytes)==I9100_EMMC_PARTITION&&wm.write_commands==writes);
 assert(i9100_emmc_write(&wm.card,&writer.grant,high-1U,1U,transfer_bytes)==I9100_EMMC_PARTITION&&wm.write_commands==writes);
 struct i9100_emmc_write_grant copy=writer.grant;
 assert(i9100_emmc_write(&wm.card,&copy,high,1U,transfer_bytes)==I9100_EMMC_OWNER&&wm.write_commands==writes);
 assert(!i9100_emmc_writer_synchronize(&writer)&&!writer.grant.dirty&&!wm.flushes);
 assert(!i9100_emmc_writer_close(&writer)&&!wm.card.write_grant);done();
 id=setup_writer(path,1);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_SYSTEM_VOLUME,344064ULL,8));
 assert(!i9100_emmc_writer_write(&writer,2,1,transfer_bytes));assert(!i9100_emmc_writer_close(&writer)&&wm.flushes==1U);done();
 id=setup_writer(path,0);id.cid[0]^=1U;
 assert(i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8)==I9100_EMMC_OWNER&&!wm.write_commands);done();
 id=setup_writer(path,0);id.entries_crc^=1U;
 assert(i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8)==I9100_EMMC_OWNER&&!wm.write_commands);done();
 id=setup_writer(path,0);wm.d.damage_backup=1;
 assert(i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8)==I9100_LAYOUT_CRC&&!wm.write_commands);done();
 id=setup_writer(path,0);
 assert(i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_SYSTEM_VOLUME,73728ULL,8)==I9100_EMMC_PARTITION&&!wm.write_commands);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.corrupt_readback=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_READBACK&&writer.grant.poisoned&&writer.grant.dirty);
 writes=wm.write_commands;assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_READBACK&&wm.write_commands==writes);
 assert(i9100_emmc_writer_close(&writer)==I9100_EMMC_READBACK&&wm.card.write_grant);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.counter_short=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_IO&&writer.grant.poisoned);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.full_stuck=wm.freeze_time=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_TIMEOUT&&!writer.grant.words_submitted&&writer.grant.mutation_possible);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.error_write=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_DATA_CRC&&!writer.grant.words_submitted);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.deny_exclusive=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_OWNER&&!wm.write_commands);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));writer.grant.first_lba=73728ULL;
 assert(i9100_emmc_write(&wm.card,&writer.grant,73728ULL,1,transfer_bytes)==I9100_EMMC_PARTITION&&!wm.write_commands);done();
 id=setup_writer(path,1);memset(wm.card.ext_csd+249,0,4);
 assert(i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8)==I9100_EMMC_UNSUPPORTED&&!wm.write_commands);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));
 assert(i9100_emmc_writer_write(&writer,0,1,writer.readback)==I9100_EMMC_ARGUMENT&&!wm.write_commands);
 assert(!i9100_emmc_writer_close(&writer));done();
 id=setup_writer(path,1);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));
 assert(!i9100_emmc_writer_write(&writer,0,1,transfer_bytes));wm.flush_error=1;
 assert(i9100_emmc_writer_close(&writer)==I9100_EMMC_CARD_ERROR&&writer.grant.poisoned&&writer.grant.dirty&&wm.card.write_grant);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.drop_argument=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_IO&&!wm.write_commands);done();
 id=setup_writer(path,0);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL,8));wm.m.regs[EM_CTRL/4]|=1U<<25;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_UNSUPPORTED&&!wm.write_commands);done();
}
#ifndef I9100_EMMC_WRITER_MAIN
#define I9100_EMMC_WRITER_MAIN main
#endif
int I9100_EMMC_WRITER_MAIN(int argc,char **argv)
{assert(argc==2);tests(argv[1]);puts("PASS actual eMMC writer + lower command whitelist: current historical GPT/PIT/SYNTHETIC backup, CID/window binding, 32-sector >4GiB writes/readback, counters/flush, protected partitions, copied grants and failure-without-retry");return 0;}
