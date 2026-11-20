/* SPDX-License-Identifier: BSD-2-Clause
 * Actual SDR/core/writer flow, with card registers/FIFO/time facade.
 * Historical primary GPT/PIT + explicit synthetic secondary GPT retained. */
#define I9100_EMMC_WRITER_MAIN sdr_legacy_writer_main
#include "i9100_emmc_writer_test.c"
#include "i9100_emmc_sdr.h"
static struct {
 uint8_t ext[512];unsigned checks,pads,switches,args[12],busy_remaining,slow_reads,fast_reads;
 unsigned fail_check,fail_pads,fail_switch,drop_switch,drop_ctype,drop_div,corrupt_offset,busy_after_switch;
 int busy_forever,freeze_time;
} sm;
static void set_byte(unsigned at,uint8_t value)
{unsigned index=at/4U,shift=(at%4U)*8U;wm.m.words[index]=(wm.m.words[index]&~(255U<<shift))|((uint32_t)value<<shift);}
static uint32_t sread(void *cookie,unsigned off)
{
 assert(cookie==&wm);
 if(off==EM_STATUS&&(sm.busy_forever||sm.busy_remaining))return 1U<<9;
 return wread(cookie,off);
}
static void swrite(void *cookie,unsigned off,uint32_t value)
{
 assert(cookie==&wm);
 if(off==EM_CTYPE&&sm.drop_ctype)return;
 if(off==EM_CLKDIV&&sm.drop_div)return;
 if(off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==6U&&wm.m.regs[EM_CMDARG/4]!=0x03200100U){
  uint32_t arg=wm.m.regs[EM_CMDARG/4];unsigned index=(arg>>16)&255U,data_value=(arg>>8)&255U;
  assert((arg&0xff0000ffU)==0x03000001U&&!(value&((1U<<9)|(1U<<10))));
  assert(index==183U||index==185U||index==187U);assert(sm.switches<12);sm.args[sm.switches++]=arg;
  assert(sm.pads==1&&wm.m.regs[EM_CLKDIV/4]>=125U); /* All configuration at <=400kHz. */
  wm.m.regs[EM_CMD/4]=value&~(1U<<31);wm.m.regs[EM_RINTSTS/4]|=1U<<2;wm.m.commands++;
  if(sm.fail_switch==sm.switches){response(&wm.m,1U<<7);return;}
  if(sm.drop_switch!=sm.switches)sm.ext[index]=(uint8_t)data_value;
  sm.busy_remaining=3;if(sm.busy_after_switch)sm.busy_forever=1;response(&wm.m,(7U<<9));return;
 }
 if(off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==13U&&(sm.busy_remaining||sm.busy_forever)){
  wm.m.regs[EM_CMD/4]=value&~(1U<<31);wm.m.regs[EM_RINTSTS/4]|=1U<<2;wm.m.commands++;
  if(sm.busy_remaining)sm.busy_remaining--;response(&wm.m,7U<<9);return;
 }
 wwrite(cookie,off,value);
 if(off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==8U){
  for(unsigned i=0;i<512;i++)set_byte(i,sm.ext[i]);
  if(sm.switches&&sm.corrupt_offset<512)set_byte(sm.corrupt_offset,sm.ext[sm.corrupt_offset]^1U);
  if(wm.m.regs[EM_CLKDIV/4]>=125U)sm.slow_reads++;else sm.fast_reads++;
  unsigned expected=sm.ext[183]==0U?0U:sm.ext[183]==1U?1U:1U<<16;
  if(wm.m.regs[EM_CTYPE/4]!=expected)wm.m.regs[EM_RINTSTS/4]|=1U<<7;
 }
}
static uint64_t snow(void *cookie){return sm.freeze_time?0:wnow(cookie);}
static int board_check(void *cookie)
{assert(cookie==&sm);return ++sm.checks==sm.fail_check?I9100_EMMC_OWNER:0;}
static int prepare_pads(void *cookie,unsigned width)
{assert(cookie==&sm&&(width==4||width==8));sm.pads++;return sm.fail_pads?I9100_EMMC_READBACK:0;}
static struct i9100_emmc_sdr_plan prepare(const char *source,unsigned width,unsigned hs,unsigned hz)
{
 setup_writer(source,1);memset(&sm,0,sizeof(sm));sm.corrupt_offset=512;
 memcpy(sm.ext,wm.card.ext_csd,512);sm.ext[196]=3U;sm.ext[203]=0x21;sm.ext[202]=0x43;sm.ext[248]=20;
 memcpy(wm.card.ext_csd,sm.ext,512);wm.card.io.read32=sread;wm.card.io.write32=swrite;wm.card.io.now_ns=snow;
 struct i9100_emmc_sdr_plan p={1,width,hs,hz,100000000U,3300,{0}};memcpy(p.cid,wm.card.cid,16);return p;
}
static void failure_no_retry(struct i9100_emmc_sdr_session *s,const struct i9100_emmc_sdr_plan *p,const struct i9100_emmc_sdr_board_ops *b)
{
 assert(s->poisoned&&!s->verified&&!wm.card.ready&&wm.card.sdr==s);unsigned commands=wm.m.commands;
 uint8_t bytes[512];assert(i9100_emmc_read(&wm.card,344064U,1,bytes)==I9100_EMMC_NO_DEVICE);
 assert(i9100_emmc_sdr_apply(1,&wm.card,s,p,b)==I9100_EMMC_ARGUMENT&&wm.m.commands==commands);
 assert(i9100_emmc_probe(&wm.card)==I9100_EMMC_BUSY&&wm.m.commands==commands);
}
int main(int argc,char **argv)
{
 assert(argc==2);const struct i9100_emmc_sdr_board_ops board={&sm,board_check,prepare_pads};
 struct i9100_emmc_sdr_session session={0};struct i9100_emmc_sdr_plan plan=prepare(argv[1],4,0,20000000);
 unsigned commands=wm.m.commands;
 assert(i9100_emmc_sdr_apply(0,&wm.card,&session,&plan,&board)==I9100_EMMC_UNSUPPORTED&&!session.self&&wm.m.commands==commands);done();
 for(unsigned width=4;width<=8;width+=4)for(unsigned hs=0;hs<=1;hs++){
  plan=prepare(argv[1],width,hs,hs?50000000U:20000000U);memset(&session,0,sizeof(session));
  assert(!i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board)&&session.verified&&!session.poisoned);
  assert(wm.card.ext_csd[183]==(width==4?1:2)&&wm.card.ext_csd[185]==hs&&session.power_class==(hs?(width==4?3:4):(width==4?1:2)));
  assert(sm.switches==(hs?3U:2U)&&sm.slow_reads==1&&sm.fast_reads==1);
  assert(sm.args[sm.switches-1]==(width==4?0x03b70101U:0x03b70201U));
  uint8_t bytes[16384];assert(!i9100_emmc_read(&wm.card,344064U,32,bytes));
  struct i9100_emmc_writer_identity id={0};memcpy(id.cid,wm.card.cid,16);id.sectors=wm.d.sectors;
  struct i9100_layout_io io={&wm.d,disk_read,wm.d.sectors};struct i9100_emmc_layout layout;
  assert(!i9100_emmc_read_layout(&io,&layout)&&!i9100_emmc_check_pit(&io,&layout));
  memcpy(id.disk_guid,layout.disk_guid,16);id.primary_crc=layout.primary_crc;id.backup_crc=layout.backup_crc;id.entries_crc=layout.entries_crc;
  memcpy(id.system_guid,i9100_emmc_find_partition(&layout,"FACTORYFS")->unique_guid,16);
  memcpy(id.data_guid,i9100_emmc_find_partition(&layout,"DATAFS")->unique_guid,16);
  assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,3489792ULL+8388608ULL,32));
  for(unsigned i=0;i<16384;i++)bytes[i]=(uint8_t)(i*19U);
  assert(!i9100_emmc_writer_write(&writer,0,32,bytes)&&writer.verified_sectors==32);
  assert(i9100_emmc_sdr_restore(&wm.card,&session)==I9100_EMMC_BUSY);
  assert(!i9100_emmc_writer_close(&writer)&&wm.flushes==1U);
  assert(!i9100_emmc_sdr_restore(&wm.card,&session)&&!wm.card.sdr&&session.phase==I9100_SDR_RESTORED);
  assert(wm.card.ext_csd[183]==0&&wm.card.ext_csd[185]==0&&wm.card.ext_csd[187]==0&&!i9100_emmc_transport_check(&wm.card));done();
 }
 for(unsigned invalid=0;invalid<8;invalid++){
  plan=prepare(argv[1],4,0,20000000);memset(&session,0,sizeof(session));
  if(invalid==0)plan.cid[0]^=1;else if(invalid==1)plan.board_reviewed=0;else if(invalid==2)plan.width=3;
  else if(invalid==3)plan.high_speed=2;else if(invalid==4)plan.vcc_mv=1800;
  else if(invalid==5)plan.target_hz=52000000;else if(invalid==6)plan.ciu_hz=200000000;else sm.fail_check=1;
  commands=wm.m.commands;assert(i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board)&&!session.attempted&&wm.m.commands==commands);done();
 }
 for(unsigned offset=160;offset<253;offset++){
  /* Independently vary each established immutable/read-only prerequisite,
   * including the vendor's first-partition-support negation bug. */
  if(offset!=160&&offset!=179&&offset!=192&&offset!=194&&offset!=196&&offset!=203&&offset!=212&&offset!=213&&offset!=214&&offset!=215&&offset!=248&&offset!=249)continue;
  plan=prepare(argv[1],8,1,50000000);memset(&session,0,sizeof(session));sm.corrupt_offset=offset;
  assert(i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board)==I9100_EMMC_READBACK);
  failure_no_retry(&session,&plan,&board);done();
 }
 plan=prepare(argv[1],4,1,50000000);wm.card.ext_csd[196]=1;memset(&session,0,sizeof(session));commands=wm.m.commands;
 assert(i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board)==I9100_EMMC_CLOCK&&!session.attempted&&wm.m.commands==commands);done();
 plan=prepare(argv[1],8,1,50000000);memset(&session,0,sizeof(session));sm.busy_after_switch=1;sm.freeze_time=1;wm.card.io.poll_bound=10;
 assert(i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board)==I9100_EMMC_TIMEOUT&&session.switches==1);
 failure_no_retry(&session,&plan,&board);done();
 plan=prepare(argv[1],4,0,20000000);memset(&session,0,sizeof(session));assert(!i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board));
 struct i9100_emmc_sdr_session copy=session;wm.card.sdr=&copy;
 assert(i9100_emmc_transport_check(&wm.card)==I9100_EMMC_OWNER);wm.card.sdr=&session;
 sm.busy_forever=1;commands=wm.m.commands;
 assert(i9100_emmc_sdr_restore(&wm.card,&session)==I9100_EMMC_BUSY&&wm.m.commands==commands&&session.verified);
 sm.busy_forever=0;
 assert(!i9100_emmc_sdr_restore(&wm.card,&session)&&wm.card.clock_div==session.original_div&&wm.card.clock_hz==session.original_hz);done();
 for(unsigned fault=0;fault<7;fault++){
  plan=prepare(argv[1],8,1,50000000);memset(&session,0,sizeof(session));
  if(fault==0)sm.fail_pads=1;else if(fault==1)sm.fail_switch=1;else if(fault==2)sm.drop_switch=3;
  else if(fault==3)sm.drop_ctype=1;else if(fault==4)sm.corrupt_offset=196;
  else if(fault==5)sm.drop_div=1;else sm.fail_check=4;
  assert(i9100_emmc_sdr_apply(1,&wm.card,&session,&plan,&board));failure_no_retry(&session,&plan,&board);done();
 }
 puts("PASS actual4/8bit legacy/HS50 SDR + powerclass/preciseCMD6/slow-fastEXT_CSD/>4GiBwriter/readback/cacheflush/explicitlegacyrestore/defaultOFF/CID/board/pads/clock/CRC/one-shot failures; card/MMIO/time facade, no hardware");return 0;
}
