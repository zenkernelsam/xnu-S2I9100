/* SPDX-License-Identifier: BSD-2-Clause; actual board/workloop/codec/gated DMA. */
#define i9100_dma_native_allocate baseline_dma_allocate
#define I9100_WORKER_MODEL_ENTRY baseline_worker_entry
#include "i9100_audio_worker_test.cpp"
#undef i9100_dma_native_allocate
#include "i9100_audio_gated.h"
#include "i9100_ring_vm.h"
static i9100_ring_vm vm_state;
static uint32_t fifo_words[262144];
extern "C" int i9100_dma_native_allocate(int,uint32_t,void *,int (*)(void *),i9100_dma_native **);
int i9100_dma_native_allocate(int enabled,uint32_t bytes,void *engine,int (*idle_fn)(void *),i9100_dma_native **out)
{
 int e=baseline_dma_allocate(enabled,bytes,engine,idle_fn,out);
 if(!e&&m.ring_vm_mode){unsigned index=(*out)->index;assert(index<4);vm_state.buffer[index]=&(*out)->buffer;}
 return e;
}
extern "C" uint32_t __real_i9100_ring_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_ring_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_ring_mmio_read(volatile uint8_t *base,unsigned off)
{return base==(volatile uint8_t *)m.pdma?ring_vm_read(&vm_state,off):__real_i9100_ring_mmio_read(base,off);}
extern "C" void __wrap_i9100_ring_mmio_write(volatile uint8_t *base,unsigned off,uint32_t value)
{
 __real_i9100_ring_mmio_write(base,off,value);
 if(base==(volatile uint8_t *)m.pdma)ring_vm_write(&vm_state,off,value);
 else if(base==(volatile uint8_t *)board_i2s&&(board_i2s[0]&5U)==5U)ring_vm_run(&vm_state);
}
static void gated_board_setup(i9100_audio_board_native **board)
{
 assert(!event_objects);board_setup();m.ring_vm_mode=1;
 memset(&vm_state,0,sizeof(vm_state));vm_state.regs=m.pdma;vm_state.i2s=board_i2s;
 vm_state.fifo=fifo_words;vm_state.capacity=262144;
 m.pdma[0xe0c/4]|=1U<<12;m.pdma[0xe10/4]|=(1U<<3)|(1U<<4);
 i9100_audio_board_profile p=profile();p.use_gated_dma=1;
 assert(!i9100_audio_board_open(1,&parent,&p,board));
 event_allocations=event_fail=add_calls=add_fail=remove_fail=timer_fail=irq_mask_fail=irq_enable_fail=0;
}
static void gated_setup(i9100_audio_board_native **board,i9100_audio_worker **worker)
{
 gated_board_setup(board);
 assert(i9100_audio_worker_create(1,*board,nullptr,1,1,250,worker)==I9100_AP_ARGUMENT);
 assert(!i9100_audio_worker_create(1,*board,nullptr,0,0,250,worker));
}
static void finish_gated(i9100_audio_worker **worker,i9100_audio_board_native **board)
{
 assert(!i9100_audio_worker_destroy(worker)&&!*worker&&!event_objects);
 m.extra_contexts=0;assert(!i9100_audio_board_close(board));board_released();
 for(unsigned n=0;n<4;n++)vm_state.buffer[n]=nullptr;
 assert(!mprotect(board_audss,4096,PROT_READ|PROT_WRITE)&&!mprotect(board_i2s,4096,PROT_READ|PROT_WRITE));
}
#ifndef I9100_GATED_PIPELINE_ENTRY
#define I9100_GATED_PIPELINE_ENTRY main
#endif
int I9100_GATED_PIPELINE_ENTRY()
{
 i9100_audio_board_native *board=nullptr;i9100_audio_worker *worker=nullptr;
 gated_setup(&board,&worker);
 assert(!i9100_audio_worker_prepare(worker,1536000));m.extra_contexts=1;
 struct i9100_audio_worker_status status={};assert(!i9100_audio_worker_status(worker,&status)&&status.gated_dma&&status.last_pcm_valid&&status.last_pcm.prepared);
 assert(!i9100_audio_worker_output_prepare(1,worker,I9100_MC_SPEAKER,-30000));
 int16_t input[128];for(unsigned n=0;n<128;n++)input[n]=(int16_t)(n*491U);
 assert(!i9100_audio_worker_enqueue(worker,input,64));
 assert(!i9100_audio_worker_start(worker));assert(vm_state.words==64);
 for(unsigned n=0;n<64;n++)assert(fifo_words[n]==0);
 assert(i9100_audio_worker_output_activate(worker)==I9100_MC_NOT_READY);
 m.ns+=1000000ULL;last_loop->tick();
 assert(!i9100_audio_worker_status(worker,&status)&&!status.fault&&status.completed_periods==1&&status.last_pcm.completed_silence_periods==1);
 assert(status.staged_frames==64&&status.staged_consumed==0&&vm_state.words==128);
 assert(!i9100_audio_worker_output_activate(worker));
 for(unsigned turn=0;turn<128;turn++){
  assert(!i9100_audio_worker_enqueue(worker,input,64));
  m.ns+=1000000ULL;last_loop->tick();
  assert(!i9100_audio_worker_status(worker,&status)&&!status.fault&&status.running&&status.completed_periods==turn+2U);
 }
 assert((m.codec[4][89]&127U)==66U&&(m.codec[2][37]&31U)==1U&&vm_state.words==64U*130U);
 /* The next buffer was already zero-filled during muted pre-roll. It stays
  * silent until its later fence; do not rewrite that live DEVICE buffer. */
 for(unsigned word=0;word<192;word++)assert(fifo_words[word]==0);
 for(unsigned word=192;word<vm_state.words;word++){
  unsigned n=word%64U;uint16_t left=(uint16_t)((int32_t)input[n*2U]*1036/32768);
  uint16_t right=(uint16_t)((int32_t)input[n*2U+1U]*1036/32768);
  assert(fifo_words[word]==((uint32_t)right<<16|left));
 }
 assert(!i9100_audio_worker_stop(worker));assert(!(board_i2s[0]&7U)&&!m.dma_count);
 assert(!i9100_audio_worker_status(worker,&status)&&!status.last_pcm.started&&!status.last_pcm.prepared&&!status.last_pcm.held_mask);
 finish_gated(&worker,&board);
 /* A partial frame is delivered once with a real zero-filled tail. */
 gated_setup(&board,&worker);assert(!i9100_audio_worker_prepare(worker,1536000));m.extra_contexts=1;
 assert(!i9100_audio_worker_start(worker));input[0]=32767;input[1]=-32768;
 assert(!i9100_audio_worker_enqueue(worker,input,1));assert(!i9100_audio_worker_flush(worker));
 m.ns+=1000000ULL;last_loop->tick();m.ns+=1000000ULL;last_loop->tick();
 assert(fifo_words[128]==((uint32_t)(uint16_t)-1036<<16|1035U));for(unsigned n=129;n<192;n++)assert(fifo_words[n]==0);
 finish_gated(&worker,&board);
 /* Failed worker scheduling and failed DMA stop keep actual owner/pins. */
 gated_setup(&board,&worker);assert(!i9100_audio_worker_prepare(worker,1536000));m.extra_contexts=1;
 assert(!i9100_audio_worker_start(worker));timer_fail=1;m.ns+=1000000ULL;last_loop->tick();
 assert(!i9100_audio_worker_status(worker,&status)&&status.fault&&!status.running&&!(board_i2s[0]&7U));
 timer_fail=0;finish_gated(&worker,&board);
 gated_setup(&board,&worker);assert(!i9100_audio_worker_prepare(worker,1536000));m.extra_contexts=1;
 assert(!i9100_audio_worker_start(worker));vm_state.kill_stuck=1;
 assert(i9100_audio_worker_stop(worker)!=0&&m.dma_count&&parent.parent_holds);
 assert(i9100_audio_board_close(&board)==I9100_AP_BUSY&&board);
 vm_state.kill_stuck=0;finish_gated(&worker,&board);
 puts("PASS actual board -> workloop -> gated PCM -> native ring -> ISA VM: zero pre-roll, low output qualification, 128 periods, partial tail, timer fault and KILL ownership retention");return 0;
}
