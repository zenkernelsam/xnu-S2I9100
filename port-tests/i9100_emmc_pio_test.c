/* SPDX-License-Identifier: BSD-2-Clause
 * Actual card/writer core; FIFO delivery and time are explicit test facades.
 * No hardware throughput or persistent-media claim is made here. */
#define I9100_EMMC_WRITER_MAIN pio_legacy_writer_main
#include "i9100_emmc_writer_test.c"
static struct {
 uint32_t data_active,progress_since_poll,progress_delays,stall_delays,full_polls,empty_polls;
 uint32_t full_every,last_full,crc_at_word;
 uint64_t advancing_ns;
 int wait_only_clock,delay_error;
} perf;
static uint32_t perf_read(void *cookie,unsigned reg)
{
 assert(cookie==&wm);
 if(reg==EM_RINTSTS)perf.progress_since_poll=0;
 if(reg==EM_DATA)perf.progress_since_poll=1;
 if(reg==EM_RINTSTS&&wm.write_active&&perf.crc_at_word&&wm.write_position>=perf.crc_at_word)
  return (1U<<7);
 if(reg==EM_STATUS&&wm.write_active){
  if(perf.full_polls){perf.full_polls--;return 1U<<3;}
  if(perf.full_every&&wm.write_position&&wm.write_position%perf.full_every==0&&perf.last_full!=wm.write_position){
   perf.last_full=wm.write_position;return 1U<<3;
  }
 }
 if(reg==EM_STATUS&&!wm.write_active&&wm.m.position<wm.m.count&&perf.empty_polls){
  perf.empty_polls--;return 0;
 }
 return wread(cookie,reg);
}
static void perf_write(void *cookie,unsigned reg,uint32_t value)
{
 assert(cookie==&wm);wwrite(cookie,reg,value);
 if(reg==EM_DATA)perf.progress_since_poll=1;
 if(reg==EM_CMD&&!(value&(1U<<21))&&((value&63U)==24U||(value&63U)==25U)){
  perf.data_active=1;perf.progress_since_poll=0;
 }
 if(reg==EM_CMD&&!(value&(1U<<21))&&((value&63U)==17U||(value&63U)==18U)){
  perf.data_active=2;perf.progress_since_poll=0;
 }
 if(reg==EM_CMD&&!(value&(1U<<21))&&((value&63U)==12U||(value&63U)==13U))perf.data_active=0;
}
static int perf_delay(void *cookie,uint32_t usec)
{
 assert(cookie==&wm);
 if(usec==10&&perf.data_active){
  if(perf.progress_since_poll)perf.progress_delays++;else perf.stall_delays++;
  if(perf.delay_error)return -1;
 }
 return wdelay(cookie,usec);
}
static uint64_t perf_now(void *cookie)
{
 assert(cookie==&wm);
 if(perf.data_active&&perf.advancing_ns)wm.m.time+=perf.advancing_ns;
 return perf.wait_only_clock?0:wnow(cookie);
}
static struct i9100_emmc_writer_identity prepare(const char *path)
{
 struct i9100_emmc_writer_identity identity=setup_writer(path,1);memset(&perf,0,sizeof(perf));
 wm.card.io.read32=perf_read;wm.card.io.write32=perf_write;
 wm.card.io.now_ns=perf_now;wm.card.io.delay_us=perf_delay;
 return identity;
}
static void clear_counts(void)
{perf.data_active=perf.progress_since_poll=perf.progress_delays=perf.stall_delays=perf.last_full=0;}
int main(int argc,char **argv)
{
 assert(argc==2);for(unsigned i=0;i<sizeof(transfer_bytes);i++)transfer_bytes[i]=(uint8_t)(i*17U+9U);
 const uint64_t high=3489792ULL+8388608ULL;uint8_t response_bytes[16384];
 struct i9100_emmc_writer_identity id=prepare(argv[1]);
 assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 assert(!i9100_emmc_writer_write(&writer,0,32,transfer_bytes));
 assert(writer.verified_sectors==32&&writer.grant.words_submitted==4096&&!perf.progress_delays&&!perf.stall_delays);
 assert(!i9100_emmc_read(&wm.card,high,32,response_bytes)&&!memcmp(response_bytes,transfer_bytes,16384));
 assert(!perf.progress_delays&&!perf.stall_delays);assert(!i9100_emmc_writer_close(&writer)&&wm.flushes==1);done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.full_polls=3;perf.full_every=64;
 assert(!i9100_emmc_writer_write(&writer,0,32,transfer_bytes)&&writer.verified_sectors==32);
 assert(perf.stall_delays==66&&!perf.progress_delays);clear_counts();perf.full_every=0;perf.empty_polls=4;
 assert(!i9100_emmc_read(&wm.card,high,32,response_bytes)&&!memcmp(response_bytes,transfer_bytes,16384));
 assert(perf.stall_delays==4&&!perf.progress_delays);assert(!i9100_emmc_writer_close(&writer));done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.wait_only_clock=1;assert(!i9100_emmc_writer_write(&writer,0,32,transfer_bytes)&&!perf.progress_delays&&!perf.stall_delays);
 assert(!i9100_emmc_writer_close(&writer));done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.full_polls=UINT32_MAX;perf.wait_only_clock=1;wm.card.io.poll_bound=32;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_TIMEOUT);
 assert(!writer.grant.words_submitted&&writer.grant.poisoned&&writer.grant.dirty&&perf.stall_delays==32);
 uint32_t writes=wm.write_commands;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_TIMEOUT&&wm.write_commands==writes);done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.crc_at_word=7;assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_DATA_CRC);
 assert(writer.grant.words_submitted==7&&writer.grant.poisoned&&!perf.progress_delays);done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.advancing_ns=100000000ULL;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_TIMEOUT);
 assert(writer.grant.words_submitted>0&&writer.grant.words_submitted<128&&writer.grant.poisoned&&!perf.progress_delays);done();

 id=prepare(argv[1]);assert(!i9100_emmc_writer_open(&writer,1,&wm.card,&id,I9100_EMMC_DATA_VOLUME,high,32));clear_counts();
 perf.full_polls=1;perf.delay_error=1;
 assert(i9100_emmc_writer_write(&writer,0,1,transfer_bytes)==I9100_EMMC_IO&&writer.grant.poisoned&&!writer.grant.words_submitted);done();

 puts("PASS actual PIO progress has zero deliberate payload sleeps; transient full/empty backoff, >4GiB 16KiB readback/flush, CRC after7words, progress deadline, frozen-clock bound, delay failure and no retry; no hardware timing claim");return 0;
}
