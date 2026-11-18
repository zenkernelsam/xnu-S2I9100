/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse only the existing bus/register/finite-PL330 VM fixture. Production
 * codec and PCM are separately compiled unchanged; no active/ready fields
 * are injected to make output qualification pass. The old regression main
 * remains a separately run suite with its amplifier-off assertions intact. */
#define main i9100_audio_transport_fixture_main
#include "i9100_audio_transport_test.c"
#undef main

static int output_lease(void *c)
{return ((struct model *)c)->clock_lease_invalid?-1:0;}
static int output_idle(void *c)
{
 struct model *p=c;
 return p->clock_consumer_busy||p->i2s[0]&7U||p->regs[0x100U/4U]||p->regs[0xd00U/4U]?-1:0;
}
static int output_stream(void *c,uint32_t rate)
{
 struct model *p=c;
 return p->output_pcm?i9100_pcm_stream_output_ready(p->output_pcm,rate):I9100_PCM_DISABLED;
}
static struct i9100_mc1n2_output_guard output_guard={&m,output_lease,output_idle,output_stream};
static void output_init(struct i9100_mc1n2 *c,unsigned id,unsigned kind)
{
 codec_init(c);m.codec[0][8]=(uint8_t)id;
 assert(!i9100_mc1n2_enable(c,1));assert(!i9100_mc1n2_cold_init(c,24000000,48000,1));
 assert(!i9100_mc1n2_clock_start(c,&clock_guard));
 m.output_authorized=1;m.output_expected_kind=kind;
}
static void silence(void)
{
 assert(!(m.codec[4][89]&0x7fU)&&!(m.codec[4][90]&0x7fU));
 for(unsigned n=35;n<=38U;n++)assert(!(m.codec[2][n]&31U));
}
static void standby(struct i9100_mc1n2 *c,struct i9100_mc1n2_output_lease *l)
{
 silence();assert((m.codec[2][3]&0x3fU)==0x3fU&&m.codec[2][2]==6U&&m.codec[3][78]==2U);
 assert((m.codec[2][5]&(c->id==0x78U?8U:0x18U))==(c->id==0x78U?8U:0x18U));
 assert(!l->owned&&!c->output_owner&&!c->output_powered&&!c->output_running);
 assert(c->clock_owned&&c->clock_active&&c->cold_owned&&m.codec[4][137]==0x4cU);
}
static void prepare_rejects(void)
{
 struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,0x79,I9100_MC_SPEAKER);
 unsigned before=m.transactions;
 assert(i9100_mc1n2_output_prepare(0,&c,I9100_MC_SPEAKER,-30000,&output_guard,&l)==I9100_MC_DISABLED);
 assert(i9100_mc1n2_output_prepare(1,&c,I9100_MC_SPEAKER,-29999,&output_guard,&l)==I9100_MC_ARGUMENT);
 struct i9100_mc1n2 copy=c;
 assert(i9100_mc1n2_output_prepare(1,&copy,I9100_MC_SPEAKER,-30000,&output_guard,&l)==I9100_MC_OWNER);
 assert(i9100_mc1n2_mute(&copy)==I9100_MC_OWNER);
 assert(m.transactions==before);
 m.clock_consumer_busy=1;
 assert(i9100_mc1n2_output_prepare(1,&c,I9100_MC_SPEAKER,-30000,&output_guard,&l)==I9100_MC_BUSY&&m.transactions==before);
 m.clock_consumer_busy=0;m.clock_lease_invalid=1;
 assert(i9100_mc1n2_output_prepare(1,&c,I9100_MC_SPEAKER,-30000,&output_guard,&l)==I9100_MC_NOT_READY&&m.transactions==before);
 m.clock_lease_invalid=0;m.codec[2][16]=0;
 assert(i9100_mc1n2_output_prepare(1,&c,I9100_MC_SPEAKER,-30000,&output_guard,&l)==I9100_MC_IO&&!l.owned);
 assert(!i9100_mc1n2_cold_stop(&c));
}
static void output_success(void)
{
 for(unsigned id=0x78U;id<=0x79U;id++)for(unsigned kind=1;kind<=2U;kind++){
  struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,id,kind);
  /* Unrelated input power bits and disconnected standby routes are retained. */
  m.codec[2][3]&=~0x80U;for(unsigned n=61;n<=67U;n++)m.codec[2][n]=(uint8_t)(n&1U?1U:8U);
  uint8_t saved_routes[7];memcpy(saved_routes,&m.codec[2][61],7);
  uint64_t before=m.ns;
  assert(!i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l));
  assert(l.owned&&c.output_owner==&l&&c.output_stage==5&&c.output_powered&&!c.output_running&&c.muted);
  assert(m.ns-before>=(kind==1U?9300000ULL:13150000ULL));silence();
  assert(!(m.codec[2][3]&0x80U)&&!m.unsafe_amplifier_writes);
  assert(m.codec[2][5]==7U); /* AA clears08; AB clears18, never writes reserved10 on AA */
  unsigned transactions=m.transactions;
  assert(i9100_mc1n2_clock_stop(&c)==I9100_MC_BUSY&&i9100_mc1n2_cold_stop(&c)==I9100_MC_BUSY);
  assert(i9100_mc1n2_enable(&c,0)==I9100_MC_BUSY&&i9100_mc1n2_stage_gain(&c,I9100_MC_SPEAKER,-30000)==I9100_MC_BUSY);
  assert(m.transactions==transactions);
  struct i9100_mc1n2_output_lease copy=l;
  assert(i9100_mc1n2_output_stop(&copy)==I9100_MC_OWNER&&m.transactions==transactions);
  assert(i9100_mc1n2_output_activate(&l)==I9100_MC_NOT_READY);silence();
  assert(!i9100_mc1n2_mute(&c)&&l.owned&&c.output_powered);silence();
  assert(!i9100_mc1n2_output_stop(&l));standby(&c,&l);
  assert(!memcmp(saved_routes,&m.codec[2][61],7));assert(!(m.codec[2][3]&0x80U));
  assert(i9100_mc1n2_output_stop(&l)==I9100_MC_OWNER);
  assert(!i9100_mc1n2_cold_stop(&c));
 }
}
static unsigned prepare_transaction_count(unsigned id,unsigned kind)
{
 struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,id,kind);
 unsigned at=m.transactions;assert(!i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l));
 unsigned count=m.transactions-at;assert(!i9100_mc1n2_output_stop(&l));assert(!i9100_mc1n2_cold_stop(&c));return count;
}
static void prepare_faults(void)
{
 for(unsigned id=0x78U;id<=0x79U;id++)for(unsigned kind=1;kind<=2U;kind++){
  unsigned count=prepare_transaction_count(id,kind);
  for(unsigned drop=0;drop<=1;drop++)for(unsigned point=1;point<=count;point++){
   struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,id,kind);
   if(drop)m.drop_transaction=m.transactions+point;else m.fail_transaction=m.transactions+point;
   int e=i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l);
   if(!drop)assert(e);
   if(e){assert(!c.output_running);if(l.owned)assert(!i9100_mc1n2_output_stop(&l));}
   else assert(!i9100_mc1n2_output_stop(&l));
   standby(&c,&l);assert(!i9100_mc1n2_cold_stop(&c));
  }
  for(unsigned fail=0;fail<3U;fail++)for(unsigned delay_point=1;delay_point<=2U;delay_point++){
   struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,id,kind);
   unsigned call=m.codec_delay_calls+delay_point;
   if(fail==0U)m.fail_delay_call=call;else if(fail==1U)m.freeze_delay_call=call;else m.reverse_delay_call=call;
   assert(i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l));
   if(l.owned)assert(!i9100_mc1n2_output_stop(&l));standby(&c,&l);assert(!i9100_mc1n2_cold_stop(&c));
  }
 }
}
static void retained_cleanup(void)
{
 struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,0x79U,I9100_MC_HEADPHONES);
 assert(!i9100_mc1n2_output_prepare(1,&c,I9100_MC_HEADPHONES,-30000,&output_guard,&l));
 m.clock_consumer_busy=1;
 assert(i9100_mc1n2_output_stop(&l)==I9100_MC_BUSY&&l.owned&&c.clock_owned&&c.cold_owned);silence();
 assert(!(m.codec[2][2]&2U)&&!(m.codec[2][5]&0x18U));
 m.clock_consumer_busy=0;m.output_cp_not_ready=1;m.freeze_time=1;
 assert(i9100_mc1n2_output_stop(&l)==I9100_MC_TIMEOUT&&l.owned&&!(m.codec[2][2]&2U));
 assert(m.codec[3][78]==2U&&(m.codec[2][5]&0x18U)==0x18U);
 m.output_cp_not_ready=m.freeze_time=0;m.clock_lease_invalid=1;
 assert(!i9100_mc1n2_output_stop(&l));standby(&c,&l); /* revoked start still permits old-owner cleanup */
 m.clock_lease_invalid=0;assert(!i9100_mc1n2_cold_stop(&c));
 l=(struct i9100_mc1n2_output_lease){0};output_init(&c,0x78U,I9100_MC_SPEAKER);
 assert(!i9100_mc1n2_output_prepare(1,&c,I9100_MC_SPEAKER,-30000,&output_guard,&l));
 m.sticky_codec_error=1;
 assert(i9100_mc1n2_output_stop(&l)==I9100_MC_IO&&l.owned&&c.output_owner==&l);
 assert(i9100_mc1n2_clock_stop(&c)==I9100_MC_BUSY);m.sticky_codec_error=0;
 assert(!i9100_mc1n2_output_stop(&l));standby(&c,&l);assert(!i9100_mc1n2_cold_stop(&c));
}
static void stop_faults(void)
{
 for(unsigned kind=1;kind<=2U;kind++){
  struct i9100_mc1n2 c;struct i9100_mc1n2_output_lease l={0};output_init(&c,0x79U,kind);
  assert(!i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l));
  unsigned before=m.transactions;assert(!i9100_mc1n2_output_stop(&l));unsigned count=m.transactions-before;
  for(unsigned drop=0;drop<=1U;drop++)for(unsigned point=1;point<=count;point++){
   memset(&l,0,sizeof(l));output_init(&c,0x79U,kind);
   assert(!i9100_mc1n2_output_prepare(1,&c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,&l));
   if(drop)m.drop_transaction=m.transactions+point;else m.fail_transaction=m.transactions+point;
   int e=i9100_mc1n2_output_stop(&l);if(!drop)assert(e);
   if(e){assert(l.owned&&c.output_owner==&l&&c.clock_owned&&c.cold_owned);assert(!i9100_mc1n2_output_stop(&l));}
   standby(&c,&l);assert(!i9100_mc1n2_cold_stop(&c));
  }
 }
}
static void integrated_init(struct i9100_pcm_stream *s,struct i9100_pl330 *p,
 struct i9100_i2s *i,struct i9100_mc1n2 *c,struct i9100_mc1n2_output_lease *l,unsigned kind)
{
 prepare_stream(s,p,i);memset(c,0,sizeof(*c));memset(l,0,sizeof(*l));m.codec[0][8]=0x79U;
 assert(!i9100_mc1n2_init(c,&codec_io));assert(!i9100_mc1n2_enable(c,1));
 assert(!i9100_mc1n2_cold_init(c,24000000,48000,1));assert(!i9100_mc1n2_clock_start(c,&clock_guard));
 m.output_authorized=1;m.output_expected_kind=kind;m.output_pcm=s;m.output_codec=c;
 assert(!i9100_mc1n2_output_prepare(1,c,(enum i9100_mc1n2_output)kind,-30000,&output_guard,l));
 assert(!i9100_pcm_stream_start(s));silence();
}
static void qualify(struct i9100_pcm_stream *s,struct i9100_pl330 *p)
{
 unsigned writes=m.writes;
 assert(i9100_pcm_stream_output_ready(s,48000)==I9100_PCM_BUSY&&m.writes==writes);
 uint32_t completed=0;execute(p->program_bytes);assert(!i9100_pcm_stream_service(s,&completed)&&completed==1);
 assert(s->completed_silence_periods==1U&&!i9100_pcm_stream_output_ready(s,48000));
}
static void actual_pcm_output(void)
{
 for(unsigned kind=1;kind<=2U;kind++){
  struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;struct i9100_mc1n2 c;
  struct i9100_mc1n2_output_lease l;integrated_init(&s,&p,&i,&c,&l,kind);
  assert(i9100_mc1n2_output_activate(&l)==I9100_MC_NOT_READY);qualify(&s,&p);
  unsigned transactions=m.transactions,writes=m.writes;
  assert(!i9100_mc1n2_output_activate(&l)&&c.output_running&&c.actual_gain_mdb==-36000&&!c.muted);
  assert((m.codec[4][89]&0x7fU)==66U&&(m.codec[4][90]&0x7fU)==66U);
  assert((m.codec[2][kind==1U?35U:37U]&31U)==1U&&m.transactions>transactions&&m.writes==writes);
  assert(!i9100_mc1n2_mute(&c)&&l.owned&&c.output_powered&&!c.output_running);silence();
  assert(!i9100_mc1n2_output_activate(&l));
  /* The actual stream fault must call the real owned-output mute before
   * DMA kill, even though start previously established silence. */
  m.i2s[0]|=1U<<17;uint32_t completed=0;
  assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_IO&&!completed&&s.fault);
  silence();assert(l.owned&&c.clock_owned&&!c.output_running&&m.kills);
  assert(!i9100_pcm_stream_stop(&s));assert(!i9100_mc1n2_output_stop(&l));standby(&c,&l);
  assert(!i9100_mc1n2_cold_stop(&c));
 }
}
static void activation_faults(void)
{
 struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;struct i9100_mc1n2 c;
 struct i9100_mc1n2_output_lease l;integrated_init(&s,&p,&i,&c,&l,I9100_MC_SPEAKER);qualify(&s,&p);
 unsigned before=m.transactions;assert(!i9100_mc1n2_output_activate(&l));unsigned count=m.transactions-before;
 assert(!i9100_pcm_stream_stop(&s));assert(!i9100_mc1n2_output_stop(&l));assert(!i9100_mc1n2_cold_stop(&c));
 for(unsigned drop=0;drop<=1U;drop++)for(unsigned point=1;point<=count;point++){
  integrated_init(&s,&p,&i,&c,&l,I9100_MC_SPEAKER);qualify(&s,&p);
  if(drop)m.drop_transaction=m.transactions+point;else m.fail_transaction=m.transactions+point;
  int e=i9100_mc1n2_output_activate(&l);if(!drop)assert(e);
  if(e){silence();assert(l.owned&&!c.output_running&&c.fault&&c.output_error);
   before=m.transactions;assert(i9100_mc1n2_output_activate(&l)==I9100_MC_BUSY&&m.transactions==before);}
  else assert(c.output_running&&c.actual_gain_mdb==-36000);
  assert(!i9100_pcm_stream_stop(&s));assert(!i9100_mc1n2_output_stop(&l));assert(!i9100_mc1n2_cold_stop(&c));
 }
 /* A nonzero completed period is not a qualifying silent pre-roll. */
 prepare_stream(&s,&p,&i);memset(pcm_in,0,sizeof(pcm_in));pcm_in[0]=-32768;
 assert(!i9100_pcm_stream_queue(&s,pcm_in,64));assert(!i9100_pcm_stream_start(&s));
 uint32_t completed=0;execute(p.program_bytes);assert(!i9100_pcm_stream_service(&s,&completed)&&completed==1);
 assert(!s.completed_silence_periods&&i9100_pcm_stream_output_ready(&s,48000)==I9100_PCM_BUSY);
 assert(!i9100_pcm_stream_stop(&s));
 integrated_init(&s,&p,&i,&c,&l,I9100_MC_HEADPHONES);
 execute(p.program_bytes);((int16_t *)s.period[s.in_flight]->cpu)[0]=32767;
 assert(i9100_pcm_stream_service(&s,&completed)==I9100_PCM_OWNER&&!completed&&!s.completed_silence_periods);
 silence();assert(!i9100_pcm_stream_stop(&s));assert(!i9100_mc1n2_output_stop(&l));assert(!i9100_mc1n2_cold_stop(&c));
}
static void qualification_faults(void)
{
 for(unsigned which=0;which<10U;which++){
  struct i9100_pcm_stream s;struct i9100_pl330 p;struct i9100_i2s i;struct i9100_mc1n2 c;
  struct i9100_mc1n2_output_lease l;integrated_init(&s,&p,&i,&c,&l,I9100_MC_SPEAKER);qualify(&s,&p);
  uint32_t old=m.i2s[1],old_cs=m.regs[0x100/4];unsigned writes=m.writes;
  struct i9100_dma_buffer *active=s.period[s.in_flight];uintptr_t old_cpu=active->cpu;uint32_t old_pa=active->physical;
  if(which==0U)m.i2s[1]&=~(1U<<11);
  else if(which==1U)m.regs[0x100/4]=0;
  else if(which==2U)m.regs[0x34/4]=1;
  else if(which==3U)m.i2s[0]|=1U<<17;
  else if(which==4U)((int16_t *)s.period[s.in_flight]->cpu)[0]=32767;
  else if(which==5U)m.ns=p.started+(uint64_t)p.job.timeout_ms*1000000ULL;
  else if(which==6U)p.job.source=s.period[s.in_flight^1U];
  else if(which==7U)m.stream_locked=1;
  else if(which==8U)active->cpu=1U;
  else active->physical+=64U;
  assert(i9100_pcm_stream_output_ready(&s,48000));
  assert(i9100_mc1n2_output_activate(&l)==I9100_MC_NOT_READY&&!c.output_running&&m.writes==writes);silence();
  active->cpu=old_cpu;active->physical=old_pa;
  m.i2s[1]=old;m.regs[0x100/4]=old_cs;m.regs[0x34/4]=0;m.i2s[0]&=~(1U<<17);
  ((int16_t *)s.period[s.in_flight]->cpu)[0]=0;m.ns=p.started;p.job.source=s.period[s.in_flight];m.stream_locked=0;
  assert(!i9100_pcm_stream_stop(&s));assert(!i9100_mc1n2_output_stop(&l));assert(!i9100_mc1n2_cold_stop(&c));
 }
}
int main(void)
{
 prepare_rejects();output_success();prepare_faults();retained_cleanup();stop_faults();actual_pcm_output();activation_faults();qualification_faults();
 puts("PASS owned AA/AB HP/SP output: vendor DAC/CP/SP stages, zero-gain prepare, every IO/drop/delay fault, retained shutdown; real finite-PCM silence/DMA/I2S qualification and fault mute");return 0;
}
