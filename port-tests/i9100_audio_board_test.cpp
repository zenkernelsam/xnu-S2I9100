/* SPDX-License-Identifier: BSD-2-Clause
 * Reuse the same host IOKit facade for the real board + PCM adapters.
 * AUDSS/I2S pages are inaccessible until simulated PMU status confirms ON.
 * This is still a host model, not hardware/cache/waveform evidence. */
#include "i9100_audio_board_native.h"
#define main baseline_audio_native_main
#include "i9100_audio_native_test.cpp"
#undef main

int i9100_peripheral_native_i2c_open(i9100_peripheral_native *,unsigned index,uint32_t pclk,i9100_peripheral_i2c *out)
{
 assert(index==1U&&pclk==100000000U);memset(out,0,sizeof(*out));
 if(m.board_bus_owned)return I9100_MC_BUSY;
 m.board_bus_owned=1;out->cookie=&m;out->transfer=bus_transfer;out->claim=bus_lock;
 out->release=bus_unlock;out->now_ns=bus_now;out->delay_us=bus_delay;
 return m.board_bus_fail?I9100_MC_IO:0;
}
int i9100_peripheral_native_i2c_close(i9100_peripheral_native *,unsigned index)
{assert(index==1U&&m.board_bus_owned);if(m.board_close_fail)return I9100_MC_IO;m.board_bus_owned=0;return 0;}
int i9100_peripheral_native_hold_get(i9100_peripheral_native *c,i9100_native_parent_hold *h)
{assert(c==&parent&&!h->owned);h->context=c;h->self=(uintptr_t)h;h->owned=1;c->parent_holds++;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->owned&&h->self==(uintptr_t)h&&h->context==&parent&&parent.parent_holds);h->owned=0;parent.parent_holds--;return 0;}
int i9100_peripheral_native_i2c_open_owned(i9100_peripheral_native *c,i9100_native_transport_lease *l,unsigned index,uint32_t pclk,i9100_peripheral_i2c *out)
{int e=i9100_peripheral_native_i2c_open(c,index,pclk,out);if(m.board_bus_owned){l->context=c;l->self=(uintptr_t)l;l->kind=I9100_NATIVE_I2C6;l->generation=l->owned=1;}return e;}
int i9100_peripheral_native_transport_close(i9100_native_transport_lease *l)
{assert(l->owned&&l->self==(uintptr_t)l&&l->kind==I9100_NATIVE_I2C6);int e=i9100_peripheral_native_i2c_close(l->context,1);if(!e)l->owned=0;return e;}
static i9100_audio_board_profile profile()
{
 i9100_audio_board_profile p;memset(&p,0,sizeof(p));p.verified_rates.xtal=24000000U;
 p.audss_config.main_source=p.audss_config.i2s_source=0;
 p.audss_config.rp_div=p.audss_config.bus_div=p.audss_config.i2s_div=1;
 p.use_config=1;p.verified_i2c6_pclk_hz=100000000;p.sample_rate=48000;p.period_frames=64;
 p.exclusive_u1_rails_clocks_verified=1;return p;
}
static void board_setup()
{
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 setup();memset(board_audss,0,sizeof(board_audss));memset(board_i2s,0,sizeof(board_i2s));
 m.board_mode=1;m.chip[0]=0x43210010U;m.pmu[0xa00/4]=m.previous_mclk=0x12340001U;
 memset(m.codec,0,sizeof(m.codec));m.codec[0][8]=0x79;m.codec[3][78]=2;m.codec[2][3]=0x3f;
 assert(!mprotect(board_audss,4096,PROT_NONE)&&!mprotect(board_i2s,4096,PROT_NONE));
}
static void board_released()
{
 assert(!m.contexts&&!m.maps&&!m.descriptors&&!m.locks&&!m.dma_count&&!m.board_bus_owned&&!parent.parent_holds);
 assert((m.pmu[0x3cc4/4]&7U)==0U&&m.pmu[0xa00/4]==0x12340001U);
 for(unsigned n=0;n<7;n++)assert(!parent.gpio.banks[I9100_GPZ].owners[n]);
 assert((m.codec[2][3]&0x3fU)==0x3fU&&(m.codec[3][78]&2U)&&!m.codec[4][137]);
 assert(m.cmu[0xc93c/4]==0U); /* Exynos4x12-only gate never touched */
}
static int open_board(i9100_audio_board_native **out)
{i9100_audio_board_profile p=profile();return i9100_audio_board_open(1,&parent,&p,out);}
#ifndef I9100_BOARD_MODEL_ONLY
int main()
{
 i9100_audio_board_native *a=0;board_setup();
 assert(i9100_audio_board_open(0,0,0,&a)==I9100_AP_DISABLED&&!m.allocations&&!m.transactions);
 m.chip[0]=0x43220000U;assert(open_board(&a)==I9100_AP_OWNER&&!a&&!m.transactions);board_released();
 board_setup();assert(!open_board(&a)&&a);
 struct i9100_audio_board_status status;assert(!i9100_audio_board_status(a,&status,0));
 assert(status.mclk_owned&&status.domain_owned&&status.audss_active&&status.clock_owned&&status.pins_owned&&status.bus_owned&&!status.pcm_owned);
 unsigned allocations=m.allocations,transactions=m.transactions;
 assert(m.codec[1][12]==0x5e&&m.codec[4][137]==0x4c&&m.codec[3][78]==2&&m.gpio[2][0]==0);
 assert(!i9100_audio_board_pcm_open(a,1536000));assert(!i9100_audio_board_start(a));
 uint32_t completed=77;assert(i9100_audio_board_service(a,&completed)==I9100_PCM_BUSY&&!completed);
 m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;
 assert(!i9100_audio_board_service(a,&completed)&&completed==1);
 assert(!i9100_audio_board_close(&a)&&!a);board_released();
 board_setup();m.codec[3][78]=1;m.codec[2][3]=0;m.check_mclk_silence=1;
 assert(!open_board(&a));assert(!i9100_audio_board_close(&a));board_released();
 board_setup();m.codec[0][8]=0x78;assert(!open_board(&a)&&m.board_aa_unlocked&&m.codec[1][14]==0);
 assert(!i9100_audio_board_pcm_open(a,1536000));assert(!i9100_audio_board_start(a));
 assert(!i9100_audio_board_close(&a));assert(m.codec[1][14]==1&&m.codec[1][1]==7);board_released();
 for(unsigned point=1;point<=allocations;point++){
  board_setup();m.fail_allocation=point;assert(open_board(&a)!=0&&!a);board_released();
 }
 /* Every setup transaction may fail after applying its bytes. */
 for(unsigned point=1;point<=transactions;point++){
  board_setup();m.board_tx_fail=point;int e=open_board(&a);assert(e);
  if(a){m.board_tx_fail=0;assert(!i9100_audio_board_close(&a));}
  board_released();
 }
 board_setup();m.board_bus_fail=1;m.board_close_fail=1;
 assert(open_board(&a)!=0&&a&&m.board_bus_owned&&m.maps==5);m.board_bus_fail=m.board_close_fail=0;
 assert(!i9100_audio_board_close(&a));board_released();
 board_setup();m.board_stall_power=1;assert(open_board(&a)!=0);
 if(a){m.board_stall_power=0;assert(!i9100_audio_board_close(&a));}board_released();
 board_setup();assert(!open_board(&a));assert(!i9100_audio_board_pcm_open(a,1536000));assert(!i9100_audio_board_start(a));
 m.pdma[0x100/4]=1;m.kill_stuck=1;
 assert(i9100_audio_board_close(&a)!=0&&a&&m.dma_count==7&&m.maps==8&&(m.pmu[0x3cc4/4]&7U)==7U);
 assert(!i9100_audio_board_status(a,&status,0)&&status.clock_owned&&status.domain_owned&&status.pins_owned);
 assert(i9100_audio_board_service(a,&completed)==I9100_AP_DISABLED&&!completed);
 m.kill_stuck=0;m.pdma[0x100/4]=0;assert(!i9100_audio_board_close(&a));board_released();
 board_setup();assert(!open_board(&a));m.board_close_fail=1;
 assert(i9100_audio_board_close(&a)!=0&&a&&(m.pmu[0x3cc4/4]&7U)==7U&&m.pmu[0xa00/4]==0x12340900U);
 m.board_close_fail=0;assert(!i9100_audio_board_close(&a));board_released();
 board_setup();assert(!open_board(&a));m.interrupts=false;
 assert(i9100_audio_board_close(&a)==I9100_AP_BUSY&&a);m.interrupts=true;assert(!i9100_audio_board_close(&a));board_released();
 for(unsigned kind=1;kind<=2U;kind++){
  board_setup();assert(!open_board(&a));transactions=m.transactions;
  assert(i9100_audio_board_output_prepare(0,a,(i9100_mc1n2_output)kind,-30000)==I9100_MC_DISABLED&&m.transactions==transactions);
  assert(!i9100_audio_board_pcm_open(a,1536000));
  transactions=m.transactions;m.pdma[0x20/4]|=1U<<4;
  assert(i9100_audio_board_output_prepare(1,a,(i9100_mc1n2_output)kind,-30000)==I9100_MC_BUSY&&m.transactions==transactions);
  m.pdma[0x20/4]&=~(1U<<4);m.pdma[0x108/4]=1U;
  assert(i9100_audio_board_output_prepare(1,a,(i9100_mc1n2_output)kind,-30000)==I9100_MC_BUSY&&m.transactions==transactions);
  m.pdma[0x108/4]=0;m.cmu[0xc940/4]&=~1U;
  assert(i9100_audio_board_output_prepare(1,a,(i9100_mc1n2_output)kind,-30000)==I9100_MC_BUSY&&m.transactions==transactions);
  m.cmu[0xc940/4]|=1U;
  assert(!i9100_audio_board_output_prepare(1,a,(i9100_mc1n2_output)kind,-30000));
  assert(!i9100_audio_board_status(a,&status,0)&&status.output_owned&&status.output_powered&&!status.output_running);
  assert(i9100_audio_board_output_activate(a)==I9100_MC_NOT_READY);
  assert(!i9100_audio_board_start(a));
  m.pdma[0x100/4]=0;m.pdma[0x24/4]|=1U<<3;
  assert(!i9100_audio_board_service(a,&completed)&&completed==1);
  assert(i9100_audio_board_output_activate(a)==I9100_MC_NOT_READY); /* stopped hardware CS is insufficient */
  /* This fixture exposes a harness-driven register boundary, not a DMA VM:
   * report the next GO's EXECUTING state without altering production active
   * fields/counters. The separate output C suite executes the actual program. */
  m.pdma[0x100/4]=1U;
  assert(!i9100_audio_board_output_activate(a));
  assert(!i9100_audio_board_status(a,&status,0)&&status.output_running&&status.output_gain_mdb==-36000);
  assert((m.codec[4][89]&0x7fU)==66U&&(m.codec[4][90]&0x7fU)==66U);
  m.kill_stuck=1;
  assert(i9100_audio_board_close(&a)!=0&&a&&parent.parent_holds&&m.maps==8U);
  assert(!i9100_audio_board_status(a,&status,0)&&status.output_owned&&status.clock_owned&&status.domain_owned);
  assert(!(m.codec[4][89]&0x7fU)&&!(m.codec[4][90]&0x7fU));
  m.kill_stuck=0;m.pdma[0x100/4]=0;assert(!i9100_audio_board_close(&a));board_released();
 }
 board_setup();assert(!open_board(&a));assert(!i9100_audio_board_output_prepare(1,a,I9100_MC_HEADPHONES,-30000));
 m.board_tx_fail=m.transactions+1U;
 assert(i9100_audio_board_close(&a)==I9100_MC_IO&&a&&parent.parent_holds&&m.maps==5U);
 assert(!i9100_audio_board_status(a,&status,0)&&status.output_owned&&status.clock_owned&&status.domain_owned);
 m.board_tx_fail=0;assert(!i9100_audio_board_close(&a));board_released();
 /* Exact worker ownership excludes legacy mutators, a second worker, stale
  * generations and copied leases; it persists through live PCM cleanup. */
 board_setup();assert(!open_board(&a));i9100_audio_board_worker_lease owner={},foreign={};
 transactions=m.transactions;allocations=m.allocations;
 assert(!i9100_audio_board_worker_claim(a,&owner)&&owner.owned);
 assert(i9100_audio_board_worker_claim(a,&foreign)==I9100_AP_BUSY&&!foreign.owned);
 assert(i9100_audio_board_hold(a)==I9100_AP_BUSY&&i9100_audio_board_unhold(a)==I9100_AP_OWNER);
 assert(i9100_audio_board_pcm_open(a,1536000)==I9100_AP_BUSY&&i9100_audio_board_pcm_close(a)==I9100_AP_BUSY);
 assert(i9100_audio_board_output_prepare(1,a,I9100_MC_HEADPHONES,-30000)==I9100_AP_BUSY);
 assert(i9100_audio_board_start(a)==I9100_AP_BUSY&&i9100_audio_board_output_mute(a)==I9100_AP_BUSY);
 assert(m.transactions==transactions&&m.allocations==allocations);
 i9100_audio_board_worker_lease copied=owner;
 assert(i9100_audio_board_worker_pcm_open(&copied,1536000)==I9100_AP_OWNER);
 assert(i9100_audio_board_worker_release(&copied)==I9100_AP_OWNER);
 assert(!i9100_audio_board_worker_pcm_open(&owner,1536000));
 assert(i9100_audio_board_worker_release(&owner)==I9100_AP_BUSY&&owner.owned);
 assert(!i9100_audio_board_worker_pcm_close(&owner));uint32_t generation=owner.generation;
 assert(!i9100_audio_board_worker_release(&owner));assert(i9100_audio_board_worker_start(&owner)==I9100_AP_OWNER);
 assert(!i9100_audio_board_worker_claim(a,&owner)&&owner.generation!=generation);
 owner.generation=generation;assert(i9100_audio_board_worker_pcm_close(&owner)==I9100_AP_OWNER);
 owner.generation=generation+1U;assert(!i9100_audio_board_worker_release(&owner));
 assert(!i9100_audio_board_close(&a));board_released();
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
 puts("PASS real audio board+PCM/host IOKit: mute-before-MCLK/power-guarded pages/all setup failures; explicit output/actual native PCM guard/failed DMA and output close retain parent/clock/domain; no C93C writes");return 0;
}
#endif
