/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_pl330.h"
#include "i9100_i2s.h"
#include "i9100_mc1n2.h"
#include "i9100_pcm_stream.h"
static uint8_t source[262144] __attribute__((aligned(64)));
static uint8_t destination[262144] __attribute__((aligned(64)));
static uint8_t program[4096] __attribute__((aligned(64)));
static uint32_t transmitted[131072];
struct model {
 uint32_t regs[1024],locked,reads,writes,fail_write,go_busy,kill_stuck,freeze_time;
 uint32_t code_pa,src_pa,dst_pa,program_size,fifo_words,manager_status;
 uint64_t ns;
 uint32_t i2s[4],dma_busy,dma_stop_fail,stopped_requests,reset_missing;
 uint8_t codec[5][256],bank_address[5],wire_register;
 unsigned bus_locked,transactions,fail_transaction,partial,busy_codec;
 unsigned stall_ready,sticky_codec_error,unsafe_amplifier_writes,codec_delays,drop_dac_mute;
 uint64_t vref_started;
 unsigned stream_locked,i2s_locked,i2s_writes,i2s_fail_write,reject_quiesce,sticky_event;
 unsigned pads_calls,pads_fail,clock_lease_invalid,clock_consumer_busy,clock_lease_calls,clock_fail_lease_call;
 unsigned aa_unlocked,aa_wrong_pll_writes,aa_dpb_sequence_errors,aa_spare_pll_writes,aa_unlock_errors;
 unsigned require_quiesce,kills,mute_calls,mute_fail;
 struct i9100_pl330 *stream_dma;
};
static struct model m;
struct buffer_cookie {struct i9100_dma_buffer b;unsigned locked,fail_sync,sync_count,released,lock_calls,fail_lock;};
static struct buffer_cookie code_buffer,src_buffer,dst_buffer;
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static uint8_t *memory(uint32_t pa,unsigned bytes)
{
 if(pa>=m.src_pa&&(uint64_t)pa+bytes<=(uint64_t)m.src_pa+sizeof(source))return source+(pa-m.src_pa);
 if(pa>=m.dst_pa&&(uint64_t)pa+bytes<=(uint64_t)m.dst_pa+sizeof(destination))return destination+(pa-m.dst_pa);
 assert(!"VM out-of-range memory address");return 0;
}
static void execute(unsigned used)
{
 /* Generic instruction interpreter, independent of encoder loop choices. */
 unsigned pc=0,loops[2]={0},steps=0,loaded=0;uint32_t sar=0,dar=0,ccr=0,word=0;
 while(pc<used&&++steps<1000000U){
  uint8_t op=program[pc];
  if(op==0xbc){assert(pc+6<=used);uint32_t v=le32(program+pc+2);
   switch(program[pc+1]){case 0:sar=v;break;case 1:ccr=v;break;case 2:dar=v;break;default:assert(0);}pc+=6;
  }else if(op==0x20||op==0x22){assert(pc+2<=used);loops[(op>>1)&1U]=program[pc+1]+1U;pc+=2;}
  else if(op==0x38||op==0x3c){assert(pc+2<=used);unsigned loop=(op>>2)&1U,jump=program[pc+1];assert(loops[loop]);
   if(--loops[loop]){assert(jump&&jump<=pc);pc-=jump;}else pc+=2;
  }else if(op==4||op==0x27){
   assert(!loaded);assert(((ccr>>1)&7U)==2U&&((ccr>>4)&15U)==0U);
   if(op==0x27){assert(sar==0x03830014U&&program[pc+1]==11U<<3);word=0xa5000000U|m.fifo_words++;pc+=2;}
   else{word=le32(memory(sar,4));pc++;}if(ccr&1U)sar+=4;loaded=1;
  }else if(op==8||op==0x2b){
   assert(loaded);assert(((ccr>>15)&7U)==2U&&((ccr>>18)&15U)==0U);
   if(op==0x2b){assert(dar==0x03830010U&&program[pc+1]==12U<<3);
    assert(m.fifo_words<sizeof(transmitted)/sizeof(transmitted[0]));transmitted[m.fifo_words++]=word;pc+=2;}
   else{uint8_t *out=memory(dar,4);for(unsigned n=0;n<4;n++)out[n]=(uint8_t)(word>>(n*8));pc++;}
   if(ccr&(1U<<14))dar+=4;loaded=0;
  }else if(op==0x32||op==0x35){assert(pc+2<=used);unsigned request=program[pc+1]>>3;assert(request==11U||request==12U);pc+=2;}
  else if(op==0x13){assert(!loaded);pc++;}
  else if(op==0x34){assert(pc+2<=used);m.regs[0x24/4]|=1U<<(program[pc+1]>>3);pc+=2;}
  else if(op==0){assert(!loaded&&!loops[0]&&!loops[1]);assert(pc+1==used);m.regs[0x100/4]=0;return;}
  else assert(!"Unsupported instruction in generated finite program");
 }
 assert(!"Program ended without DMAEND or exceeded bound");
}
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;if(!p->freeze_time)p->ns+=us*1000ULL;return 0;}
static uint32_t read(void *c,unsigned off){struct model *p=c;assert(off<4096U);p->reads++;return p->regs[off/4];}
static void write(void *c,unsigned off,uint32_t value)
{
 struct model *p=c;assert(p->locked&&off<4096);p->writes++;if(p->fail_write==p->writes)return;
 if(off==0x2c){if(!p->sticky_event)p->regs[0x24/4]&=~value;return;}
 p->regs[off/4]=value;
 if(off==0xd04){uint32_t instruction=p->regs[0xd08/4];unsigned op=(instruction>>16)&255U;
  if(op==0xa2){assert(((instruction>>24)&7U)==0);assert(p->regs[0xd0c/4]==p->code_pa);p->regs[0x100/4]=1;
   if(p->go_busy)p->regs[0xd00/4]=1;}
  else if(op==1){assert(instruction&1U);if(p->require_quiesce)assert(!(p->i2s[0]&7U));
   p->kills++;if(!p->kill_stuck){p->regs[0x100/4]=0;p->regs[0x34/4]=0;}}
  else assert(0);
 }
}
static int block_lock(void *c){struct buffer_cookie *b=c;b->lock_calls++;if(b->locked||b->lock_calls==b->fail_lock)return -1;b->locked=1;return 0;}
static void block_unlock(void *c){struct buffer_cookie *b=c;assert(b->locked);b->locked=0;}
static int sync(void *c,uintptr_t cpu,uint32_t pa,uint32_t bytes,unsigned action)
{struct buffer_cookie *b=c;assert(b->locked&&cpu==b->b.cpu&&pa==b->b.physical&&bytes==b->b.bytes);assert(action>=1&&action<=4);return ++b->sync_count==b->fail_sync?-1:0;}
static int idle(void *c){(void)c;return m.regs[0x100/4]||m.regs[0xd00/4]?-1:0;}
static void free_buffer(void *c,uintptr_t cpu,uint32_t pa,uint32_t bytes){struct buffer_cookie *b=c;(void)cpu;(void)pa;(void)bytes;assert(!b->locked);b->released++;}
static void bind(struct buffer_cookie *b,void *cpu,uint32_t pa,unsigned bytes)
{memset(b,0,sizeof(*b));struct i9100_dma_io io={b,block_lock,block_unlock,sync,idle,free_buffer};assert(!i9100_dma_bind(&b->b,&io,(uintptr_t)cpu,pa,bytes));assert(!i9100_dma_enable(&b->b,1));}
static void init_pl(struct i9100_pl330 *p)
{
 memset(&m,0,sizeof(m));memset(p,0,sizeof(*p));m.code_pa=0x40004000;m.src_pa=0x40010000;m.dst_pa=0x40200000;
 const uint32_t ids[2]={0x41330,0xb105f00d};
 for(unsigned which=0;which<2;which++)for(unsigned n=0;n<4;n++)m.regs[((which?0xff0:0xfe0)+n*4)/4]=(ids[which]>>(n*8))&255U;
 m.regs[0xe00/4]=1U|(7U<<4)|(31U<<12)|(31U<<17);
 struct i9100_pl330_io io={&m,read,write,lock,unlock,now,delay};assert(!i9100_pl330_init(p,&io,0,3));
 assert(!m.reads&&!m.writes);assert(!i9100_pl330_enable(p,1));
 bind(&code_buffer,program,m.code_pa,sizeof(program));bind(&src_buffer,source,m.src_pa,sizeof(source));bind(&dst_buffer,destination,m.dst_pa,sizeof(destination));
}
static struct i9100_pl330_job job(unsigned mode,unsigned bytes)
{
 struct i9100_pl330_job j;memset(&j,0,sizeof(j));j.mode=mode;j.code=&code_buffer.b;
 j.source=mode==I9100_PL330_I2S_RX?0:&src_buffer.b;j.destination=mode==I9100_PL330_I2S_TX?0:&dst_buffer.b;
 j.bytes=bytes;j.timeout_ms=50;return j;
}
static void pl330(void)
{
 const unsigned sizes[]={4,100,1024,1028,65536,262144};
 for(unsigned mode=1;mode<=3;mode++)for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++){
  struct i9100_pl330 p;init_pl(&p);struct i9100_pl330_job j=job(mode,sizes[n]);
  for(unsigned b=0;b<sizeof(source);b++)source[b]=(uint8_t)(b*37U);memset(destination,0,sizeof(destination));
  assert(!i9100_pl330_submit(&p,&j));assert(p.active&&code_buffer.b.state==I9100_DMA_DEVICE);
  assert(i9100_dma_release(&code_buffer.b)==I9100_DMA_OWNER);uint32_t completed=99;
  assert(i9100_pl330_poll(&p,&completed)==I9100_PL330_BUSY&&!completed);
  execute(p.program_bytes);assert(!i9100_pl330_poll(&p,&completed)&&completed==sizes[n]);
  assert(code_buffer.b.state==I9100_DMA_CPU&&!p.active);
  if(mode==1)assert(!memcmp(source,destination,sizes[n]));else assert(m.fifo_words==sizes[n]/4);
  assert(!i9100_pl330_enable(&p,0));
 }
 struct i9100_pl330 p;init_pl(&p);struct i9100_pl330_job j=job(1,1024);
 j.destination=&src_buffer.b;assert(i9100_pl330_submit(&p,&j)==I9100_PL330_ARGUMENT);j=job(1,1024);
 assert(!i9100_pl330_submit(&p,&j));m.ns=50000001;m.kill_stuck=1;m.freeze_time=1;
 uint32_t completed=99;assert(i9100_pl330_poll(&p,&completed)==I9100_PL330_TIMEOUT&&!completed);
 assert(p.active&&p.fault&&code_buffer.b.state==I9100_DMA_QUARANTINED);
 assert(i9100_dma_release(&src_buffer.b)==I9100_DMA_OWNER);m.kill_stuck=0;assert(!i9100_pl330_stop(&p));
 assert(!p.active&&!p.fault&&src_buffer.b.state==I9100_DMA_CPU);
 init_pl(&p);j=job(1,1024);m.go_busy=1;
 assert(i9100_pl330_submit(&p,&j)==I9100_PL330_TIMEOUT&&p.fault&&p.active);
 assert(code_buffer.b.state==I9100_DMA_QUARANTINED);m.regs[0xd00/4]=0;m.go_busy=0;assert(!i9100_pl330_stop(&p));
 init_pl(&p);j=job(1,1024);assert(!i9100_pl330_submit(&p,&j));m.regs[0x34/4]=1;m.regs[0x40/4]=0x20;
 assert(i9100_pl330_poll(&p,&completed)==I9100_PL330_FAULT&&p.error_status==0x20&&!completed);
 init_pl(&p);j=job(1,1024);src_buffer.fail_sync=1;assert(i9100_pl330_submit(&p,&j)==I9100_PL330_IO);
 assert(!p.active&&code_buffer.b.state==I9100_DMA_CPU&&src_buffer.b.state==I9100_DMA_CPU);
 unsigned used=99;assert(i9100_pl330_encode(2,m.src_pa,0x13800000,4,3,program,sizeof(program),&used)==I9100_PL330_ARGUMENT&&!used);
}
static uint32_t i2s_read(void *c,unsigned off){struct model *p=c;assert((p->locked||p->i2s_locked)&&off<=12);return p->i2s[off/4];}
static void i2s_write(void *c,unsigned off,uint32_t value)
{struct model *p=c;assert((p->locked||p->i2s_locked)&&off<=12);
 if(++p->i2s_writes==p->i2s_fail_write||(off==0&&!(value&7U)&&p->reject_quiesce))return;
 if(off==0&&!(value&0x80000000U)&&(value&7U))p->reset_missing=1;
 if(off==0&&(value&7U)&&p->require_quiesce)assert(p->regs[0x100/4]);
 p->i2s[off/4]=value;if(off==0&&!(value&7U))p->stopped_requests=1;}
static int dma_idle(void *c){return ((struct model *)c)->dma_busy?-1:0;}
static int dma_stop(void *c){struct model *p=c;assert(p->stopped_requests);if(p->dma_stop_fail)return -1;p->dma_busy=0;return 0;}
static int pads_idle(void *c)
{struct model *p=c;assert(!(p->i2s[0]&7U)&&!p->dma_busy);if(p->stream_dma)assert(!i9100_pl330_idle(p->stream_dma));
 p->pads_calls++;return p->pads_fail?I9100_I2S_IO:0;}
static void i2s(void)
{
 memset(&m,0,sizeof(m));struct i9100_i2s s={0};struct i9100_i2s_io io={&m,i2s_read,i2s_write,lock,unlock,dma_idle,dma_stop,pads_idle};
 assert(!i9100_i2s_init(&s,&io));assert(!i9100_i2s_enable(&s,1));
 m.i2s[1]=0x20000000;assert(i9100_i2s_configure(&s,48000,1536000,0)==I9100_I2S_CLOCK);
 assert(!i9100_i2s_configure(&s,48000,1536000,1));
 assert(m.i2s[1]==0xe0001c00U);assert(i9100_i2s_start(&s,0,0)==I9100_I2S_ARGUMENT);
 assert(!i9100_i2s_start(&s,0,1));assert(!m.reset_missing&&m.i2s[0]==0x8000002dU);
 struct i9100_i2s_snapshot snapshot;m.i2s[0]|=1U<<17;m.i2s[2]=(5U<<8)|3U;unsigned writes=m.i2s_writes;
 assert(!i9100_i2s_snapshot(&s,&snapshot)&&snapshot.errors==(1U<<17)&&snapshot.tx_count_encoded==5&&snapshot.rx_count_encoded==3);
 assert(m.i2s_writes==writes&&(m.i2s[0]&(1U<<17))); /* a snapshot never clears/ACKs the hardware status */
 m.i2s[0]&=~(1U<<17);
 m.dma_busy=m.dma_stop_fail=1;assert(i9100_i2s_stop(&s)==I9100_I2S_IO&&s.fault&&s.prepared);
 assert(!(m.i2s[0]&7U)&&!m.pads_calls);m.dma_stop_fail=0;m.pads_fail=1;
 assert(i9100_i2s_stop(&s)==I9100_I2S_IO&&s.prepared&&s.fault&&m.i2s[1]==0xe0001c00U);
 m.pads_fail=0;assert(!i9100_i2s_stop(&s));assert(!s.prepared&&m.i2s[1]==0x20000000U);
}
static int bus_claim(void *c){struct model *p=c;if(p->bus_locked)return -1;p->bus_locked=1;return 0;}
static void bus_release(void *c){struct model *p=c;assert(p->bus_locked);p->bus_locked=0;}
static unsigned bank(unsigned port){switch(port){case 1:return 1;case 12:return 2;case 14:return 3;case 16:return 4;default:return 0;}}
static void codec_write(unsigned wire,uint8_t value)
{
 assert(!(wire&1U));unsigned reg=wire>>1,index=bank(reg);
 if(reg==0U&&value==1U){
  uint8_t id=m.codec[0][8];memset(m.codec,0,sizeof(m.codec));m.codec[0][8]=id;
  m.codec[1][0]=0x10;m.codec[1][1]=id==0x78U?7U:6U;m.codec[1][3]=0x1f;m.codec[3][15]=1;
  m.codec[1][14]=id==0x78U?1U:0U;m.aa_unlocked=0;
  m.codec[2][2]=15;m.codec[2][3]=0xff;m.codec[2][4]=0xff;m.codec[3][78]=2;
 }
 if(reg==60U&&value==0xd9U&&m.codec[0][8]==0x78U){
  if(m.bank_address[2]!=2U)m.aa_unlock_errors++;
  else m.aa_unlocked=1;
 }
 if(index)m.bank_address[index]=value;
 else if((index=bank(reg-1U))){
  unsigned address=m.bank_address[index];uint8_t old=m.codec[index][address];
  if(m.codec[0][8]==0x78U){
   if(index==3U&&(address==1U||address==4U||(address>=15U&&address<=19U)))m.aa_wrong_pll_writes++;
   if(index==1U&&(address==17U||address==18U))m.aa_spare_pll_writes++;
   if(index==2U&&address==2U&&!m.aa_unlocked)return;
   if(index==1U&&address==3U&&!(old&1U)&&(value&1U)&&((old^value)&0x1eU))m.aa_dpb_sequence_errors++;
  }
  if(index==3U&&address==78U&&(value&2U)&&m.drop_dac_mute)return;
  if(index==2U&&address==2U&&(old&1U)&&!(value&1U))m.vref_started=m.ns;
  if(index==2U&&address==3U&&(value&0x3fU)!=0x3fU)m.unsafe_amplifier_writes++;
  m.codec[index][address]=value;
 }
 else m.codec[0][reg]=value;
}
static uint8_t codec_read(unsigned wire)
{
 assert(!(wire&1U));unsigned reg=wire>>1,index=bank(reg-1U);
 if(index){if(index==2U&&(m.bank_address[index]==12||m.bank_address[index]==13)&&m.busy_codec)return 0x18;
  if(index==2U&&m.bank_address[index]==47U){
   if(m.stall_ready)return 0;
   uint8_t ready=0,power=m.codec[2][2];
   if(!(power&8U))ready|=0x80U;
   if(!(power&1U)&&m.ns>=m.vref_started+15000000ULL)ready|=0x40U;
   return ready;
  }
  return m.codec[index][m.bank_address[index]];}
 return m.codec[0][reg];
}
static int codec_transfer(void *c,const struct i9100_i2c_message *msgs,unsigned count,unsigned *done)
{
 struct model *p=c;assert(p->bus_locked&&msgs[0].address==0x3a);*done=0;p->transactions++;
 if(count==1){assert(!msgs[0].read&&!(msgs[0].length&1U));for(unsigned n=0;n<msgs[0].length;n+=2)codec_write(msgs[0].bytes[n],msgs[0].bytes[n+1]);}
 else{assert(count==2&&msgs[0].length==1&&msgs[1].read&&msgs[1].length==1);msgs[1].bytes[0]=codec_read(msgs[0].bytes[0]);}
 if(p->transactions==p->fail_transaction||p->sticky_codec_error)return -1;
 *done=p->partial?count-1:count;return 0;
}
static struct i9100_peripheral_i2c codec_io={&m,codec_transfer,bus_claim,bus_release,now,delay};
static void codec_init(struct i9100_mc1n2 *c)
{
 memset(c,0,sizeof(*c));memset(&m,0,sizeof(m));m.codec[0][8]=0x79;
 m.codec[2][35]=m.codec[2][36]=m.codec[2][37]=m.codec[2][38]=31;m.codec[2][77]=0x83;
 assert(!i9100_mc1n2_init(c,&codec_io));assert(!m.transactions);
}
static void codec(void)
{
 struct i9100_mc1n2 c;codec_init(&c);assert(i9100_mc1n2_mute(&c)==I9100_MC_DISABLED&&!m.transactions);
 assert(!i9100_mc1n2_enable(&c,1));assert(c.muted&&(m.codec[2][3]&0x3fU)==0x3fU&&(m.codec[3][78]&2U));
 assert(!i9100_mc1n2_stage_gain(&c,I9100_MC_SPEAKER,-30000));assert(c.actual_gain_mdb==-36000&&c.analog_gain==1);
 assert((m.codec[2][37]&31U)==1&&m.codec[2][38]==1&&(m.codec[2][77]&3U)==0&&(m.codec[2][3]&0x3fU)==0x3fU);
 assert(i9100_mc1n2_stage_gain(&c,I9100_MC_SPEAKER,-29000)==I9100_MC_ARGUMENT);
 uint8_t code;int32_t gain;
 for(int32_t requested=-96000;requested<=-30000;requested+=10){assert(!i9100_mc1n2_safe_gain(requested,&code,&gain));assert(code<=1&&gain<=requested);}
 uint8_t sentinel=0xab;m.partial=1;assert(i9100_mc1n2_read(&c,I9100_MC_ANALOG,77,&sentinel)==I9100_MC_IO&&sentinel==0xab);m.partial=0;
 assert(!i9100_mc1n2_enable(&c,0));
 for(unsigned point=1;point<65;point++){
  codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));m.fail_transaction=m.transactions+point;
  int error=i9100_mc1n2_stage_gain(&c,I9100_MC_HEADPHONES,-30000);
  if(error){assert(c.fault&&!c.gain_armed);assert((m.codec[2][3]&0x3fU)==0x3fU);
   for(unsigned n=35;n<=38;n++)assert(!(m.codec[2][n]&31U));}
  else assert(c.analog_gain<=1);
 }
 codec_init(&c);m.codec[0][8]=0x80;assert(i9100_mc1n2_enable(&c,1)==I9100_MC_ID&&m.transactions==1&&!c.enabled);
 codec_init(&c);m.busy_codec=1;m.freeze_time=1;assert(i9100_mc1n2_enable(&c,1)==I9100_MC_TIMEOUT&&c.fault);
 m.busy_codec=0;assert(!i9100_mc1n2_mute(&c));
 codec_init(&c);m.codec[3][78]=1;m.drop_dac_mute=1;
 assert(i9100_mc1n2_enable(&c,1)==I9100_MC_IO&&c.fault&&!c.muted);
 m.drop_dac_mute=0;assert(!i9100_mc1n2_mute(&c)&&c.muted&&(m.codec[3][78]&2U));
}
static void codec_cold(void)
{
 struct i9100_mc1n2 c;
 for(unsigned rate=44100;rate<=48000;rate+=3900){
  codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));unsigned before=m.transactions;
  assert(i9100_mc1n2_cold_init(&c,20000000,rate,1)==I9100_MC_ARGUMENT&&m.transactions==before);
  assert(i9100_mc1n2_cold_init(&c,24000000,rate,0)==I9100_MC_ARGUMENT&&m.transactions==before);
  assert(!i9100_mc1n2_cold_init(&c,24000000,rate,1));
  assert(c.cold_stage==5&&c.cold_owned&&c.muted&&c.sample_rate==rate&&!c.fault);
  assert(m.ns>=18000000ULL&&m.codec[3][16]==28&&m.codec[3][17]==86&&m.codec[3][18]==28&&m.codec[3][19]==86);
  assert(m.codec[3][15]==0&&m.codec[3][79]==0&&m.codec[2][77]==0&&m.codec[2][2]==6);
  assert(m.codec[4][130]==0&&m.codec[4][135]==(rate==48000?0xa0:0xa1)&&m.codec[4][142]==0x44&&m.codec[4][143]==0x10);
  assert(m.codec[4][137]==0&&m.codec[1][12]==0xae&&m.codec[3][78]==2&&!m.unsafe_amplifier_writes);
  assert((m.codec[2][3]&0x3f)==0x3f&&m.codec[2][44]==0x30);
  assert(!i9100_mc1n2_cold_stop(&c)&&!c.cold_owned&&!c.cold_stage&&m.codec[2][2]==15&&m.codec[3][15]==1);
 }
 codec_init(&c);m.codec[0][8]=0x80;assert(i9100_mc1n2_enable(&c,1)==I9100_MC_ID);unsigned before=m.transactions;
 codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));before=m.transactions;
 assert(!i9100_mc1n2_cold_init(&c,24000000,48000,1));unsigned count=m.transactions-before;
 for(unsigned point=1;point<=count;point++){
  codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));m.fail_transaction=m.transactions+point;
  int e=i9100_mc1n2_cold_init(&c,24000000,48000,1);
  assert(e&&c.cold_stage!=5&&!c.cold_owned&&!m.unsafe_amplifier_writes);
  if(c.failure_stage){assert(c.fault&&(m.codec[2][2]&15)==15&&(m.codec[3][15]&1)==1&&(m.codec[2][3]&0x3f)==0x3f);}
 }
 codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));m.stall_ready=m.freeze_time=1;
 assert(i9100_mc1n2_cold_init(&c,24000000,48000,1)==I9100_MC_TIMEOUT&&c.fault&&!c.cold_owned);
 codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));assert(!i9100_mc1n2_cold_init(&c,24000000,48000,1));
 m.sticky_codec_error=1;assert(i9100_mc1n2_cold_stop(&c)&&c.cold_owned&&c.fault);
 m.sticky_codec_error=0;assert(!i9100_mc1n2_cold_stop(&c)&&!c.cold_owned);
}
static int16_t pcm_in[65536],pcm_out[65536];
static int clock_lease(void *c)
{struct model *p=c;assert(p->bus_locked);p->clock_lease_calls++;
 return p->clock_lease_invalid||p->clock_lease_calls==p->clock_fail_lease_call?-1:0;}
static int clock_idle(void *c){struct model *p=c;assert(p->bus_locked);return p->clock_consumer_busy?-1:0;}
static struct i9100_mc1n2_clock_guard clock_guard={&m,clock_lease,clock_idle};
static void codec_aa(void)
{
 struct i9100_mc1n2 c;
 for(unsigned rate=44100;rate<=48000;rate+=3900){
  codec_init(&c);m.codec[0][8]=0x78;assert(!i9100_mc1n2_enable(&c,1));
  assert(!i9100_mc1n2_cold_init(&c,24000000,rate,1)&&c.cold_owned&&c.cold_stage==5&&c.id==0x78U);
  assert(m.aa_unlocked&&!m.aa_wrong_pll_writes&&m.codec[1][14]==0&&m.codec[1][15]==28&&m.codec[1][16]==86);
  assert(!m.aa_spare_pll_writes&&!m.aa_unlock_errors&&m.codec[1][1]==0&&m.codec[1][3]==0x1c);
  assert(m.codec[1][12]==0xae&&m.codec[3][78]==2&&(m.codec[2][3]&0x3f)==0x3f);
  assert(!i9100_mc1n2_clock_start(&c,&clock_guard)&&m.codec[4][137]==0x4c&&m.codec[1][12]==0x5e);
  assert(!i9100_mc1n2_clock_stop(&c));assert(!i9100_mc1n2_cold_stop(&c));
  assert(!c.cold_owned&&m.codec[1][14]==1&&m.codec[1][1]==7&&m.codec[1][3]==0x1f&&!m.aa_dpb_sequence_errors);
  assert(!m.aa_wrong_pll_writes&&!m.aa_spare_pll_writes&&!m.aa_unlock_errors&&!m.unsafe_amplifier_writes);
 }
 codec_init(&c);m.codec[0][8]=0x78;assert(!i9100_mc1n2_enable(&c,1));unsigned before=m.transactions;
 assert(!i9100_mc1n2_cold_init(&c,24000000,48000,1));unsigned count=m.transactions-before;
 for(unsigned point=1;point<=count;point++){
  codec_init(&c);m.codec[0][8]=0x78;assert(!i9100_mc1n2_enable(&c,1));m.fail_transaction=m.transactions+point;
  assert(i9100_mc1n2_cold_init(&c,24000000,48000,1)&&!c.cold_owned&&c.cold_stage!=5&&!m.aa_wrong_pll_writes);
  assert(!m.aa_dpb_sequence_errors&&!m.aa_spare_pll_writes&&!m.aa_unlock_errors&&!m.unsafe_amplifier_writes);
 }
 codec_init(&c);m.codec[0][8]=0x78;assert(!i9100_mc1n2_enable(&c,1));assert(!i9100_mc1n2_cold_init(&c,24000000,48000,1));
 m.sticky_codec_error=1;assert(i9100_mc1n2_cold_stop(&c)&&c.cold_owned&&c.fault);
 m.sticky_codec_error=0;assert(!i9100_mc1n2_cold_stop(&c)&&m.codec[1][14]==1&&m.codec[1][1]==7);
 codec_init(&c);assert(!i9100_mc1n2_enable(&c,1));m.codec[0][8]=0x78;before=m.transactions;
 assert(i9100_mc1n2_cold_init(&c,24000000,48000,1)==I9100_MC_ID&&m.transactions==before+1U);
 assert(c.id==0x79U&&!c.cold_owned);
}
static void init_clock(struct i9100_mc1n2 *c,unsigned rate)
{codec_init(c);assert(!i9100_mc1n2_enable(c,1));assert(!i9100_mc1n2_cold_init(c,24000000,rate,1));}
static void codec_clock(void)
{
 struct i9100_mc1n2 c;
 for(unsigned rate=44100;rate<=48000;rate+=3900){
  init_clock(&c,rate);unsigned transactions=m.transactions;m.clock_consumer_busy=1;
  assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_BUSY&&m.transactions==transactions&&!c.clock_owned);
  m.clock_consumer_busy=0;m.clock_lease_invalid=1;
  assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_NOT_READY&&m.transactions==transactions&&!c.clock_owned);
  m.clock_lease_invalid=0;assert(!i9100_mc1n2_clock_start(&c,&clock_guard));
  assert(c.clock_owned&&c.clock_active&&c.clock_stage==4&&c.cold_owned&&c.muted);
  assert(m.codec[4][137]==0x4c&&m.codec[1][12]==0x5e&&m.codec[4][135]==(rate==48000?0x20:0x21));
  assert(!m.unsafe_amplifier_writes&&m.codec[3][78]==2&&(m.codec[2][3]&0x3f)==0x3f);
  transactions=m.transactions;assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_BUSY&&m.transactions==transactions);
  m.clock_consumer_busy=1;assert(i9100_mc1n2_clock_stop(&c)==I9100_MC_BUSY&&m.transactions==transactions&&c.clock_owned);
  assert(i9100_mc1n2_cold_stop(&c)==I9100_MC_BUSY&&m.transactions==transactions&&c.cold_owned);
  assert(i9100_mc1n2_enable(&c,0)==I9100_MC_BUSY&&m.transactions==transactions&&c.enabled);
  m.clock_consumer_busy=0;assert(!i9100_mc1n2_clock_stop(&c)&&!c.clock_owned&&!c.clock_active&&c.cold_owned);
  assert(m.codec[1][12]==0xae&&!m.codec[4][137]&&m.codec[4][135]==(rate==48000?0xa0:0xa1));
  assert(!c.clock_guard.lease_valid);assert(!i9100_mc1n2_cold_stop(&c));
 }
 init_clock(&c,48000);unsigned before=m.transactions;assert(!i9100_mc1n2_clock_start(&c,&clock_guard));
 unsigned count=m.transactions-before;
 for(unsigned point=1;point<=count;point++){
  init_clock(&c,48000);m.fail_transaction=m.transactions+point;int error=i9100_mc1n2_clock_start(&c,&clock_guard);
  assert(error&&!c.clock_owned&&!c.clock_active&&c.cold_owned&&!m.unsafe_amplifier_writes);
  assert(m.codec[1][12]==0xae&&!m.codec[4][137]);
  if(c.clock_failure_stage)assert(c.fault&&!c.clock_cleanup_error&&m.codec[4][135]==0xa0);
 }
 init_clock(&c,48000);m.codec[0][8]=0x78;before=m.transactions;
 assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_ID&&m.transactions==before+1&&!c.clock_owned);
 init_clock(&c,48000);m.codec[4][137]=0x1c;
 assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_NOT_READY&&!c.clock_owned); /* reserved bit4 is not the timer */
 init_clock(&c,48000);m.stall_ready=m.freeze_time=1;
 assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_TIMEOUT&&!c.clock_owned&&m.codec[1][12]==0xae);
 init_clock(&c,48000);m.clock_fail_lease_call=2;
 assert(i9100_mc1n2_clock_start(&c,&clock_guard)==I9100_MC_NOT_READY&&c.clock_failure_stage==3&&!c.clock_owned&&c.fault);
 assert(m.codec[1][12]==0xae&&!m.codec[4][137]);
 init_clock(&c,48000);assert(!i9100_mc1n2_clock_start(&c,&clock_guard));m.sticky_codec_error=1;
 assert(i9100_mc1n2_clock_stop(&c)==I9100_MC_IO&&c.clock_owned&&c.fault);
 assert(i9100_mc1n2_cold_stop(&c)==I9100_MC_IO&&c.cold_owned&&(m.codec[2][2]&15U)==6U);
 m.sticky_codec_error=0;assert(!i9100_mc1n2_clock_stop(&c));assert(!i9100_mc1n2_cold_stop(&c));
 init_clock(&c,48000);assert(!i9100_mc1n2_clock_start(&c,&clock_guard));m.clock_lease_invalid=1;
 assert(i9100_mc1n2_clock_stop(&c)==I9100_MC_NOT_READY&&c.clock_owned&&!c.clock_active);
 m.clock_lease_invalid=0;assert(!i9100_mc1n2_clock_stop(&c));
 init_clock(&c,48000);assert(!i9100_mc1n2_clock_start(&c,&clock_guard));assert(!i9100_mc1n2_enable(&c,0));
 assert(!c.clock_owned&&!c.cold_owned&&!c.enabled&&!m.codec[4][137]);
}
static void pcm(void)
{
 for(unsigned n=0;n<65536;n++)pcm_in[n]=(int16_t)(n-32768);
 i9100_pcm_attenuate_30db(pcm_out,pcm_in,65536);
 for(unsigned n=0;n<65536;n++){assert(pcm_out[n]>=-1036&&pcm_out[n]<=1036);assert(pcm_out[n]==(int32_t)pcm_in[n]*1036/32768);}
}
static int stream_lock(void *c){struct model *p=c;if(p->stream_locked)return -1;p->stream_locked=1;return 0;}
static void stream_unlock(void *c){struct model *p=c;assert(p->stream_locked);p->stream_locked=0;}
static int live_i2s_lock(void *c){struct model *p=c;if(p->i2s_locked)return -1;p->i2s_locked=1;return 0;}
static void live_i2s_unlock(void *c){struct model *p=c;assert(p->i2s_locked);p->i2s_locked=0;}
static int live_dma_idle(void *c){return i9100_pl330_idle(((struct model *)c)->stream_dma);}
static int live_dma_stop(void *c){struct model *p=c;assert(!(p->i2s[0]&7U));return i9100_pl330_stop(p->stream_dma);}
static int ensure_muted(void *c){struct model *p=c;assert(p->stream_locked);p->mute_calls++;return p->mute_fail?-99:0;}
static void init_stream(struct i9100_pcm_stream *s,struct i9100_pl330 *p,struct i9100_i2s *i)
{
 init_pl(p);memset(i,0,sizeof(*i));memset(s,0,sizeof(*s));m.stream_dma=p;m.require_quiesce=1;m.i2s[1]=0x20000000U;
 struct i9100_i2s_io ii={&m,i2s_read,i2s_write,live_i2s_lock,live_i2s_unlock,live_dma_idle,live_dma_stop,pads_idle};
 assert(!i9100_i2s_init(i,&ii));assert(!i9100_i2s_enable(i,1));
 struct i9100_pcm_io io={&m,stream_lock,stream_unlock,ensure_muted};
 unsigned reads=m.reads,writes=m.writes;
 assert(!i9100_pcm_stream_init(s,&io,p,i,&src_buffer.b,&dst_buffer.b,&code_buffer.b));
 assert(m.reads==reads&&m.writes==writes&&!m.mute_calls&&!s->enabled);
}
static void prepare_stream(struct i9100_pcm_stream *s,struct i9100_pl330 *p,struct i9100_i2s *i)
{
 init_stream(s,p,i);assert(!i9100_pcm_stream_enable(s,1));
 assert(!i9100_pcm_stream_prepare(s,48000,64,1536000,1));
 assert(s->held_mask==7&&src_buffer.b.holds==1&&dst_buffer.b.holds==1&&code_buffer.b.holds==1);
}
static uint32_t scaled_word(const int16_t *input,unsigned word)
{
 return (uint16_t)((int32_t)input[word*2]*1036/32768)|
  ((uint32_t)(uint16_t)((int32_t)input[word*2+1]*1036/32768)<<16);
}
static void assert_stream_released(struct i9100_pcm_stream *s,struct i9100_pl330 *p,struct i9100_i2s *i)
{
 assert(!s->held_mask&&!s->prepared&&!s->started&&!p->active&&!i->prepared&&!(m.i2s[0]&7U));
 assert(!src_buffer.b.holds&&!dst_buffer.b.holds&&!code_buffer.b.holds);
 assert(src_buffer.b.state==I9100_DMA_CPU&&dst_buffer.b.state==I9100_DMA_CPU&&code_buffer.b.state==I9100_DMA_CPU);
}
static void stream_periods(void)
{
 struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;uint32_t completed;
 const unsigned frames[]={16,64,257,4096};
 for(unsigned rate=44100;rate<=48000;rate+=3900)for(unsigned test=0;test<4;test++){
  init_stream(&s,&p,&i);unsigned writes=m.writes;
  assert(i9100_pcm_stream_prepare(&s,rate,frames[test],rate*32,1)==I9100_PCM_DISABLED&&m.writes==writes&&!m.mute_calls);
  assert(!i9100_pcm_stream_enable(&s,1));assert(!i9100_pcm_stream_prepare(&s,rate,frames[test],rate*32,1));
  for(unsigned n=0;n<frames[test]*2U;n++)pcm_in[n]=(int16_t)((n*509U+7001U)&65535U);
  assert(!i9100_pcm_stream_queue(&s,pcm_in,frames[test]));assert(!i9100_pcm_stream_queue(&s,pcm_in,frames[test]));
  assert(i9100_pcm_stream_queue(&s,pcm_in,frames[test])==I9100_PCM_BUSY);
  assert(!i9100_pcm_stream_start(&s)&&p.active&&s.in_flight==0&&s.ready_mask==2);
  completed=77;assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_BUSY&&!completed&&!s.completed_frames);
  execute(p.program_bytes);assert(!i9100_pcm_stream_service(&s,&completed)&&completed==1);
  assert(s.completed_frames==frames[test]&&s.completed_periods==1&&s.in_flight==1&&!s.zero_periods);
  for(unsigned n=0;n<frames[test];n++)assert(transmitted[n]==scaled_word(pcm_in,n));
  assert(i9100_dma_release(&src_buffer.b)==I9100_DMA_BUSY); /* CPU again, but still pinned */
  assert(!i9100_pcm_stream_queue(&s,pcm_in,frames[test]));
  for(unsigned retry=0;retry<3;retry++)assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_BUSY&&!completed&&s.completed_periods==1);
  execute(p.program_bytes);assert(!i9100_pcm_stream_service(&s,&completed)&&completed==1&&s.completed_periods==2);
  for(unsigned n=0;n<frames[test];n++)assert(transmitted[frames[test]+n]==scaled_word(pcm_in,n));
  struct i9100_pcm_status status;assert(!i9100_pcm_stream_status(&s,&status));
  assert(status.completed_frames==frames[test]*2U&&status.queued_frames==frames[test]*3U&&status.held_mask==7);
  assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
  assert(!i9100_pcm_stream_enable(&s,0));
 }
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s)&&s.zero_periods==1);
 execute(p.program_bytes);assert(!i9100_pcm_stream_service(&s,&completed)&&s.zero_periods==2);
 for(unsigned n=0;n<64;n++)assert(!transmitted[n]);assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);pcm_in[0]=32767;pcm_in[1]=-32768;
 memset(source,0xcc,256);assert(!i9100_pcm_stream_queue(&s,pcm_in,1));assert(!i9100_pcm_stream_start(&s));
 execute(p.program_bytes);assert(transmitted[0]==scaled_word(pcm_in,0));
 for(unsigned n=1;n<64;n++)assert(!transmitted[n]);assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_queue(&s,0,0));assert(!i9100_pcm_stream_start(&s)&&!s.zero_periods);
 assert(!i9100_pcm_stream_stop(&s));
 /* A long alternating producer run uses the actual finite-program VM, not
  * a fake completion counter. The final next job is cancelled, not counted. */
 prepare_stream(&s,&p,&i);for(unsigned n=0;n<128;n++)pcm_in[n]=(int16_t)(n*431U);
 assert(!i9100_pcm_stream_queue(&s,pcm_in,64));assert(!i9100_pcm_stream_start(&s));
 for(unsigned n=0;n<512;n++){
  if(n<511)assert(!i9100_pcm_stream_queue(&s,pcm_in,64));
  execute(p.program_bytes);assert(!i9100_pcm_stream_service(&s,&completed)&&completed==1);
  assert(s.completed_frames==(uint64_t)(n+1)*64&&s.completed_periods==n+1);
 }
 assert(m.fifo_words==32768&&s.zero_periods==1);
 for(unsigned n=0;n<32768;n++)assert(transmitted[n]==scaled_word(pcm_in,n%64));
 assert(!i9100_pcm_stream_stop(&s)&&s.completed_periods==512);assert_stream_released(&s,&p,&i);
}
static void stream_rejects(void)
{
 struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;prepare_stream(&s,&p,&i);
 assert(i9100_pcm_stream_queue(&s,0,1)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_queue(&s,pcm_in,65)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_queue(&s,(const int16_t *)(UINTPTR_MAX-1U),1)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_queue(&s,(const int16_t *)source,1)==I9100_PCM_OWNER);
 assert(i9100_pcm_stream_queue(&s,(const int16_t *)destination,1)==I9100_PCM_OWNER);
 assert(i9100_pcm_stream_queue(&s,(const int16_t *)program,1)==I9100_PCM_OWNER);
 s.queued_frames=UINT64_MAX;assert(i9100_pcm_stream_queue(&s,pcm_in,1)==I9100_PCM_OVERFLOW&&!s.ready_mask);
 struct i9100_pcm_stream copy=s;assert(i9100_pcm_stream_start(&copy)==I9100_PCM_OWNER);
 m.stream_locked=1;assert(i9100_pcm_stream_queue(&s,pcm_in,1)==I9100_PCM_BUSY&&!s.fault);m.stream_locked=0;
 assert(!i9100_pcm_stream_stop(&s));
 init_stream(&s,&p,&i);assert(!i9100_pcm_stream_enable(&s,1));unsigned reads=m.reads,writes=m.writes;
 assert(i9100_pcm_stream_prepare(&s,48000,15,1536000,1)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_prepare(&s,48000,4097,1536000,1)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536001,1)==I9100_PCM_ARGUMENT);
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536000,0)==I9100_PCM_ARGUMENT);
 assert(m.reads==reads&&m.writes==writes&&!m.mute_calls);
 dst_buffer.b.physical=src_buffer.b.physical;
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536000,1)==I9100_PCM_OWNER&&!s.held_mask);
 dst_buffer.b.physical=m.dst_pa;dst_buffer.b.cpu=(uintptr_t)source;
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536000,1)==I9100_PCM_OWNER&&!s.held_mask);
 dst_buffer.b.cpu=(uintptr_t)destination;assert(!i9100_dma_hold(&dst_buffer.b));
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536000,1)==I9100_PCM_OWNER&&!s.held_mask);assert(!i9100_dma_unhold(&dst_buffer.b));
 struct i9100_pcm_stream other={0};struct i9100_pcm_io wrong={&m,lock,unlock,ensure_muted};
 assert(i9100_pcm_stream_init(&other,&wrong,&p,&i,&src_buffer.b,&dst_buffer.b,&code_buffer.b)==I9100_PCM_ARGUMENT);
 code_buffer.fail_lock=code_buffer.lock_calls+1;
 assert(i9100_pcm_stream_prepare(&s,48000,64,1536000,1)==I9100_PCM_IO&&s.fault&&!s.held_mask);
 assert(!src_buffer.b.holds&&!dst_buffer.b.holds);assert(!i9100_pcm_stream_stop(&s));
}
static void stream_faults(void)
{
 struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;uint32_t completed;
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));m.reject_quiesce=1;
 assert(i9100_pcm_stream_stop(&s)==I9100_PCM_IO&&s.fault&&s.started&&s.held_mask==7&&!m.kills);
 assert(p.active&&src_buffer.b.state==I9100_DMA_DEVICE);unsigned reads=m.reads,writes=m.writes;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_FAULT&&!completed);
 assert(i9100_pcm_stream_queue(&s,pcm_in,64)==I9100_PCM_FAULT&&m.reads==reads&&m.writes==writes);
 m.reject_quiesce=0;assert(!i9100_pcm_stream_stop(&s)&&m.kills==1);assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));m.ns=1000000000ULL;m.kill_stuck=m.freeze_time=1;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&!completed&&s.last_error==I9100_PL330_TIMEOUT);
 assert(m.kills==1&&s.fault&&s.started&&s.held_mask==7&&p.active&&code_buffer.b.state==I9100_DMA_QUARANTINED&&!(m.i2s[0]&7U));
 writes=m.writes;assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_FAULT&&m.writes==writes);
 assert(i9100_dma_release(&src_buffer.b)==I9100_DMA_BUSY);
 m.kill_stuck=m.freeze_time=0;assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);code_buffer.fail_sync=code_buffer.sync_count+1;
 assert(i9100_pcm_stream_start(&s)==I9100_PCM_IO&&s.fault&&!s.completed_frames);assert_stream_released(&s,&p,&i);
 assert(!i9100_pcm_stream_stop(&s)&&!s.fault);
 prepare_stream(&s,&p,&i);m.go_busy=1;assert(i9100_pcm_stream_start(&s)==I9100_PCM_IO&&s.fault&&s.held_mask==7);
 assert(!(m.i2s[0]&7U)&&p.active);m.go_busy=0;m.regs[0xd00/4]=0;assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);m.i2s_fail_write=m.i2s_writes+2;
 assert(i9100_pcm_stream_start(&s)==I9100_PCM_IO&&s.fault&&m.kills==1);assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);m.mute_fail=1;writes=m.writes;
 assert(i9100_pcm_stream_start(&s)==I9100_PCM_IO&&s.fault&&s.held_mask==7&&!s.started&&m.writes==writes);
 assert(i9100_pcm_stream_stop(&s)==I9100_PCM_IO&&s.held_mask==7&&!p.active&&!i.prepared);
 m.mute_fail=0;assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));m.i2s_fail_write=m.i2s_writes+5;
 assert(i9100_pcm_stream_stop(&s)==I9100_PCM_IO&&s.held_mask==7&&s.i2s_owned&&!p.active&&i.prepared);
 assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));execute(p.program_bytes);m.sticky_event=1;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&!completed&&s.fault&&s.held_mask==7&&p.active);
 assert(!s.completed_frames);m.sticky_event=0;assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));execute(p.program_bytes);dst_buffer.fail_sync=dst_buffer.sync_count+1;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&completed==1&&s.completed_frames==64&&m.kills==1);
 assert(s.fault&&!p.active);assert_stream_released(&s,&p,&i);assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));m.regs[0x34/4]=1;m.regs[0x40/4]=0x99;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&s.last_error==I9100_PL330_FAULT&&p.error_status==0x99&&!completed&&m.kills==1);
 assert_stream_released(&s,&p,&i);assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));code_buffer.fail_lock=code_buffer.lock_calls+2;
 assert(i9100_pcm_stream_stop(&s)==I9100_PCM_IO&&s.held_mask==4&&code_buffer.b.holds==1&&!src_buffer.b.holds&&!dst_buffer.b.holds);
 assert(!p.active&&!s.i2s_owned);assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));s.completed_frames=UINT64_MAX-63;execute(p.program_bytes);
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&!completed&&s.last_error==I9100_PCM_OVERFLOW);
 assert(!i9100_pcm_stream_stop(&s));
 /* An unrelated job cannot be killed even if a caller violated ownership. */
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));p.job.source=&dst_buffer.b;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_OWNER&&s.held_mask==7&&!m.kills);
 p.job.source=&src_buffer.b;assert(!i9100_pcm_stream_stop(&s));assert_stream_released(&s,&p,&i);
 prepare_stream(&s,&p,&i);m.i2s[0]|=1U<<17;
 assert(i9100_pcm_stream_start(&s)==I9100_PCM_IO&&s.last_error==I9100_I2S_STALE&&!s.completed_periods&&s.i2s_fault_valid);
 assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));execute(p.program_bytes);m.i2s[0]|=1U<<17;m.i2s[2]=(7U<<8)|2U;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&completed==1&&s.completed_frames==64&&s.last_error==I9100_I2S_FIFO);
 struct i9100_pcm_status stats;assert(!i9100_pcm_stream_status(&s,&stats));
 assert(stats.i2s_fault_valid&&stats.i2s_fault.errors==(1U<<17)&&stats.i2s_fault.tx_count_encoded==7&&stats.fifo_errors==1);
 assert(!i9100_pcm_stream_stop(&s));
 prepare_stream(&s,&p,&i);assert(!i9100_pcm_stream_start(&s));m.i2s[0]|=(1U<<26)|(1U<<24);
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&!completed&&!s.completed_frames&&s.i2s_fault_valid);
 assert(s.i2s_fault.errors==((1U<<26)|(1U<<24))&&!(m.i2s[0]&7U));assert(!i9100_pcm_stream_stop(&s));
}
int main(void)
{pl330();i2s();codec();codec_cold();codec_aa();codec_clock();pcm();stream_periods();stream_rejects();stream_faults();
 puts("PASS finite DMA/I2S pad-idle-before-mode-restore/MC1N2 cold/mute/clock-only4c/all clock transactions/busy consumers/sticky shutdown; PCM512 periods/zero/-30dB/exact positions/pins/explicit recovery");return 0;}
