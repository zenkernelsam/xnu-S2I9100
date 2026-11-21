/* SPDX-License-Identifier: BSD-2-Clause
 * Real MFC board/native/IRQ-registry/worker control flow driven by an
 * explicit host facade: GIC dispatch, firmware replies, timer and workloop
 * thread are modeled; no codec execution or real interrupt delivery. */
#include <stdio.h>
#include "i9100_mfc_worker_facade.h"
#include "i9100_mfc_worker.h"
static const struct i9100_mfc_power_config MFC_CFG={1,24000000U,200000000U};
static uint8_t mfc_fw[262144];
static const uint8_t mfc_stream[]={0,0,0,1,0x67,0x42,0,0x1e,0xf4,0x05,0x01,0xec,0x80};
static struct i9100_mfc_firmware_profile mfc_profile()
{struct i9100_mfc_firmware_profile f={};for(unsigned i=0;i<sizeof(mfc_fw);i++)mfc_fw[i]=(uint8_t)(i*13U+7U);
 f.board_data_reviewed=1;f.system_bytes=0x80000;f.firmware_bytes=sizeof(mfc_fw);f.expected_version=0x211116;f.timeout_ms=500;
 assert(SHA256(mfc_fw,sizeof(mfc_fw),f.sha256));return f;}
struct mfc_done {std::atomic<unsigned> calls{0};std::atomic<int> last{0};std::atomic<uint64_t> token{0};std::atomic<unsigned> order[16];};
static volatile int mfc_last_res;
static void mfc_on_done(void *p,uint64_t token,int result)
{auto *d=(mfc_done *)p;unsigned n=d->calls++;d->token=token;d->last=result;if(n<16)d->order[n]=(unsigned)token;mfc_last_res=result;}
static struct i9100_mfc_worker *mfc_dbg_w;static struct i9100_mfc_native *mfc_dbg_n;
/* Level-triggered reality: a reply asserted while the leased line sits masked
 * stays pending in the GIC and dispatches the moment the worker re-arms; if a
 * dispatch is genuinely lost the bounded timer fallback still polls the latch
 * away. The registry model drops masked dispatches, so retry while the latch
 * remains asserted. */
static void mfc_irq_reply(unsigned r,unsigned arg=0,bool wait_pending=true)
{
 /* A firmware answer cannot precede its command: wait until the deferred
  * worker has really issued the request into the mailbox (pending) before
  * posting the reply, or the stale R2H makes quiet() defer forever. Tests
  * that deliberately fabricate a stale latch pass wait_pending=false. */
 if(wait_pending){struct i9100_mfc_native_status st={};unsigned p=0;int se=0;
  for(;p<4000;p++){se=i9100_mfc_native_status(mfc_dbg_n,&st);if(!se&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(p>=4000){
   struct i9100_mfc_worker_status ws;struct i9100_mfc_native_status nst;
   i9100_mfc_native_status(mfc_dbg_n,&nst);
   int we=mfc_dbg_w?i9100_mfc_worker_snapshot(mfc_dbg_w,&ws):-1;if(we)memset(&ws,0,sizeof(ws));
   fprintf(stderr,"WAITPEND-STUCK r=%u w=%p: sub=%llu cmp=%llu can=%llu def=%llu tim=%llu act=%u queued=%u armed=%u err=%d fault=%u phase=%u pending=%u nerr=%d latch=%x lastcb=%d\n",
    r,(void *)mfc_dbg_w,
    (unsigned long long)ws.submitted,(unsigned long long)ws.completed,(unsigned long long)ws.cancelled,
    (unsigned long long)ws.defers,(unsigned long long)ws.timeouts,ws.in_flight,ws.queued,ws.armed,
    ws.error,ws.fault,nst.phase,nst.pending,nst.error,mfc_regs[MF_MFC][8/4],mfc_last_res);}
  assert(p<4000);}
 mfc_response(r,arg);
 /* Delivered means consumed: either the leased handler took the dispatch, or
  * the bounded timer fallback already polled the latch away (lost-IRQ
  * recovery). A masked leased line leaves a level source pending in the GIC
  * instead — the owner's deferred drain or next arm consumes it, and this
  * doorbell must not pretend a masked dispatch reached the handler. */
 unsigned i=0;while(i<4000&&(mfc_regs[MF_MFC][8/4]&1U)&&!mfc_dispatch_irq126())
 {if(!(mfc_regs[MF_GIC][0x10c/4]&(1U<<30))&&++i>=64)break;
  i++;mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(i>=4000){
  struct i9100_mfc_worker_status ws;struct i9100_mfc_native_status nst;
  int ne=mfc_dbg_n?i9100_mfc_native_status(mfc_dbg_n,&nst):-1;
  int we=mfc_dbg_w?i9100_mfc_worker_snapshot(mfc_dbg_w,&ws):-1;if(we)memset(&ws,0,sizeof(ws));
  unsigned en=99;for(unsigned s=0;s<I9100_IRQ_SLOTS;s++)if(mfc_parent.irqs.slots[s].irq==126)en=mfc_parent.irqs.slots[s].enabled;
  fprintf(stderr,"REPLY %u STUCK caller=%p: sub=%llu cmp=%llu can=%llu def=%llu tim=%llu act=%u queued=%u armed=%u err=%d fault=%u irq=%u native=%d phase=%u pending=%u nerr=%d gic=%x sloten=%u latch=%x\n",
   r,__builtin_return_address(0),(unsigned long long)ws.submitted,(unsigned long long)ws.completed,
   (unsigned long long)ws.cancelled,(unsigned long long)ws.defers,(unsigned long long)ws.timeouts,
   ws.in_flight,ws.queued,ws.armed,ws.error,ws.fault,ws.irq_bound,ne<0?-9:0,nst.phase,nst.pending,nst.error,
   mfc_regs[MF_GIC][0x10c/4],en,mfc_regs[MF_MFC][8/4]);
 }
 assert(i<4000); /* pending reply must eventually reach the leased handler */
}
static struct i9100_mfc_worker *mfc_re_w;static mfc_done mfc_re2;
static void mfc_reenter_done(void *p,uint64_t token,int result)
{
 auto *d=(mfc_done *)p;d->calls++;d->last=result;d->token=token;
 if(token==600){int e=i9100_mfc_worker_frame(mfc_re_w,&mfc_re2,601,mfc_stream,sizeof(mfc_stream),4,0,mfc_on_done);assert(!e);}
}
static struct i9100_mfc_worker *mfc_sd_w;static int mfc_sd_stop=99,mfc_sd_destroy=99;
static void mfc_stop_destroy_done(void *p,uint64_t token,int result)
{
 auto *d=(mfc_done *)p;d->calls++;d->last=result;d->token=token;
 mfc_sd_stop=i9100_mfc_worker_stop(mfc_sd_w);
 struct i9100_mfc_worker *x=mfc_sd_w;mfc_sd_destroy=i9100_mfc_worker_destroy(&x);
}
static bool mfc_wait_phase(struct i9100_mfc_native *n,uint32_t phase,unsigned ms=4000)
{
 struct i9100_mfc_native_status st;
 for(unsigned i=0;i<ms;i++){
  if(!i9100_mfc_native_status(n,&st)&&(st.phase==phase||st.phase==I9100_MFC_N_READY))return true;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return false;
}
static bool mfc_wait_done(mfc_done &d,unsigned ms=4000)
{
 for(unsigned i=0;i<ms;i++){if(d.calls)return true;mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(mfc_dbg_w&&mfc_dbg_n){struct i9100_mfc_worker_status ws;struct i9100_mfc_native_status nst;
  int we=i9100_mfc_worker_snapshot(mfc_dbg_w,&ws),ne=i9100_mfc_native_status(mfc_dbg_n,&nst);
  if(!we)fprintf(stderr,"DONE-STUCK sub=%llu cmp=%llu def=%llu tim=%llu act=%u q=%u armed=%u err=%d fault=%u ph=%u pend=%u nerr=%d latch=%x r2h=%x\n",
   (unsigned long long)ws.submitted,(unsigned long long)ws.completed,(unsigned long long)ws.defers,
   (unsigned long long)ws.timeouts,ws.in_flight,ws.queued,ws.armed,ws.error,ws.fault,
   ne<0?99U:nst.phase,ne<0?9U:nst.pending,ne<0?-9:nst.error,mfc_regs[MF_MFC][8/4],mfc_regs[MF_MFC][0x44/4]);}
 return false;
}
static i9100_mfc_native *open_engine(struct i9100_mfc_board **board_out)
{
 mfc_setup();struct i9100_mfc_board *b=nullptr;assert(!i9100_mfc_board_open(1,&mfc_parent,&MFC_CFG,&b));
 struct i9100_mfc_supply s={};assert(!i9100_mfc_board_supply(b,&s));
 struct i9100_mfc_decode_config dc={1,8388608,2097152};struct i9100_mfc_native *n=nullptr;
 assert(!i9100_mfc_native_open_decode(1,&mfc_parent,&s,&dc,&n));*board_out=b;return n;
}
static bool mfc_wait_native(struct i9100_mfc_native *n,unsigned ms,bool (*pred)(const struct i9100_mfc_native_status *))
{struct i9100_mfc_native_status st;for(unsigned i=0;i<ms;i++){if(!i9100_mfc_native_status(n,&st)&&pred(&st))return true;
 std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;}
static bool st_draining(const struct i9100_mfc_native_status *s){return s->draining&&!s->drained;}
/* A real firmware frame reply. display=1 additionally presents slot `slot`
 * (its shared tag must match the submitted tag); display=0 decodes into
 * slot `slot` without presenting. */
static void frame_reply(unsigned consumed,unsigned display=0,unsigned slot=0,unsigned tag=1)
{
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x201c/4]=display;mfc_regs[MF_MFC][0x202c/4]=0;
 mfc_regs[MF_MFC][0x2020/4]=1;mfc_regs[MF_MFC][0x2018/4]=consumed;
 mfc_regs[MF_MFC][0x2024/4]=(0x48000000U>>11)+mfc_regs[MF_MFC][(0x700+4*slot)/4];
 mfc_regs[MF_MFC][0x2028/4]=(0x46000000U>>11)+mfc_regs[MF_MFC][(0x600+4*slot)/4];
 if(display){
  mfc_regs[MF_MFC][0x2010/4]=mfc_regs[MF_MFC][0x2024/4];mfc_regs[MF_MFC][0x2014/4]=mfc_regs[MF_MFC][0x2028/4];
  uint32_t *sh=(uint32_t *)(mfc_bank_a+0x116000U);sh[8/4]=tag;sh[0xc0/4]=4;
 }
}
/* Drive the production CMD9→8→1→4→15 sequence over real IRQ dispatch. */
static void boot_engine(struct i9100_mfc_worker *w,struct i9100_mfc_native *n,mfc_done &d)
{
 auto pf=mfc_profile();
 assert(!i9100_mfc_worker_boot(w,&d,100,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 assert(mfc_wait_phase(n,I9100_MFC_N_BOOT));
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 mfc_irq_reply(1,2);assert(mfc_wait_phase(n,I9100_MFC_N_HEADER));
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x2008/4]=640;mfc_regs[MF_MFC][0x2004/4]=480;
 mfc_regs[MF_MFC][0x200c/4]=4;mfc_regs[MF_MFC][0x2018/4]=sizeof(mfc_stream);
 mfc_irq_reply(4);assert(mfc_wait_phase(n,I9100_MFC_N_BUFFERS));
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_irq_reply(15);
 assert(mfc_wait_done(d));assert(d.last==0&&d.token==100);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.phase==I9100_MFC_N_READY);
}
static void close_all(struct i9100_mfc_board *b,struct i9100_mfc_native *n,struct i9100_mfc_worker *w)
{
 int de=i9100_mfc_worker_destroy(&w);
 if(de){struct i9100_mfc_native_status q;i9100_mfc_native_status(n,&q);
  struct i9100_mfc_worker_status x;i9100_mfc_worker_snapshot(w,&x);
  fprintf(stderr,"CLOSE destroy=%d w=%p pend=%u leased=%x nerr=%d ncerr=%d ph=%u en=%u sub=%llu cmp=%llu werr=%d wcerr=%d stage=%u r2h=%x latch=%x gicact=%x mc=%x rst=%x\n",
   de,(void *)w,q.pending,q.leased,q.error,q.cleanup_error,q.phase,x.armed,(unsigned long long)x.submitted,(unsigned long long)x.completed,
   x.error,x.cleanup_error,q.stop_stage,mfc_regs[MF_MFC][0x44/4],mfc_regs[MF_MFC][8/4],mfc_regs[MF_GIC][0x30c/4],
   mfc_regs[MF_MFC][0x510/4],mfc_regs[MF_MFC][0/4]);}
 assert(!de&&!w);
 int ce=i9100_mfc_native_close(&n);
 if(ce){struct i9100_mfc_native_status q;i9100_mfc_native_status(n,&q);
  fprintf(stderr,"CLOSE native=%d n=%p ph=%u pend=%u nerr=%d cerr=%d holds=%u r2h=%x latch=%x gic=%x\n",
   ce,(void *)n,q.phase,q.pending,q.error,q.cleanup_error,mfc_parent.holds,
   mfc_regs[MF_MFC][0x44/4],mfc_regs[MF_MFC][8/4],mfc_regs[MF_GIC][0x10c/4]);}
 assert(!ce&&!n);
 assert(!i9100_mfc_board_close(&b)&&!b);
 mfc_dbg_w=nullptr;mfc_dbg_n=nullptr; /* globals must not outlive the engine */
 assert(!m.maps&&!m.descriptors&&!m.contexts&&!m.locks&&!m.objects&&!m.dma&&!mfc_parent.holds);
}
/* Async boot pipeline, real IRQ dispatch path, mask/rearm, lease lifecycle. */
static void integration()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 /* Raw callers cannot seize the engine or board while the worker holds them. */
 assert(i9100_mfc_native_close(&n)==I9100_MFC_BUSY&&n);
 assert(i9100_mfc_board_close(&b)==I9100_MP_BUSY&&b);
 mfc_done d;boot_engine(w,n,d);
 /* Frame: non-inline submit; input copied before caller mutation. */
 mfc_done f;uint8_t chunk[sizeof(mfc_stream)];memcpy(chunk,mfc_stream,sizeof(chunk));
 assert(!i9100_mfc_worker_frame(w,&f,201,chunk,sizeof(chunk),1,0,mfc_on_done));
 memset(chunk,0xaa,sizeof(chunk)); /* mutating source must not corrupt the queued copy */
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
 frame_reply(sizeof(mfc_stream),1);mfc_irq_reply(5);
 assert(mfc_wait_done(f));assert(f.last==0);
 /* The queued copy reached the codec, not the mutated source. */
 bool found=false;for(unsigned off=0;off+sizeof(mfc_stream)<=8388608U;off++)
  if(!memcmp(mfc_bank_a+off,mfc_stream,sizeof(mfc_stream))){found=true;break;}
 assert(found);
 /* Output frame lease is kept, not auto-discarded. */
 struct i9100_mfc_frame_lease lease={};assert(!i9100_mfc_worker_frame_lease(w,&lease)&&lease.owned);
 assert(!i9100_mfc_worker_frame_return(w,&lease));
 struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
 assert(ws.interrupts>=5&&ws.completed>=2&&m.eois>=5);
 close_all(b,n,w);fflush(0);fprintf(stderr,"integration ok\n");
}
/* FIFO ordering, bounded queue backpressure, cancel exactly-once. */
static void ordering()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 /* Fill the queue: one in flight + up to 7 queued, then the next is BUSY. */
 mfc_done f[16];unsigned ok=0,busy=0;
 for(unsigned i=0;i<16;i++){int e=i9100_mfc_worker_frame(w,&f[i],300+i,mfc_stream,sizeof(mfc_stream),i+1,0,mfc_on_done);
  if(!e)ok++;else{assert(e==I9100_MFC_BUSY);busy++;}}
 assert(ok==I9100_MFC_WORKER_QUEUE&&busy==16-I9100_MFC_WORKER_QUEUE);
 /* Cancel a queued request; it completes exactly once as aborted. */
 i9100_mfc_worker_cancel(w,&f[3],303);
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(f[0]));assert(f[0].last==0);
 assert(mfc_wait_done(f[3]));assert(f[3].last==I9100_MFC_ABORTED&&f[3].calls==1);
 /* Drain the rest through real replies; FIFO order in completion tags. */
 for(unsigned i=1;i<ok;i++){
  if(f[i].calls)continue;
  frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
  assert(mfc_wait_done(f[i]));assert(f[i].last==0);
 }
 struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
 assert(ws.completed==ok+1&&ws.cancelled==1&&ws.queued==0&&!ws.in_flight); /* +1: boot request */
 int se=i9100_mfc_worker_stop(w);
 if(se){struct i9100_mfc_native_status q;i9100_mfc_native_status(n,&q);unsigned en=99;
  for(unsigned i=0;i<I9100_IRQ_SLOTS;i++)if(mfc_parent.irqs.slots[i].irq==126)en=mfc_parent.irqs.slots[i].enabled;
  fprintf(stderr,"STOP ordering se=%d pending=%u exp=%u last=%u raw=%x err=%d ph=%u sub=%u comp=%u drain=%u/%u gic=%x slot126en=%u\n",
   se,q.pending,q.expected,q.last_reply,q.raw_interrupt,q.error,q.phase,q.frame_submitted,q.frame_completed,
   q.draining,q.drained,mfc_regs[MF_GIC][0x10c/4],en);}
 assert(!se);
 close_all(b,n,w);fflush(0);fprintf(stderr,"ordering ok\n");
}
/* Lost/spurious/stale IRQs, no-completion-bit, timeout, late notify. */
static void interrupt_edges()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 /* Spurious dispatch while armed but the MFC latch is clear: no notify. */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,400,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 struct i9100_mfc_worker_status ws;
 for(unsigned i=0;i<400;i++){if(!i9100_mfc_worker_snapshot(w,&ws)&&ws.armed)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert((mfc_regs[MF_GIC][0x10c/4]&(1U<<30))!=0); /* armed for the command */
 assert(ws.armed);
 uint64_t ints=ws.interrupts;
 assert(mfc_dispatch_irq126()==1); /* latch clear: counted spurious, no notify */
 assert(!(mfc_regs[MF_MFC][8/4]&1U));
 /* Lost IRQ: reply lands without dispatch; bounded defers must recover. */
 frame_reply(sizeof(mfc_stream));mfc_response(5);
 for(unsigned i=0;i<80&&!f.calls;i++){mfc_advance_us(3000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(f.calls&&f.last==0);
 assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.defers>0&&ws.interrupts==ints);
 /* Stale notify after completion: while the leased line stays masked the
  * level source simply remains pending in the GIC — no fabricated wake may
  * consume it. The next command's own drain acknowledges it. */
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5,0,false);
 assert(mfc_regs[MF_MFC][8/4]&1U);
 {assert(!i9100_mfc_worker_snapshot(w,&ws));assert(!ws.late_irq);}
 /* Timeout: a command whose issue keeps seeing stale mailbox state hits
  * its bounded deadline; its own deferred drain acknowledged the latch. */
 mfc_done t;assert(!i9100_mfc_worker_frame(w,&t,401,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done));
 for(unsigned i=0;i<600&&!(t.calls&&ws.late_irq>=1);i++){assert(!i9100_mfc_worker_snapshot(w,&ws));
  mfc_advance_us(3000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(t.calls&&t.last==I9100_MFC_TIMEOUT);
 /* ws is a lagging snapshot: refresh it so the drain counter and the
  * completion total are read after t's own retirement. */
 assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.late_irq>=1&&ws.completed==3&&f.calls==1);
 assert(ws.timeouts>=1);
 /* The firmware eventually answers the dead command late; the stale-notify
  * drain must consume it so the engine returns quiescent before close. */
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5,0,false);
 for(unsigned i=0;i<400;i++){struct i9100_mfc_native_status q;
  if(!i9100_mfc_native_status(n,&q)&&!q.pending)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_native_status q;assert(!i9100_mfc_native_status(n,&q)&&!q.pending);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"interrupt_edges ok\n");
}
/* Half-consumption, continuation, tags in order, real drain-to-EMPTY EOS. */
static void stream_semantics()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 static uint8_t half[64];for(unsigned i=0;i<64;i++)half[i]=(uint8_t)(i*3U+1U);
 half[0]=0;half[1]=0;half[2]=1; /* real NAL start code required by the decoder */
 /* Half consumption: firmware reports only part of the chunk consumed. */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,500,half,sizeof(half),1,0,mfc_on_done));
 frame_reply(sizeof(half)/2);mfc_irq_reply(5);
 assert(mfc_wait_done(f));assert(f.last==0);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.consumed_bytes==sizeof(half)/2);
 /* The CPB tail continues with a zero-byte request on the same stream. */
 mfc_done cont;assert(!i9100_mfc_worker_frame(w,&cont,501,0,0,0,0,mfc_on_done));
 frame_reply(sizeof(half)/2);mfc_irq_reply(5);
 assert(mfc_wait_done(cont)&&cont.last==0);
 assert(!i9100_mfc_native_status(n,&st)&&st.consumed_bytes==sizeof(half));
 /* Tags must keep increasing; out-of-order presentation stays firmware-side. */
 mfc_done f2,f3;
 assert(!i9100_mfc_worker_frame(w,&f2,502,half,sizeof(half),2,0,mfc_on_done));
 frame_reply(sizeof(half));mfc_irq_reply(5);assert(mfc_wait_done(f2)&&f2.last==0);
 assert(!i9100_mfc_worker_frame(w,&f3,503,half,sizeof(half),3,0,mfc_on_done));
 frame_reply(sizeof(half));mfc_irq_reply(5);assert(mfc_wait_done(f3)&&f3.last==0);
 /* EOS: LAST then drain continuation until an actual EMPTY response. */
 mfc_done eos;assert(!i9100_mfc_worker_frame(w,&eos,504,half,sizeof(half),4,1,mfc_on_done));
 frame_reply(sizeof(half));mfc_irq_reply(5); /* consume input, still draining */
 assert(mfc_wait_native(n,4000,st_draining));assert(!eos.calls); /* not done before EMPTY */
 for(unsigned i=0;i<400&&!eos.calls;i++){
  /* Advance model time so the deferred drain continuation issues, then the
   * firmware answers it with the real EMPTY status. Reply only while a
   * mailbox command actually waits for one: an EMPTY answer can retire the
   * last drain between the loop check and the post. */
  mfc_advance_us(3000);std::this_thread::sleep_for(std::chrono::milliseconds(2));
  struct i9100_mfc_native_status q;if(!i9100_mfc_native_status(n,&q)&&q.pending){frame_reply(0,3);mfc_irq_reply(5);}
 }
 assert(eos.calls&&eos.last==0);
 assert(!i9100_mfc_native_status(n,&st)&&st.drained);
 close_all(b,n,w);fflush(0);fprintf(stderr,"stream_semantics ok\n");
}
/* Every DPB slot leased: frame submit backpressures on a real release wake. */
static void dpb_backpressure()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.dpb_count&&st.dpb_count<16U);
 unsigned count=st.dpb_count;struct i9100_mfc_frame_lease leases[16]={};
 /* Present a distinct slot per frame until every DPB slot is display-ready. */
 for(unsigned k=0;k<count;k++){
  mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,700+k,mfc_stream,sizeof(mfc_stream),10+k,0,mfc_on_done));
  frame_reply(sizeof(mfc_stream),1,k,10+k);mfc_irq_reply(5);
  assert(mfc_wait_done(f)&&f.last==0);
  /* Caller-side retry: the worker's idle drain may still hold the engine
   * gate for a short window after the done callback. */
  int le;unsigned tries=0;do{le=i9100_mfc_worker_frame_lease(w,&leases[k]);tries++;}
  while(le==I9100_MFC_BUSY&&tries<4000&&(mfc_advance_us(2000),std::this_thread::sleep_for(std::chrono::milliseconds(1)),true));
  if(le){struct i9100_mfc_native_status q;i9100_mfc_native_status(n,&q);
   fprintf(stderr,"LEASE k=%u le=%d tries=%u ph=%u pend=%u nerr=%d drdy=%x leased=%x sub=%u comp=%u\n",
    k,le,tries,q.phase,q.pending,q.error,q.display_ready,q.leased,q.frame_submitted,q.frame_completed);}
  assert(!le&&leases[k].owned);
 }
 assert(i9100_mfc_worker_frame_lease(w,&leases[15])==I9100_MFC_BUSY); /* nothing left */
 /* The next frame cannot issue: it waits for an actual lease release. */
 mfc_done bp;assert(!i9100_mfc_worker_frame(w,&bp,800,mfc_stream,sizeof(mfc_stream),20,0,mfc_on_done));
 for(unsigned i=0;i<200;i++){if(bp.calls)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(!bp.calls); /* still backpressured, not fabricated */
 struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.queued==1&&!ws.in_flight);
 /* Returning one lease wakes the queue; the frame then runs for real. */
 {int re;unsigned tries=0;do{re=i9100_mfc_worker_frame_return(w,&leases[0]);tries++;}
  while(re==I9100_MFC_BUSY&&tries<4000&&(mfc_advance_us(2000),std::this_thread::sleep_for(std::chrono::milliseconds(1)),true));
  assert(!re);}
 for(unsigned i=0;i<400&&!ws.in_flight;i++){assert(!i9100_mfc_worker_snapshot(w,&ws));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(bp)&&bp.last==0);
 for(unsigned k=1;k<count;k++){
  int re;unsigned tries=0;do{re=i9100_mfc_worker_frame_return(w,&leases[k]);tries++;}
  while(re==I9100_MFC_BUSY&&tries<4000&&(mfc_advance_us(2000),std::this_thread::sleep_for(std::chrono::milliseconds(1)),true));
  assert(!re);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"dpb_backpressure ok\n");
}
/* Post a firmware reply then poll until it is consumed (R2H cleared).
 * native_poll reports BUSY while boot phases still advance: consumption,
 * not a zero return, is the success criterion. */
static void poll_reply(struct i9100_mfc_native *n,unsigned r,unsigned arg=0)
{
 mfc_response(r,arg);
 unsigned i=0;for(;i<4000&&mfc_regs[MF_MFC][0x44/4];i++){
  int e=i9100_mfc_native_poll(n);assert(e==0||e==I9100_MFC_BUSY);mfc_advance_us(2000);}
 assert(i<4000);
}
/* Polling path stays identical when no worker/IRQ owner exists. */
static void polling_compat()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 auto pf=mfc_profile();
 assert(!i9100_mfc_native_start(n,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream)));
 /* Replies land but nothing dispatches: direct polling consumes them. */
 poll_reply(n,9);
 poll_reply(n,8,0x60000);
 poll_reply(n,1,2);
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x2008/4]=640;mfc_regs[MF_MFC][0x2004/4]=480;
 mfc_regs[MF_MFC][0x200c/4]=4;mfc_regs[MF_MFC][0x2018/4]=sizeof(mfc_stream);
 poll_reply(n,4);
 mfc_regs[MF_MFC][0x2000/4]=2;poll_reply(n,15);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.phase==I9100_MFC_N_READY);
 assert(!(mfc_regs[MF_GIC][0x10c/4]&(1U<<30))); /* IRQ126 stayed masked */
 assert(!i9100_mfc_native_frame(n,mfc_stream,sizeof(mfc_stream),1,0));
 frame_reply(sizeof(mfc_stream),1,0,1);poll_reply(n,5);
 struct i9100_mfc_frame_lease lease={};assert(!i9100_mfc_native_acquire(n,&lease)&&lease.owned);
 assert(!i9100_mfc_native_release(n,&lease));
 assert(!i9100_mfc_native_close(&n)&&!n);assert(!i9100_mfc_board_close(&b)&&!b);
 assert(!m.maps&&!m.descriptors&&!m.contexts&&!m.locks&&!m.objects&&!m.dma&&!mfc_parent.holds);
 fflush(0);fprintf(stderr,"polling_compat ok\n");
}
/* Callback reentry into submit, stop refusal of raw close, retained failure. */
static void reentry_and_failures()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 /* A done callback reenters submit: the second frame is queued, not run inline. */
 static mfc_done re1;mfc_re_w=w;
 assert(!i9100_mfc_worker_frame(w,&re1,600,mfc_stream,sizeof(mfc_stream),3,0,mfc_reenter_done));
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(re1)&&re1.last==0);
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(mfc_re2)&&mfc_re2.last==0);
 /* Stop refuses while an unfinished command still owns the mailbox. */
 mfc_done s;assert(!i9100_mfc_worker_frame(w,&s,602,mfc_stream,sizeof(mfc_stream),5,0,mfc_on_done));
 std::this_thread::sleep_for(std::chrono::milliseconds(10));
 for(unsigned i=0;i<600&&!s.calls;i++){mfc_advance_us(3000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(s.calls==1&&s.last==I9100_MFC_TIMEOUT); /* timed out exactly once */
 /* A late firmware answer to the dead command is drained by the stale-notify
  * path; stop must then be able to prove mailbox quiescence. */
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5,0,false);
 for(unsigned i=0;i<400;i++){struct i9100_mfc_native_status q;
  if(!i9100_mfc_native_status(n,&q)&&!q.pending)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 int se=i9100_mfc_worker_stop(w);assert(!se);
 close_all(b,n,w);
 /* Construction-failure sweep: every allocation point cleans up. */
 for(unsigned point=1;point<=8;point++){
  struct i9100_mfc_board *bb;struct i9100_mfc_native *nn=open_engine(&bb);
  unsigned b_obj=m.objects,b_ctx=m.contexts,b_locks=m.locks;
  struct i9100_mfc_worker *ww=nullptr;mfc_fail_at=point;
  int e=i9100_mfc_worker_create(1,nn,bb,&ww);
  if(!e)assert(!i9100_mfc_worker_destroy(&ww)&&!ww);
  else assert(!ww); /* create's own cleanup path released the partial worker */
  if(m.objects!=b_obj||m.contexts!=b_ctx||m.locks!=b_locks)
   fprintf(stderr,"SWEEP point=%u e=%d obj=%u/%u ctx=%u/%u locks=%u/%u\n",
    point,e,m.objects,b_obj,m.contexts,b_ctx,m.locks,b_locks);
  assert(m.objects==b_obj&&m.contexts==b_ctx&&m.locks==b_locks); /* no net worker leak */
  assert(!i9100_mfc_native_close(&nn));assert(!i9100_mfc_board_close(&bb));
 }
 /* A done callback may reenter stop/destroy: both are clean refusals from
  * the gate-owning thread, never teardown under its own gate. */
 static mfc_done sd;
 {struct i9100_mfc_board *sb;struct i9100_mfc_native *sn=open_engine(&sb);
  struct i9100_mfc_worker *sw=nullptr;assert(!i9100_mfc_worker_create(1,sn,sb,&sw));
  mfc_sd_w=sw;mfc_dbg_w=sw;mfc_dbg_n=sn;
  mfc_done sd_boot;boot_engine(sw,sn,sd_boot);
  assert(!i9100_mfc_worker_frame(sw,&sd,810,mfc_stream,sizeof(mfc_stream),2,0,mfc_stop_destroy_done));
  frame_reply(sizeof(mfc_stream),1,0,2);mfc_irq_reply(5);
  for(unsigned i=0;i<4000&&!sd.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(sd.calls==1&&mfc_sd_stop!=0&&mfc_sd_destroy!=0);
  mfc_sd_w=nullptr;close_all(sb,sn,sw);}
 /* Late EOI retention: the provider claims the line active and the handler
  * masks+notifies, but the acknowledge completion is still pending, so
  * release and destroy refuse until the real EOI lands; an explicit retry
  * then completes every teardown stage. */
 {struct i9100_mfc_board *rb;struct i9100_mfc_native *rn=open_engine(&rb);
  struct i9100_mfc_worker *rw=nullptr;assert(!i9100_mfc_worker_create(1,rn,rb,&rw));
  mfc_dbg_w=rw;mfc_dbg_n=rn;mfc_done gd;boot_engine(rw,rn,gd);
  mfc_done rf;assert(!i9100_mfc_worker_frame(rw,&rf,900,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
  mfc_eoi_deferred=true;frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
  mfc_eoi_deferred=false;mfc_complete_eoi();
  assert(mfc_wait_done(rf)&&rf.last==0);
  /* Park a genuinely in-flight dispatch/EOI window across destroy. */
  mfc_eoi_deferred=true;mfc_response(9);
  for(unsigned i=0;i<4000&&(mfc_regs[MF_MFC][8/4]&1U)&&!mfc_dispatch_irq126();i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(!(mfc_regs[MF_GIC][0x30c/4]&(1U<<30)))mfc_dispatch_irq126(); /* drain may win; the claim still must happen */
  assert(mfc_regs[MF_GIC][0x30c/4]&(1U<<30)); /* claimed active, EOI genuinely deferred */
  int de=i9100_mfc_worker_destroy(&rw);
  assert(de!=0&&rw); /* retained: active window unproven, ownership kept */
  assert(i9100_mfc_native_close(&rn)==I9100_MFC_BUSY);
  assert(i9100_mfc_board_close(&rb)==I9100_MP_BUSY);
  mfc_eoi_deferred=false;mfc_complete_eoi();
  int re=i9100_mfc_worker_destroy(&rw);
  assert(!re&&!rw); /* explicit cleanup retry after actual quiescence */
  assert(!i9100_mfc_native_close(&rn));assert(!i9100_mfc_board_close(&rb));
  mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;}
 /* Provider-failure retention: a refused event-source removal preserves
  * ownership with the original error; clearing the fault lets the same
  * explicit retry finish every teardown stage without a stale entry. */
 {struct i9100_mfc_board *pb;struct i9100_mfc_native *pn=open_engine(&pb);
  struct i9100_mfc_worker *pw=nullptr;assert(!i9100_mfc_worker_create(1,pn,pb,&pw));
  mfc_dbg_w=pw;mfc_dbg_n=pn;mfc_done pd;boot_engine(pw,pn,pd);
  mfc_remove_fail=true;int pe=i9100_mfc_worker_destroy(&pw);assert(pe!=0&&pw);
  mfc_remove_fail=false;assert(!i9100_mfc_worker_destroy(&pw)&&!pw);
  assert(!i9100_mfc_native_close(&pn));assert(!i9100_mfc_board_close(&pb));
  mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;}
 fflush(0);fprintf(stderr,"reentry_and_failures ok\n");
}
int main()
{
 integration();ordering();interrupt_edges();stream_semantics();dpb_backpressure();polling_compat();reentry_and_failures();
 puts("PASS actual MFC board->native->IRQ126 notify->IOWorkLoop deferred completion; bounded FIFO/backpressure/EOS; explicit GIC/firmware/timer/IOKit facades, hardware=false");
 return 0;
}
