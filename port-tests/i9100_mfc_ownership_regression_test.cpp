/* SPDX-License-Identifier: BSD-2-Clause
 * R004 ownership/IRQ/lifecycle regressions against the real worker gate,
 * control lease and dispatch/EOI model. Reuses the worker test's engine,
 * reply and dispatch helpers; models only, never hardware delivery proof. */
#define main mfc_worker_baseline_main
#include "i9100_mfc_worker_test.cpp"
#undef main
#if defined(__has_include)
# if __has_include(<sanitizer/lsan_interface.h>)
#  include <sanitizer/lsan_interface.h>
#  define MFC_LSAN_OFF() __lsan_disable()
#  define MFC_LSAN_IGNORE(p) __lsan_ignore_object(p)
# endif
#endif
#ifndef MFC_LSAN_OFF
#define MFC_LSAN_OFF() do{}while(0)
#define MFC_LSAN_IGNORE(p) do{ (void)(p); }while(0)
#endif
/* R010 injection knobs (linked through --wrap): mfc_freeze_busy makes the
 * next N owner freeze calls report the real API's BUSY, mfc_freeze_refuse
 * returns a permanent hard refusal, mfc_freeze_calls counts every attempt
 * (each attempt is a real service pass behind a queued timer or notify),
 * and mfc_irq_fault_fail makes the next N fault queries refuse with the
 * owner-path diagnosis. */
static std::atomic<unsigned> mfc_freeze_busy{0},mfc_freeze_calls{0},mfc_irq_fault_fail{0},mfc_status_busy{0};
static std::atomic<int> mfc_freeze_refuse{0};
extern "C" int __real_i9100_mfc_native_freeze_ctl(struct i9100_mfc_native *,uintptr_t);
extern "C" int __wrap_i9100_mfc_native_freeze_ctl(struct i9100_mfc_native *n,uintptr_t ctl)
{
 mfc_freeze_calls++;
 if(int r=mfc_freeze_refuse.load())return r;
 if(unsigned b=mfc_freeze_busy.load()){mfc_freeze_busy.store(b-1);return I9100_MFC_BUSY;}
 return __real_i9100_mfc_native_freeze_ctl(n,ctl);
}
extern "C" int __real_i9100_mfc_irq_fault(struct i9100_mfc_irq *,int *);
extern "C" int __wrap_i9100_mfc_irq_fault(struct i9100_mfc_irq *q,int *code)
{
 if(unsigned b=mfc_irq_fault_fail.load()){mfc_irq_fault_fail.store(b-1);return I9100_MFC_OWNER;}
 return __real_i9100_mfc_irq_fault(q,code);
}
/* One-shot status BUSY: the injected unknown-state status must be consumed
 * by the worker pass (status_budget drains to 0) — matching the R010
 * retained-review probe's exact injection. */
extern "C" int __real_i9100_mfc_native_status(struct i9100_mfc_native *,struct i9100_mfc_native_status *);
extern "C" int __wrap_i9100_mfc_native_status(struct i9100_mfc_native *n,struct i9100_mfc_native_status *s)
{
 if(mfc_status_busy.exchange(0))return I9100_MFC_BUSY;
 return __real_i9100_mfc_native_status(n,s);
}
/* F1: while the worker's control lease is bound, raw native entry points
 * are refused exactly as the R003 probes documented; the same calls on the
 * old candidate let a raw start seize the engine. */
static void raw_controller()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();
 assert(i9100_mfc_native_start(n,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream))==I9100_MFC_OWNER);
 assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY); /* defer-shaped refusal, never a latch */
 assert(i9100_mfc_native_frame(n,mfc_stream,sizeof(mfc_stream),1,0)==I9100_MFC_OWNER);
 struct i9100_mfc_frame_lease lease={};assert(i9100_mfc_native_acquire(n,&lease)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_close(&n)==I9100_MFC_BUSY);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.error);
 close_all(b,n,w);fflush(0);fprintf(stderr,"raw_controller ok\n");
}
/* F2: the exact bound owner's dispatch/active window is a bounded defer,
 * not -847 foreign evidence; the request completes after the real EOI. */
static void owned_active()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,700,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 mfc_eoi_deferred=true;
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 /* Genuinely active line: the worker defers without faulting, and the raw
  * observer sees the same transient refusal, not enabled=0 "success". */
 {struct i9100_mfc_worker_status q={};bool seen=false;
  for(unsigned i=0;i<4000;i++){
   if(!i9100_mfc_worker_snapshot(w,&q)&&q.defers&&!q.fault&&!q.error){seen=true;break;}
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(seen&&!f.calls);
  assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);
  struct i9100_mfc_native_status ns;assert(!i9100_mfc_native_status(n,&ns)&&!ns.error);}
 mfc_eoi_deferred=false;mfc_complete_eoi();
 assert(mfc_wait_done(f)&&f.last==0);
 struct i9100_mfc_worker_status q;assert(!i9100_mfc_worker_snapshot(w,&q)&&!q.fault&&!q.error);
 close_all(b,n,w);fflush(0);fprintf(stderr,"owned_active ok\n");
}
/* F3/F4: a borrowed output frame holds the worker alive: stop and destroy
 * refuse until the lease is returned through the still-open return path. */
static void borrowed_stop()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,701,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.last==0);
 struct i9100_mfc_frame_lease lease={};assert(!i9100_mfc_worker_frame_lease(w,&lease)&&lease.owned);
 assert(i9100_mfc_worker_stop(w)!=0); /* borrowed frame: cannot prove quiet */
 struct i9100_mfc_worker *x=w;assert(i9100_mfc_worker_destroy(&x)!=0&&x==w); /* retained, lease path alive */
 assert(!i9100_mfc_worker_frame_return(w,&lease)); /* return right preserved */
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"borrowed_stop ok\n");
}
/* F3: an external cancel finishes the request on the loop thread exactly
 * once; the dequeue->active publication window cannot lose it either. */
static void external_cancel()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 /* Saturate the queue, then cancel from this thread while requests sit at
  * every stage: queued, being issued and in flight all stay accounted. */
 mfc_done f[8];unsigned n_req=0;
 for(unsigned i=0;i<8;i++)
  if(!i9100_mfc_worker_frame(w,&f[i],720+i,mfc_stream,sizeof(mfc_stream),1+i,0,mfc_on_done))n_req++;
 for(unsigned i=0;i<n_req;i++)i9100_mfc_worker_cancel(w,&f[i],720+i);
 unsigned total=0,aborted=0;
 for(unsigned i=0;i<n_req;i++){assert(mfc_wait_done(f[i]));total+=f[i].calls;if(f[i].last==I9100_MFC_ABORTED)aborted++;}
 assert(total==n_req); /* every request finished exactly once */
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5,0,false); /* retire any command that issued before its cancel */
 close_all(b,n,w);fflush(0);fprintf(stderr,"external_cancel ok\n");
}
/* F5: a reply without the host-interrupt bit is never consumed as a
 * completion; the request finishes only when the real latched reply lands. */
static void no_completion_bit()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,730,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream));mfc_regs[MF_MFC][0x44/4]=5; /* R2H with no latch raised */
 mfc_advance_us(200000);
 struct i9100_mfc_worker_status q;assert(!i9100_mfc_worker_snapshot(w,&q));
 assert(!f.calls&&q.in_flight); /* not finished on a latch-less reply */
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.last==0);
 close_all(b,n,w);fflush(0);fprintf(stderr,"no_completion_bit ok\n");
}
/* F5: an unexpected reply that does not match the pending command is never
 * retired as this request's completion. */
static void stale_reply()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,740,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 /* A spurious latch carries no valid reply code: it must not retire the
  * request — the drain acknowledges it and the real reply still completes
  * the command afterwards. */
 mfc_irq_reply(0);
 mfc_advance_us(200000);
 assert(!f.calls);
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.last==0);
 close_all(b,n,w);fflush(0);fprintf(stderr,"stale_reply ok\n");
}
/* F5: a raised latch carrying a reply code that does not match the pending
 * command is real firmware evidence — the request retires exactly once with
 * the real I9100_MFC_REPLY, the engine fault latches, every later issue is
 * refused through the same callback path, and explicit cleanup still proves
 * quiescence through the faulted engine. */
static void wrong_reply_fault()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,750,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(0xdead);
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last==I9100_MFC_REPLY);
 struct i9100_mfc_native_status st={};assert(!i9100_mfc_native_status(n,&st));
 assert(st.error==I9100_MFC_REPLY);
 /* The latched fault locks out later issue: admission refuses the submit
  * with the latched engine error and no completion is ever fabricated. */
 mfc_done g;int se=i9100_mfc_worker_frame(w,&g,751,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done);
 assert(se==I9100_MFC_REPLY&&!g.calls);
 /* Explicit teardown: stop must still prove real quiescence through the
  * faulted engine instead of succeeding by hiding the latched error. */
 assert(!i9100_mfc_worker_stop(w));
 assert(!i9100_mfc_native_status(n,&st)&&st.error==I9100_MFC_REPLY&&!st.pending);
 close_all(b,n,w);fflush(0);fprintf(stderr,"wrong_reply_fault ok\n");
}
/* F4/F5: all DPB slots borrowed => backpressure, not loss; a borrowed lease
  * blocks stop; returning it is the only way through. */
static void all_borrowed()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.dpb_count&&st.dpb_count<=4U);
 struct i9100_mfc_frame_lease leases[4]={};
 /* One pending frame command retires exactly one reply and presents at most
  * one slot: fill every DPB slot with a real frame round-trip each. */
 mfc_done f[4];
 for(unsigned k=0;k<st.dpb_count;k++){
  assert(!i9100_mfc_worker_frame(w,&f[k],750+k,mfc_stream,sizeof(mfc_stream),1+k,0,mfc_on_done));
  frame_reply(sizeof(mfc_stream),1,k,1+k);mfc_irq_reply(5);
  assert(mfc_wait_done(f[k])&&f[k].last==0);
 }
 unsigned got=0;
 for(unsigned k=0;k<st.dpb_count;k++){int e=i9100_mfc_worker_frame_lease(w,&leases[k]);if(!e&&leases[k].owned)got++;}
 assert(got==st.dpb_count);
 mfc_done f2;assert(!i9100_mfc_worker_frame(w,&f2,790,mfc_stream,sizeof(mfc_stream),9,0,mfc_on_done));
 mfc_advance_us(200000);
 assert(!f2.calls); /* backpressured: every output slot borrowed */
 assert(i9100_mfc_worker_stop(w)!=0); /* cannot prove quiet while borrowed */
 assert(!i9100_mfc_worker_frame_return(w,&leases[0]));
 for(unsigned i=0;i<4000&&!f2.calls;i++){
  struct i9100_mfc_native_status q;if(!i9100_mfc_native_status(n,&q)&&q.pending){frame_reply(sizeof(mfc_stream));mfc_irq_reply(5,0,false);}
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f2.calls==1);
 for(unsigned k=1;k<st.dpb_count;k++)assert(!i9100_mfc_worker_frame_return(w,&leases[k]));
 /* f2's display slot is still presented: return it too before stop. */
 close_all(b,n,w);fflush(0);fprintf(stderr,"all_borrowed ok\n");
}
/* R005-F1: an arbitrary integer is not a control lease. The provider must
 * verify token identity itself — a never-issued value, a forged struct that
 * merely looks like the real token, a stale released token and a wrong
 * caller-supplied token are all refused before any MMIO or fault path. */
static void noop_notify(void *){}
static void fake_control()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 auto pf=mfc_profile();
 assert(i9100_mfc_native_control_bind(n,0x1234)==I9100_MFC_OWNER); /* unissued */
 /* A forged token object that copies the real one's fields is still an
  * integer the provider never issued: verify compares, never dereferences. */
 i9100_mfc_board_irq_token t={};
 assert(!i9100_mfc_board_irq_acquire(b,noop_notify,n,&t));
 i9100_mfc_board_irq_token forged={};
 forged.self=(uintptr_t)&forged;forged.board=(uintptr_t)b;forged.owned=1;forged.generation=t.generation;
 assert(i9100_mfc_native_control_bind(n,(uintptr_t)&forged)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_control_bind(n,t.self)==0); /* real provider-issued binds */
 assert(i9100_mfc_native_control_bind(n,t.self)==I9100_MFC_BUSY); /* single lease */
 struct i9100_mfc_native_status st;
 /* Every controlled entry re-verifies: a wrong token is refused before any
  * hardware access and can never latch a fault on the legal owner. */
 unsigned r0=m.reads,w0=m.writes;
 assert(i9100_mfc_native_start_ctl(n,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_poll_ctl(n,0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_frame_ctl(n,mfc_stream,sizeof(mfc_stream),1,0,0x1234)==I9100_MFC_OWNER);
 struct i9100_mfc_frame_lease lease={};assert(i9100_mfc_native_acquire_ctl(n,&lease,0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_shutdown_ctl(n,0x1234)==I9100_MFC_OWNER);
 assert(m.reads==r0&&m.writes==w0); /* refused before any MMIO */
 assert(!i9100_mfc_native_status(n,&st)&&!st.error); /* no fault latched */
 /* Stale token: once the board token is released, its integer is dead. */
 assert(!i9100_mfc_board_irq_release(b,&t));
 assert(i9100_mfc_native_poll_ctl(n,t.self)==I9100_MFC_OWNER);
 assert(!i9100_mfc_native_status(n,&st)&&!st.error);
 assert(!i9100_mfc_native_control_drop(n,t.self));
 /* Re-acquired token gets a new generation; the new lease binds again. */
 i9100_mfc_board_irq_token t2={};assert(!i9100_mfc_board_irq_acquire(b,noop_notify,n,&t2));
 assert(!i9100_mfc_native_control_bind(n,t2.self));
 assert(i9100_mfc_native_poll_ctl(n,t.self)==I9100_MFC_OWNER); /* previous token stays stale */
 assert(!i9100_mfc_native_control_drop(n,t2.self));
 assert(!i9100_mfc_board_irq_release(b,&t2));
 assert(!i9100_mfc_native_close(&n)&&!n);assert(!i9100_mfc_board_close(&b)&&!b);
 mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;
 fflush(0);fprintf(stderr,"fake_control ok\n");
}
/* R005-F1: while the worker's real lease is bound, per-entry calls carrying
 * wrong tokens are refused without touching MMIO or latching native_error;
 * the legitimate owner keeps working through the same path. */
static void wrong_control()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 auto pf=mfc_profile();
 assert(mfc_wait_idle(4000)); /* drain the loop before the no-MMIO snapshot */
 unsigned r0=m.reads,w0=m.writes;
 assert(i9100_mfc_native_poll_ctl(n,0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_start_ctl(n,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_frame_ctl(n,mfc_stream,sizeof(mfc_stream),1,0,0x1234)==I9100_MFC_OWNER);
 struct i9100_mfc_frame_lease lease={};assert(i9100_mfc_native_acquire_ctl(n,&lease,0x1234)==I9100_MFC_OWNER);
 assert(i9100_mfc_native_shutdown_ctl(n,0x1234)==I9100_MFC_OWNER);
 assert(m.reads==r0&&m.writes==w0); /* qualification precedes MMIO */
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.error);
 /* The bound owner's own path still completes real work. */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,765,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.last==0);
 assert(!i9100_mfc_native_status(n,&st)&&!st.error);
 close_all(b,n,w);fflush(0);fprintf(stderr,"wrong_control ok\n");
}
/* R005-F1/F2: real damage on the legal owner's path still latches — power
 * loss while commands run fails the request and sets native_error, while an
 * idle bogus-token call stays a clean refusal. */
static void owner_fault_latches()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_regs[MF_PMU][0x3c44/4]=0; /* real power loss on the bound owner's domain */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,766,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls&&f.last!=0); /* real damage retired the request as an error */
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.error);
 assert(i9100_mfc_native_poll_ctl(n,0x1234)==I9100_MFC_OWNER); /* still refused, no double fault semantics */
 mfc_regs[MF_PMU][0x3c44/4]=7; /* repair the domain; teardown still proceeds */
 close_all(b,n,w);fflush(0);fprintf(stderr,"owner_fault_latches ok\n");
}
/* R005-F2: an MC that ignores reset makes the real shutdown fail — stop
 * cannot claim quiescence while MC_STATUS stays busy and the RISC register
 * still reads 0x3ff; once the MC releases, the same stop proves it. */
static void busy_mc_stop()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_mc_stuck=true;mfc_regs[MF_MFC][0x510/4]=1;
 int se=i9100_mfc_worker_stop(w);
 assert(se!=0); /* real shutdown must fail, never a pretend success */
 assert((mfc_regs[MF_MFC][0x510/4]&3U)!=0); /* MC still busy, evidence kept */
 int de=i9100_mfc_worker_destroy(&w);assert(de!=0&&w); /* retained: not quiet */
 mfc_mc_stuck=false;
 se=i9100_mfc_worker_stop(w);
 assert(!se); /* same stop now proves actual quiescence */
 assert(!(mfc_regs[MF_MFC][0/4]&1U)); /* RISC reset really taken */
 assert(!(mfc_regs[MF_MFC][0x510/4]&3U)); /* MC really idle */
 assert(!mfc_regs[MF_MFC][0x30/4]&&!mfc_regs[MF_MFC][0x44/4]&&!mfc_regs[MF_MFC][8/4]);
 close_all(b,n,w);fflush(0);fprintf(stderr,"busy_mc_stop ok\n");
}
/* R005-F3: a cancelled-but-issued request keeps its real retirement path —
 * it stays active under the armed line or a queued timer until the firmware
 * actually replies (or the deadline proves it dead); no second command may
 * issue meanwhile, and no artificial loop wake finishes it. */
static void cancel_retirement()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,770,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 /* A second request proves no command issues during the retirement window. */
 mfc_done f2;assert(!i9100_mfc_worker_frame(w,&f2,771,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done));
 i9100_mfc_worker_cancel(w,&f,770);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.in_flight&&!f.calls); /* still issued, not early-finished */
  assert(ws.armed);} /* the armed line remains the retirement service */
 /* Model time advances but no reply lands: still no fabricated finish. */
 for(unsigned i=0;i<20;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(!f.calls&&ws.in_flight&&!f2.calls);}
 /* The real reply retires it: exactly once, marked cancelled. */
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MFC_ABORTED);
 /* The queued request now issues and completes normally. */
 frame_reply(sizeof(mfc_stream),1,1,2);mfc_irq_reply(5);
 assert(mfc_wait_done(f2)&&f2.last==0);
 struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.cancelled==1);
 close_all(b,n,w);fflush(0);fprintf(stderr,"cancel_retirement ok\n");
}
/* R005-F3/F5: the IRQ notify path retires an issued command without any
 * manual clock advance — a due-timer-less loop is never woken artificially. */
static void irq_no_clock_retirement()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,775,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 unsigned i=0;while(i<4000&&(mfc_regs[MF_MFC][8/4]&1U)&&!mfc_dispatch_irq126()){i++;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(i<4000); /* dispatched without touching the model clock */
 for(i=0;i<4000&&!f.calls;i++)std::this_thread::sleep_for(std::chrono::milliseconds(1)); /* no mfc_advance_us */
 assert(f.calls==1&&f.last==0); /* retired by the real notify path */
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_no_clock_retirement ok\n");
}
/* R005-F3: a callback holding the gate open is still counted: the eight
 * request bound includes it, and the count only drops after the gate is
 * safely reclosed and the request's last access finished. */
static std::atomic<unsigned> mfc_hold_gate;
static void mfc_blocking_done(void *p,uint64_t token,int result)
{
 auto *d=(mfc_done *)p;d->calls++;d->last=result;d->token=token;
 while(mfc_hold_gate.load())std::this_thread::yield();
}
static void callback_gate_accounting()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_hold_gate.store(1);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,780,mfc_stream,sizeof(mfc_stream),1,0,mfc_blocking_done));
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 for(unsigned i=0;i<4000&&!f.calls;i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(f.calls); /* callback entered and now holds the gate open */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.callbacks==1); /* still counted while user code runs inside */
  /* The callback reference consumes bound capacity: 7 more fit, the 8th
   * must be refused even though nothing is queued or in flight. */
  mfc_done g[8];unsigned ok=0,busy=0;
  for(unsigned i=0;i<8;i++){int e=i9100_mfc_worker_frame(w,&g[i],790+i,mfc_stream,sizeof(mfc_stream),10+i,0,mfc_on_done);
   if(!e)ok++;else{assert(e==I9100_MFC_BUSY);busy++;}}
  assert(ok==I9100_MFC_WORKER_QUEUE-1&&busy==1);
  /* Cancels while the gate is still held by the callback land determinist-
   * ically on the cancelled list; the same bound must cover them too. */
  for(unsigned i=0;i<4;i++)i9100_mfc_worker_cancel(w,&g[i],790+i);
  {struct i9100_mfc_worker_status ws2;assert(!i9100_mfc_worker_snapshot(w,&ws2));
   assert(ws2.queued+ws2.cancel_pending+ws2.in_flight+ws2.callbacks==I9100_MFC_WORKER_QUEUE);}
  mfc_hold_gate.store(0);
  for(unsigned i=0;i<4000;i++){assert(!i9100_mfc_worker_snapshot(w,&ws));
   if(ws.callbacks==0&&!ws.cancel_pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  for(unsigned i=0;i<4;i++)assert(g[i].calls==1&&g[i].last==I9100_MFC_ABORTED);
  /* f already presented DPB slot 0; each live reply must present a slot the
   * decoder still reports free — re-presenting an occupied slot is a real
   * protocol violation, not something to hide behind wrong test input. */
  for(unsigned i=4;i<7;i++){frame_reply(sizeof(mfc_stream),1,i-3,10+i);mfc_irq_reply(5);
   for(unsigned k=0;k<4000&&!g[i].calls;k++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   if(!(g[i].calls==1&&g[i].last==0)){struct i9100_mfc_worker_status ws3;struct i9100_mfc_native_status nst3;
    i9100_mfc_worker_snapshot(w,&ws3);i9100_mfc_native_status(n,&nst3);
    fprintf(stderr,"G%u calls=%u last=%d act=%u q=%u armed=%u nerr=%d ph=%u pend=%u fc=%llu\n",i,
     g[i].calls.load(),g[i].last.load(),ws3.in_flight,ws3.queued,ws3.armed,nst3.error,nst3.phase,nst3.pending,
     (unsigned long long)nst3.frame_completed);fflush(0);}
   assert(g[i].calls==1&&g[i].last==0);}
 }
 struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.callbacks==0&&ws.cancelled==4);
 close_all(b,n,w);fflush(0);fprintf(stderr,"callback_gate_accounting ok\n");
}
/* R005-F5: a blocked workloop must not lose accounting — submissions and
 * cancels under the held gate all land in the same bounded ledger. */
static void blocked_loop_accounting()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 assert(!mfc_loops.empty());auto st=mfc_state(mfc_loops[0].first);assert(st);
 st->gate.lock(); /* the runner cannot dispatch while the gate is held */
 mfc_done g[9];unsigned ok=0,busy=0;
 for(unsigned i=0;i<9;i++){int e=i9100_mfc_worker_frame(w,&g[i],820+i,mfc_stream,sizeof(mfc_stream),20+i,0,mfc_on_done);
  if(!e)ok++;else{assert(e==I9100_MFC_BUSY);busy++;}}
 assert(ok==I9100_MFC_WORKER_QUEUE&&busy==1);
 for(unsigned i=0;i<4;i++)i9100_mfc_worker_cancel(w,&g[i],820+i);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.queued+ws.cancel_pending+ws.in_flight+ws.callbacks==I9100_MFC_WORKER_QUEUE);}
 st->gate.unlock();
 for(unsigned i=0;i<4;i++){for(unsigned k=0;k<4000&&!g[i].calls;k++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(g[i].calls==1&&g[i].last==I9100_MFC_ABORTED);}
 for(unsigned i=4;i<8;i++){frame_reply(sizeof(mfc_stream),1,(i-4)%4,20+i);mfc_irq_reply(5);
  for(unsigned k=0;k<4000&&!g[i].calls;k++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(g[i].calls==1&&g[i].last==0);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"blocked_loop_accounting ok\n");
}
/* R005-F5: frames presenting in nonzero/alternate DPB slots keep the same
 * display semantics, and the LAST drain still waits for the real EMPTY. */
static void alternate_slot_eos()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.dpb_count>=2U);
 mfc_done f1,f2;
 assert(!i9100_mfc_worker_frame(w,&f1,830,mfc_stream,sizeof(mfc_stream),10,0,mfc_on_done));
 frame_reply(sizeof(mfc_stream),1,1,10);mfc_irq_reply(5);assert(mfc_wait_done(f1)&&f1.last==0);
 assert(!i9100_mfc_worker_frame(w,&f2,831,mfc_stream,sizeof(mfc_stream),11,0,mfc_on_done));
 frame_reply(sizeof(mfc_stream),1,3%st.dpb_count,11);mfc_irq_reply(5);assert(mfc_wait_done(f2)&&f2.last==0);
 mfc_done eos;assert(!i9100_mfc_worker_frame(w,&eos,832,mfc_stream,sizeof(mfc_stream),12,1,mfc_on_done));
 frame_reply(sizeof(mfc_stream));mfc_irq_reply(5);
 assert(mfc_wait_native(n,4000,st_draining));
 for(unsigned i=0;i<400&&!eos.calls;i++){
  mfc_advance_us(3000);std::this_thread::sleep_for(std::chrono::milliseconds(2));
  struct i9100_mfc_native_status q;if(!i9100_mfc_native_status(n,&q)&&q.pending){frame_reply(0,3);mfc_irq_reply(5);}
 }
 assert(eos.calls&&eos.last==0);
 assert(!i9100_mfc_native_status(n,&st)&&st.drained);
 close_all(b,n,w);fflush(0);fprintf(stderr,"alternate_slot_eos ok\n");
}
/* F2/F4: a dropped GIC enable write faults the exact slot; issue is refused
 * and destroy preserves ownership with the original error. Once the write
 * path is repaired, the exact owner's cleanup masks, proves dispatch/EOI
 * quiet and releases the binding so the whole teardown completes. */
static void faulted_slot()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_gic_mask_fail=true; /* ISR-path mask write is lost: slot faults */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,760,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 /* The dropped mask write faults the exact slot: the in-flight request is
  * retired with the propagated ownership/IO error, never as success. */
 assert(mfc_wait_done(f)&&f.last!=0);
 mfc_done f2;int se=i9100_mfc_worker_frame(w,&f2,761,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done);
 if(!se){for(unsigned i=0;i<4000&&!f2.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
 assert(se!=0||f2.last!=0); /* a faulted slot is never ready for issue */
 int de=i9100_mfc_worker_destroy(&w);
 assert(de!=0&&w); /* retained: mask retry still drops while the write is lost */
 assert(i9100_mfc_native_close(&n)==I9100_MFC_BUSY);
 assert(i9100_mfc_board_close(&b)==I9100_MP_BUSY);
 /* The original fault diagnostics survive cleanup: the latched owner error
  * and mask-failure evidence are never silently cleared. */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.error!=0);}
 mfc_gic_mask_fail=false; /* repair the write path; the same owner retries */
 int re=i9100_mfc_worker_destroy(&w);
 if(re){struct i9100_mfc_native_status q;i9100_mfc_native_status(n,&q);
  struct i9100_mfc_worker_status ws;i9100_mfc_worker_snapshot(w,&ws);
  fprintf(stderr,"CLEANUP re=%d nerr=%d cerr=%d werr=%d wcerr=%d gic=%x act=%x\n",
   re,q.error,q.cleanup_error,ws.error,ws.cleanup_error,mfc_regs[MF_GIC][0x10c/4],mfc_regs[MF_GIC][0x30c/4]);}
 assert(!re&&!w); /* exact-owner cleanup completed the whole teardown */
 assert(!i9100_mfc_native_close(&n)&&!n);assert(!i9100_mfc_board_close(&b)&&!b);
 mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;
 fflush(0);fprintf(stderr,"faulted_slot ok\n");
}
/* R006-F1: boot with a firmware deadline beyond the worker's own 500ms
 * command window — the exact R005 probe geometry (500ms vs 5000ms). */
static void boot_engine_ms(struct i9100_mfc_worker *w,struct i9100_mfc_native *n,mfc_done &d,unsigned timeout_ms)
{
 auto pf=mfc_profile();pf.timeout_ms=timeout_ms;
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
}
/* Waits until the issued command's worker deadline really fired: exactly
 * one callback with the expected result while the request stays internally
 * owned. Returns false if the engine never reached pending or the callback
 * never landed. */
static bool wait_worker_deadline(struct i9100_mfc_native *n,struct i9100_mfc_worker *w,mfc_done &f,int expect)
{
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(f.calls!=1||f.last!=expect){
  struct i9100_mfc_worker_status ws;struct i9100_mfc_native_status st;
  i9100_mfc_worker_snapshot(w,&ws);i9100_mfc_native_status(n,&st);
  fprintf(stderr,"DEADLINE calls=%u last=%d expect=%d act=%u pend=%u nerr=%d werr=%d fault=%u tim=%llu\n",
   f.calls.load(),f.last.load(),expect,ws.in_flight,st.pending,st.error,ws.error,ws.fault,
   (unsigned long long)ws.timeouts);}
 return f.calls==1&&f.last==expect;
}
/* R006-F1 (the rejected R005 geometry): callback=-844 fires once at the
 * 500ms worker deadline while the firmware's 5000ms deadline is still far
 * away — but the issued command is NOT abandoned: it stays an internal
 * in-flight owner, pending keeps reading 1, no fault is latched, and a
 * real late reply still retires the hardware side with no second callback. */
static void deadline_late_reply()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,850,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 assert(wait_worker_deadline(n,w,f,I9100_MFC_TIMEOUT));
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.in_flight&&ws.timeouts&&!ws.fault&&!ws.error); /* retained, not dropped */
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.pending&&!st.error);} /* the hardware command is still real */
 /* The real late reply is consumed by the same poll/IRQ service and retires
  * the internal owner silently. */
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1); /* exactly one callback for the whole request lifetime */
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.pending&&!st.error);
  struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight);}
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"deadline_late_reply ok\n");
}
/* R006-F1: no reply ever arrives — the firmware's own 5000ms deadline is
 * what really retires the command: the poll service fires it, pending
 * clears with a latched error, and the internal owner is freed only after
 * that real retirement. The latched engine fault then refuses new issue;
 * explicit cleanup still proves quiescence through the faulted engine. */
static void deadline_firmware()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,851,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 assert(wait_worker_deadline(n,w,f,I9100_MFC_TIMEOUT));
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending&&!st.error);}
 /* Advance past the firmware's own deadline: the real poll must fire it. */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&st.error)break;
  mfc_advance_us(200000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(!st.pending&&st.error==I9100_MFC_TIMEOUT);} /* firmware deadline, real evidence */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1); /* still exactly once */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight&&ws.fault&&ws.error);}
 /* The latched fault refuses new issue at admission. */
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,852,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 assert(!i9100_mfc_worker_stop(w)); /* explicit same-owner cleanup */
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.pending);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"deadline_firmware ok\n");
}
/* R006-F1: explicit stop after the deadline — drain must not wait out the
 * firmware's own 5000ms clock; the proven owner shutdown retires the
 * command and the same stop completes the retirement bookkeeping. */
static void deadline_explicit_stop()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,853,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 assert(wait_worker_deadline(n,w,f,I9100_MFC_TIMEOUT));
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 int se=i9100_mfc_worker_stop(w); /* would hang until 5s if it waited on firmware */
 assert(!se&&f.calls==1);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.pending);}
 assert(!(mfc_regs[MF_MFC][0x510/4]&3U)); /* MC really idle after the reset */
 close_all(b,n,w);fflush(0);fprintf(stderr,"deadline_explicit_stop ok\n");
}
/* R006-F1: a retirement that cannot prove quiescence wedges honestly —
 * with the domain dead the firmware's own deadline can never fire (the
 * poll cannot reach the engine), the 6s retirement bound hands the owner
 * shutdown which is refused, the fault latches, new issue is refused, and
 * ownership survives until the same owner's explicit cleanup after real
 * repair — which itself must first fail honestly while MC stays stuck. */
static void deadline_shutdown_stuck()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,854,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 assert(wait_worker_deadline(n,w,f,I9100_MFC_TIMEOUT));
 mfc_regs[MF_CMU][0xc928/4]&=~7U; /* domain dies inside the retirement window */
 /* Burn the retirement bound: each advance services one bounded defer
  * window; when the bound fires the owner shutdown is refused for real. */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error)break;
  mfc_advance_us(100000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&ws.in_flight); /* wedged: ownership kept, fault latched */
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,855,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 /* Repair the domain but keep MC stuck: the explicit cleanup still fails
  * honestly — stop cannot claim quiescence it cannot prove. */
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_mc_stuck=true;mfc_regs[MF_MFC][0x510/4]=1;
 assert(i9100_mfc_worker_stop(w)!=0);
 mfc_mc_stuck=false;
 assert(!i9100_mfc_worker_stop(w)); /* same-owner cleanup after real repair */
 assert(!(mfc_regs[MF_MFC][0x510/4]&3U));
 assert(f.calls==1);
 close_all(b,n,w);fflush(0);fprintf(stderr,"deadline_shutdown_stuck ok\n");
}
/* R006-F1: cancel an issued command under the same deadline with no reply —
 * the client hears ABORTED exactly once, the hardware side keeps its owner,
 * and the real reply still retires it silently afterwards. */
static void cancel_deadline()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,856,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 i9100_mfc_worker_cancel(w,&f,856);
 assert(wait_worker_deadline(n,w,f,I9100_MFC_ABORTED));
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.in_flight&&!ws.fault);
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1); /* no second callback for the silent retirement */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.cancelled==1&&!ws.in_flight);}
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"cancel_deadline ok\n");
}
/* R006-F1: a failed line-arm after the command was already issued must not
 * detach the in-flight request — the client hears the arm error once while
 * the request stays internally owned. The faulted IRQ slot also locks the
 * whole ctl path (irq_verify refuses a faulted token), so the bounded
 * retirement can never silently consume a reply: the request wedges with a
 * latched fault until the exact owner's explicit cleanup repairs the slot
 * and proves real quiescence. */
static void arm_failure_retention()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine_ms(w,n,d,5000);
 mfc_gic_unmask_fail=true; /* the leased-line enable write is dropped */
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,857,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0); /* arm error reported exactly once */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.in_flight); /* issued command still owned, never detached */
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 /* The faulted slot locks the whole ctl path: the fault latches and new
  * issue is refused — the request is retained, never silently retired. */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0&&ws.in_flight);
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,858,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 mfc_gic_unmask_fail=false;
 /* Same-owner cleanup repairs the faulted slot, proves real quiescence and
  * retires the request — still with exactly one client callback. */
 assert(!i9100_mfc_worker_stop(w));
 assert(f.calls==1);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.pending);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"arm_failure_retention ok\n");
}
/* R006-F2: with the engine idle, a lost local clock gate or power domain
 * must never be probed — the stray wake's drain latches the real fault and
 * new issue is refused, without a single illegal read or write into the
 * dead domain. Repair restores explicit cleanup. */
static void idle_domain_loss()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 assert(mfc_wait_idle(4000));
 mfc_regs[MF_CMU][0xc928/4]&=~7U; /* local clock gate lost while PMU stays on */
 i9100_mfc_worker_cancel(w,&d,999999); /* a real workloop wake, no request */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0);}
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,858,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done)!=0&&!g.calls);
 mfc_regs[MF_CMU][0xc928/4]|=7U; /* repair the domain */
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);
 /* Power loss on the idle path latches the same way. */
 n=open_engine(&b);w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d2;boot_engine(w,n,d2);
 assert(mfc_wait_idle(4000));
 mfc_regs[MF_PMU][0x3c44/4]=0;
 i9100_mfc_worker_cancel(w,&d2,999999);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error)break;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0);}
 mfc_regs[MF_PMU][0x3c44/4]=7;
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"idle_domain_loss ok\n");
}
/* R006-F2: clock loss mid-command — the domain must not be read; the
 * request hears the real error once, the fault latches, issue is refused
 * and repaired-domain cleanup still proves quiescence. */
static void active_clock_loss()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,859,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 mfc_regs[MF_CMU][0xc928/4]&=~7U; /* domain dies under the issued command */
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0); /* real error, exactly once */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0);
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.error);}
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,860,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 assert(!i9100_mfc_worker_stop(w)); /* repaired domain: explicit cleanup */
 close_all(b,n,w);fflush(0);fprintf(stderr,"active_clock_loss ok\n");
}
/* R007: registry-level observation of the leased IRQ126 slot. */
static unsigned irq126_slot_field(unsigned which)
{
 for(unsigned s=0;s<I9100_IRQ_SLOTS;s++)
  if(mfc_parent.irqs.slots[s].irq==126)
   return which==0?mfc_parent.irqs.slots[s].enabled:mfc_parent.irqs.slots[s].fault;
 return 0xffffffffU;
}
/* R007-F1 (the rejected probe): a real reply + raised latch with the local
 * clock gate off must never reach the gated MFC domain. The upper half
 * qualifies the domain through the always-on CMU/PMU maps, masks the leased
 * GIC126, still notifies the bound sink, and the deferred thread service
 * latches the real domain fault — all without one illegal MMIO access. */
static void irq_late_clock_loss()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,870,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5); /* real reply, latch raised */
 mfc_regs[MF_CMU][0xc928/4]&=~7U; /* domain dies before the dispatch reaches it */
 assert(mfc_dispatch_irq126()); /* real provider dispatch/EOI; no illegal read */
 assert(!m.illegal_reads&&!m.illegal_writes&&irq126_slot_field(0)==0);
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0); /* real domain error, exactly once */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&ws.in_flight);
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 for(unsigned i=0;i<8;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(irq126_slot_field(0)==0);} /* mask held across real timer services */
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 assert(!i9100_mfc_worker_stop(w)); /* same-owner cleanup after repair */
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_late_clock_loss ok\n");
}
/* R007-F1: the same dispatch while the PMU power domain is down. */
static void irq_late_power_loss()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,871,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_PMU][0x3c40/4]&=~7U;mfc_regs[MF_PMU][0x3c44/4]&=~7U; /* power gone */
 assert(mfc_dispatch_irq126());
 assert(!m.illegal_reads&&!m.illegal_writes&&irq126_slot_field(0)==0);
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0);}
 mfc_regs[MF_PMU][0x3c40/4]|=7U;mfc_regs[MF_PMU][0x3c44/4]|=7U;
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_late_power_loss ok\n");
}
/* R007-F1: a dispatch on a live domain with no latch raised is a genuine
 * spurious interrupt — the line stays enabled, nothing is masked, faulted
 * or consumed, and the real reply still completes the command. */
static void irq_spurious_powered()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,872,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 assert(mfc_dispatch_irq126()); /* no latch raised: upper half reports spurious */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(!ws.fault&&!ws.error&&!f.calls&&irq126_slot_field(0)==1); /* still armed */
  struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&st.pending);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_irq_reply(5);
 assert(mfc_wait_done(f)&&f.last==0);
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_spurious_powered ok\n");
}
/* R007-F2: once a real domain fault is latched, every later timer service
 * keeps the leased line masked — the damaged owner is retired through the
 * poll/deadline/shutdown path, never fed more dispatches. */
static void post_fault_mask_held()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,873,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0);
 for(unsigned i=0;i<4000&&irq126_slot_field(0);i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(irq126_slot_field(0)==0); /* the fault pass disarmed the leased line */
 for(unsigned i=0;i<16;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(irq126_slot_field(0)==0);} /* held masked across many real services */
 assert(!mfc_dispatch_irq126()); /* masked slot: the provider declines */
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 for(unsigned i=0;i<16;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(irq126_slot_field(0)==0);} /* still masked after the domain returns */
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"post_fault_mask_held ok\n");
}
/* R007-F2: a dropped mask write inside the upper half's dead-domain path is
 * real retained evidence — the slot faults, the notify still reaches the
 * worker, and the same owner's cleanup repairs the slot and proves
 * quiescence. */
static void irq_mask_write_loss()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,874,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending)break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 mfc_gic_mask_fail=true;
 assert(mfc_dispatch_irq126()); /* mask write dropped: dispatch still completes */
 mfc_gic_mask_fail=false;
 assert(irq126_slot_field(1)!=0); /* faulted slot is retained evidence */
 for(unsigned i=0;i<4000&&!f.calls;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1&&f.last!=0);
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 assert(!i9100_mfc_worker_stop(w)); /* repair proves mask+dispatch quiet */
 assert(!irq126_slot_field(1));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_mask_write_loss ok\n");
}
/* R008-F1 (the rejected probe's deterministic window): a real reply+raised
 * latch with the local clock gate off is dispatched while the deferred
 * thread is held on the real workloop gate, and the gate is restored before
 * the thread may run. The ISR's dead-domain observation is published under
 * the exact bound token+generation before the notify, so the deferred pass
 * consumes the persistent record — never rereading the restored domain as
 * success: exactly one real error callback, fault latched, mask held, the
 * in-flight owner retired by the real bounded path and new issue refused
 * until the same owner's explicit cleanup. */
static void irq_fault_transient_clock()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,880,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);
 ls->gate.lock(); /* the deferred thread cannot run inside the window */
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126()); /* the ISR really observed the dead domain */
 mfc_regs[MF_CMU][0xc928/4]|=7U; /* restored before the thread may run */
 ls->gate.unlock();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&ws.ctl_bound);}
 assert(irq126_slot_field(0)==0&&!m.illegal_reads&&!m.illegal_writes);
 /* The restored domain lets the real bounded retirement consume the reply:
  * one callback for the whole request lifetime, ownership kept. */
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st)&&!st.pending);
  struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight&&ws.fault);}
 /* The latched fault refuses new codec issue even after the domain returns. */
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,881,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 for(unsigned i=0;i<8;i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(irq126_slot_field(0)==0);} /* mask held across real services */
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_transient_clock ok\n");
}
/* R008-F1: the same deterministic window with the PMU power domain down
 * instead of the local clock gate — the persistent record delivers the
 * same owner fault across the resource restoration. */
static void irq_fault_transient_power()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,882,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_PMU][0x3c40/4]&=~7U;mfc_regs[MF_PMU][0x3c44/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_PMU][0x3c40/4]|=7U;mfc_regs[MF_PMU][0x3c44/4]|=7U;
 ls->gate.unlock();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_transient_power ok\n");
}
/* R008-F1: a merged wake window — a queued cancel, a due timer service and
 * the fault notify all land in the same deferred pass; the persistent
 * record is still consumed first and the merged normal work cannot
 * launder or preempt it. */
static void irq_fault_notify_merge()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,883,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 mfc_done g;assert(!i9100_mfc_worker_frame(w,&g,884,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)); /* queued behind the in-flight command */
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 i9100_mfc_worker_cancel(w,&g,884); /* normal drain work merged into the window */
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_advance_us(4000); /* the 2ms defer timer is also due inside the window */
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 ls->gate.unlock();
 /* One merged pass: the cancel drains normally, the record still latches
  * the real fault, and the active command hears its error exactly once. */
 for(unsigned i=0;i<4000&&(!f.calls||!g.calls);i++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(g.calls==1&&g.last==I9100_MFC_ABORTED);
 assert(f.calls==1&&f.last==I9100_MP_OWNER);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&ws.cancelled==1&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(f.calls==1);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_notify_merge ok\n");
}
/* R008-F1: repeated status reads, foreign-token queries and many real
 * timer services never consume or launder the pending record — only the
 * accused binding's own release retires it, and a rebound generation
 * inherits nothing. */
static void irq_fault_status_repeat()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,885,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 ls->gate.unlock();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 /* Foreign, stale-shaped and wrong-generation tokens read an empty record
  * and change nothing for the real owner. */
 i9100_mfc_board_irq_token foreign={};foreign.self=(uintptr_t)&foreign;
 foreign.board=(uintptr_t)b;foreign.owned=1;foreign.generation=77;
 int code=7;assert(!i9100_mfc_board_irq_fault(b,&foreign,&code)&&code==0);
 i9100_mfc_board_irq_token stale={};stale.self=(uintptr_t)&stale;
 stale.board=(uintptr_t)b;stale.owned=0;stale.generation=0;
 assert(!i9100_mfc_board_irq_fault(b,&stale,&code)&&code==0);
 /* Sixteen real service windows with repeated status reads: the latched
  * fault and the mask stay while the real retirement drains the reply. */
 for(unsigned i=0;i<16;i++){
  struct i9100_mfc_native_status nst;assert(!i9100_mfc_native_status(n,&nst));
  struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&irq126_slot_field(0)==0);
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 /* The foreign query never touched the record: the fault is still latched. */
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&ws.error!=0);}
 assert(f.calls==1);
 assert(!i9100_mfc_worker_stop(w));
 assert(!i9100_mfc_worker_destroy(&w)&&!w);
 /* The accused binding retired its record on release: a rebound token gets
  * a fresh generation and inherits no accusation. */
 i9100_mfc_board_irq_token t={};assert(!i9100_mfc_board_irq_acquire(b,noop_notify,n,&t));
 code=5;assert(!i9100_mfc_board_irq_fault(b,&t,&code)&&code==0);
 assert(!i9100_mfc_board_irq_release(b,&t));
 assert(!i9100_mfc_native_close(&n)&&!n);assert(!i9100_mfc_board_close(&b)&&!b);
 mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;
 fflush(0);fprintf(stderr,"irq_fault_status_repeat ok\n");
}
/* R008-F1: a failed explicit cleanup after the fault keeps the binding and
 * the record; once the real blocker is repaired the same owner's cleanup
 * proves quiescence and retires everything. */
static void irq_fault_cleanup_retry()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,886,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 ls->gate.unlock();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 mfc_mc_stuck=true;mfc_regs[MF_MFC][0x510/4]=1;
 assert(i9100_mfc_worker_stop(w)!=0); /* cannot prove quiescence: retained */
 struct i9100_mfc_worker *x=w;assert(i9100_mfc_worker_destroy(&x)!=0&&x==w);
 mfc_mc_stuck=false;
 assert(!i9100_mfc_worker_stop(w)); /* same-owner cleanup after real repair */
 assert(!(mfc_regs[MF_MFC][0x510/4]&3U)&&!(mfc_regs[MF_MFC][0/4]&1U));
 assert(f.calls==1);
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_cleanup_retry ok\n");
}
/* Shared window prelude for mid-pipeline freeze tests: wait until the
 * mailbox really holds a pending command and the leased line is armed. */
static void wait_pending_armed(struct i9100_mfc_native *n,uint32_t phase)
{
 struct i9100_mfc_native_status st={};unsigned p=0;
 for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&st.phase==phase&&
  (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);
}
/* Locked-gate dead-domain window: post the reply, kill the local clock,
 * run the real GIC126 dispatcher (the ISR observes the dead domain and
 * publishes the persistent fault), restore before the deferred thread
 * may run. */
static void dead_clock_window()
{
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);
 ls->gate.lock();
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 ls->gate.unlock();
}
/* Wait for the retirement path to really drain the in-flight command. */
static void wait_retired(struct i9100_mfc_native *n,struct i9100_mfc_worker *w)
{
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_native_status st;struct i9100_mfc_worker_status ws;
  if(!i9100_mfc_native_status(n,&st)&&!st.pending&&!i9100_mfc_worker_snapshot(w,&ws)&&!ws.in_flight)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
/* R009-F1 (the review probe's contract): BOOT pending+armed, gate locked,
 * reply 9 lands, the clock dies, the real dispatch runs, the clock comes
 * back before the deferred thread may. The consumed fault freezes codec
 * stepping: the BOOT reply drains for mailbox evidence but SYS_INIT can
 * never issue — phase stays exactly where the fault caught it and the
 * real command counters prove zero new codec traffic. */
static void irq_fault_transient_boot()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error!=0&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,141,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_transient_boot ok\n");
}
/* R009-F1: the same frozen window at SYSINIT — reply 8 drains for evidence
 * but OPEN can never issue: phase/expected stay, instance never opens. */
static void fault_freeze_sysinit()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,150,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 mfc_response(8,0x60000);dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_SYSINIT&&st.expected==8&&!st.pending&&st.codec_frozen&&!st.instance_open);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"fault_freeze_sysinit ok\n");
}
/* R009-F1: frozen at OPEN — reply 1 drains but the header parse can never
 * issue its channel command: phase/expected stay, instance opens on the
 * drained reply yet no 0x2040/0x2080 traffic appears. */
static void fault_freeze_open()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,160,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 wait_pending_armed(n,I9100_MFC_N_OPEN);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 mfc_response(1,2);dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_OPEN&&st.expected==1&&!st.pending&&st.codec_frozen&&st.instance_open);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"fault_freeze_open ok\n");
}
/* R009-F1: frozen at HEADER — reply 4 drains but INIT_BUFFERS can never
 * issue: phase stays N_HEADER, no decoder bind traffic. */
static void fault_freeze_header()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,170,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 wait_pending_armed(n,I9100_MFC_N_OPEN);
 mfc_irq_reply(1,2);assert(mfc_wait_phase(n,I9100_MFC_N_HEADER));
 wait_pending_armed(n,I9100_MFC_N_HEADER);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x2008/4]=640;mfc_regs[MF_MFC][0x2004/4]=480;
 mfc_regs[MF_MFC][0x200c/4]=4;mfc_regs[MF_MFC][0x2018/4]=sizeof(mfc_stream);
 mfc_response(4);dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_HEADER&&st.expected==4&&!st.pending&&st.codec_frozen&&st.width==640);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"fault_freeze_header ok\n");
}
/* R009-F1: frozen at a LAST (EOS) frame — the reply drains but the drain
 * continuation that LAST requires is a codec command and never issues:
 * no EMPTY fabrication, the mailbox retires quiet, and the phase stays
 * exactly at N_FRAME where the fault declaration caught it. */
static void fault_freeze_last()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,180,mfc_stream,sizeof(mfc_stream),4,1,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 frame_reply(sizeof(mfc_stream),0,0,4);mfc_response(5);dead_clock_window();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_FRAME&&!st.pending&&st.codec_frozen&&!st.drained);}
 /* The drain continuation for LAST never issued: zero command traffic. */
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws)&&ws.fault&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"fault_freeze_last ok\n");
}
/* R009-F1: no-active residue consumption — after the faulted frame's
 * retirement drains, the still-published record is re-read on every
 * subsequent idle pass. Each re-consumption replays the idempotent codec
 * freeze, delivers no further callback, drains nothing new and issues no
 * codec traffic; the first-fault error stays latched. */
static void irq_fault_idle_wake()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,190,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);dead_clock_window();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 wait_retired(n,w);
 /* Idle windows with the record still published: repeated consumption
  * replays the freeze, never a second callback or codec traffic. */
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 for(unsigned i=0;i<16;i++){mfc_advance_us(20000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(f.calls==1);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_FRAME&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error==I9100_MP_OWNER&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 mfc_done g;assert(i9100_mfc_worker_frame(w,&g,191,mfc_stream,sizeof(mfc_stream),2,0,mfc_on_done)!=0&&!g.calls);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"irq_fault_idle_wake ok\n");
}
/* R009-F2: the record's generation lifecycle — a retired binding clears
 * its own accusation, a rebound generation reads empty, and a fresh
 * dead-domain window publishes a brand-new record for the new binding. */
static void irq_fault_generation()
{
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,190,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);dead_clock_window();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(!i9100_mfc_worker_stop(w));
 assert(!i9100_mfc_worker_destroy(&w)&&!w);
 /* The accused binding retired its own record: a rebound token inherits
  * nothing and reads the empty record. */
 i9100_mfc_board_irq_token t={};assert(!i9100_mfc_board_irq_acquire(b,noop_notify,n,&t));
 int code=9;assert(!i9100_mfc_board_irq_fault(b,&t,&code)&&code==0);
 /* A fresh dead-domain dispatch on the old binding's slot cannot publish:
  * the slot is masked and carries no new handler for this token. */
 assert(!mfc_dispatch_irq126());
 assert(!i9100_mfc_board_irq_release(b,&t));
 assert(!i9100_mfc_native_close(&n)&&!n);assert(!i9100_mfc_board_close(&b)&&!b);
 mfc_dbg_w=nullptr;mfc_dbg_n=nullptr;
 fflush(0);fprintf(stderr,"irq_fault_generation ok\n");
}
/* R010: the exact-owner freeze return value is part of the control flow.
 * A BUSY freeze keeps codec_gate closed — request, arena, private input and
 * the masked IRQ stay retained while a real bounded timer retries — and only
 * a landed freeze may reopen the drain path. This single-BUSY window is the
 * minimal contract: the BOOT reply drains only after freeze lands on the
 * second attempt, and SYS_INIT can never issue in between. */
static void freeze_busy_once()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_freeze_busy=1;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 /* Two real service-window attempts: the BUSY one plus the landed one —
  * each behind a real queued timer or notify, never a spin. */
 assert(mfc_freeze_calls==fc0+2);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error==I9100_MP_OWNER&&!ws.in_flight&&!ws.freeze_pending&&!ws.freeze_refused&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_once ok\n");
}
/* The independent-review probe contract: the first three owner freeze calls
 * report BUSY. The old control flow still ran poll_ctl and issued SYS_INIT
 * (phase=SYSINIT expected=8 pending=1); the fixed flow must hold
 * phase=BOOT expected=9 pending=1 until the fourth attempt lands the
 * freeze, after which the reply drains frozen with zero command traffic. */
static void freeze_busy_thrice()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_freeze_busy=3;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+4);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_thrice ok\n");
}
/* Sustained BUSY: the freeze never lands inside FREEZE_TRIES bounded
 * retries, so codec_gate never reopens — no drain, no automatic shutdown,
 * no reply consumption and no request free. The wedged request latches as
 * honest retained state (fault, pending mailbox, masked IRQ, private input
 * held) and only the same owner's explicit worker_stop cleanup may prove
 * quiescence. */
static void freeze_busy_persistent()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_freeze_busy=1024;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 /* The bounded retry exhausts (64 real attempts behind real timers), the
  * retirement bound lapses, and the request must stay retained — never
  * freed, never auto-shutdown, mailbox left as evidence. */
 wait_retired(n,w);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status q={};
  if(!i9100_mfc_worker_snapshot(w,&q)&&q.in_flight&&q.freeze_pending)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(mfc_freeze_calls==fc0+64);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&st.pending&&!st.codec_frozen);}
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.in_flight&&ws.freeze_pending&&!ws.freeze_refused&&irq126_slot_field(0)==0);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 assert(!m.illegal_reads&&!m.illegal_writes);
 /* Exact-owner cleanup still ends the lifecycle: the explicit stop
  * shutdown proves quiescence, the retained request retires, and the
  * binding closes cleanly. */
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_persistent ok\n");
}
/* A hard (non-BUSY) refusal is permanent owner diagnosis: one real
 * attempt, never retried, the gate stays closed forever, and the request
 * retires only through the same owner's explicit cleanup. */
static void freeze_hard_refused()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_freeze_refuse=I9100_MFC_OWNER;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status q={};
  if(!i9100_mfc_worker_snapshot(w,&q)&&q.in_flight&&q.freeze_pending)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(mfc_freeze_calls==fc0+1);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.in_flight&&ws.freeze_pending&&ws.freeze_refused==I9100_MFC_OWNER&&irq126_slot_field(0)==0);}
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&st.pending&&!st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_hard_refused ok\n");
}
/* A refused fault query is owner-path damage itself: the real -847
 * diagnosis is latched (never swallowed as isr_fault=0), the freeze still
 * lands on the bound lease, and the BOOT reply drains frozen afterwards. */
static void freeze_query_negative()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,140,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_irq_fault_fail=2;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MFC_OWNER);
 wait_retired(n,w);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BOOT&&st.expected==9&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.error==I9100_MFC_OWNER&&!ws.in_flight&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_query_negative ok\n");
}
/* The freeze-BUSY gate at each real continuation phase: the reply may only
 * drain after the freeze lands; the next command can never issue in the
 * BUSY window. */
static void freeze_busy_sysinit()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,150,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(8,0x60000);
 mfc_freeze_busy=2;
 dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+3);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_SYSINIT&&st.expected==8&&!st.pending&&st.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_sysinit ok\n");
}
static void freeze_busy_open()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,160,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 wait_pending_armed(n,I9100_MFC_N_OPEN);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_response(1,2);
 mfc_freeze_busy=2;
 dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+3);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_OPEN&&st.expected==1&&!st.pending&&st.codec_frozen&&st.instance_open);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_open ok\n");
}
static void freeze_busy_header()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,170,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 wait_pending_armed(n,I9100_MFC_N_OPEN);
 mfc_irq_reply(1,2);assert(mfc_wait_phase(n,I9100_MFC_N_HEADER));
 wait_pending_armed(n,I9100_MFC_N_HEADER);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x2008/4]=640;mfc_regs[MF_MFC][0x2004/4]=480;
 mfc_regs[MF_MFC][0x200c/4]=4;mfc_regs[MF_MFC][0x2018/4]=sizeof(mfc_stream);
 mfc_response(4);
 mfc_freeze_busy=2;
 dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+3);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_HEADER&&st.expected==4&&!st.pending&&st.codec_frozen&&st.width==640);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_header ok\n");
}
/* BUFFERS: reply 15 only drains after the freeze lands — the READY
 * transition can never run in the BUSY window. */
static void freeze_busy_buffers()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,175,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 mfc_irq_reply(9);assert(mfc_wait_phase(n,I9100_MFC_N_SYSINIT));
 wait_pending_armed(n,I9100_MFC_N_SYSINIT);
 mfc_irq_reply(8,0x60000);assert(mfc_wait_phase(n,I9100_MFC_N_OPEN));
 wait_pending_armed(n,I9100_MFC_N_OPEN);
 mfc_irq_reply(1,2);assert(mfc_wait_phase(n,I9100_MFC_N_HEADER));
 wait_pending_armed(n,I9100_MFC_N_HEADER);
 mfc_regs[MF_MFC][0x2000/4]=2;mfc_regs[MF_MFC][0x2008/4]=640;mfc_regs[MF_MFC][0x2004/4]=480;
 mfc_regs[MF_MFC][0x200c/4]=4;mfc_regs[MF_MFC][0x2018/4]=sizeof(mfc_stream);
 mfc_irq_reply(4);assert(mfc_wait_phase(n,I9100_MFC_N_BUFFERS));
 wait_pending_armed(n,I9100_MFC_N_BUFFERS);
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes,fc0=mfc_freeze_calls;
 mfc_regs[MF_MFC][0x2000/4]=2;
 mfc_response(15);
 mfc_freeze_busy=2;
 dead_clock_window();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+3);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_BUFFERS&&st.expected==15&&!st.pending&&st.codec_frozen&&st.dpb_count==4);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_buffers ok\n");
}
/* LAST: the EOS drain continuation is itself a codec command — under a
 * BUSY freeze the frame reply drains only after the freeze lands, and the
 * drain command never issues. */
static void freeze_busy_last()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,180,mfc_stream,sizeof(mfc_stream),4,1,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 frame_reply(sizeof(mfc_stream),0,0,4);mfc_response(5);
 unsigned fc0=mfc_freeze_calls;
 mfc_freeze_busy=2;
 dead_clock_window();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 wait_retired(n,w);
 assert(mfc_freeze_calls==fc0+3);
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_FRAME&&!st.pending&&st.codec_frozen&&!st.drained);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&!ws.in_flight&&!ws.freeze_pending&&irq126_slot_field(0)==0);}
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_last ok\n");
}
/* Idle residue under the same qualification: a permanently refused freeze
 * retires the request as retained wedged state, and every later idle pass
 * keeps codec_gate closed — no poll, no residue drain, no codec traffic —
 * until the exact owner's explicit cleanup. */
static void freeze_busy_idle()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 mfc_done d;boot_engine(w,n,d);
 mfc_done f;assert(!i9100_mfc_worker_frame(w,&f,190,mfc_stream,sizeof(mfc_stream),1,0,mfc_on_done));
 {struct i9100_mfc_native_status st={};unsigned p=0;
  for(;p<4000;p++){if(!i9100_mfc_native_status(n,&st)&&st.pending&&
   (mfc_regs[MF_GIC][0x10c/4]&(1U<<30)))break;
   mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(p<4000);}
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 frame_reply(sizeof(mfc_stream),1,0,1);mfc_response(5);
 unsigned fc0=mfc_freeze_calls;
 mfc_freeze_busy=1024;
 dead_clock_window();
 assert(mfc_wait_done(f)&&f.calls==1&&f.last==I9100_MP_OWNER);
 /* The request wedges retained (freeze never lands), and the mailbox
  * stays pending as evidence. */
 wait_retired(n,w);
 for(unsigned i=0;i<4000;i++){
  struct i9100_mfc_worker_status q={};
  if(!i9100_mfc_worker_snapshot(w,&q)&&q.in_flight&&q.freeze_pending)break;
  mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(mfc_freeze_calls==fc0+64);
 {struct i9100_mfc_worker_status ws;assert(!i9100_mfc_worker_snapshot(w,&ws));
  assert(ws.fault&&ws.in_flight&&ws.freeze_pending&&irq126_slot_field(0)==0);}
 {struct i9100_mfc_native_status st;assert(!i9100_mfc_native_status(n,&st));
  assert(st.phase==I9100_MFC_N_FRAME&&st.pending&&!st.codec_frozen);}
 /* Idle windows while the wedge is retained: the still-published record
  * is re-consumed, the owed freeze keeps every codec call closed and the
  * residue is never drained without qualification. */
 for(unsigned i=0;i<16;i++){mfc_advance_us(20000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 assert(f.calls==1&&mfc_freeze_calls==fc0+64);
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 assert(!m.illegal_reads&&!m.illegal_writes);
 assert(!i9100_mfc_worker_stop(w));
 close_all(b,n,w);fflush(0);fprintf(stderr,"freeze_busy_idle ok\n");
}
/* R011 (the retained-review probe's contract): a freeze that never lands
 * drives the request to the retire_stuck wedge (active=1 pending=1 fault=1
 * freeze_pending=1). A same owner/token cancel then wakes the loop and the
 * very next native_status is injected with the real API's BUSY — the old
 * control flow fell into the generic TIMEOUT/-1 finish and freed the
 * request while the mailbox still showed pending (active=0 pending=1).
 * Unknown state must keep the retention: request, private input, arenas,
 * control lease, masked IRQ and the single callback all stay until proven
 * quiescence or the same owner's explicit cleanup. */
static void retire_stuck_retention()
{
 mfc_freeze_busy=0;mfc_freeze_refuse=0;mfc_irq_fault_fail=0;mfc_status_busy=0;
 struct i9100_mfc_board *b;struct i9100_mfc_native *n=open_engine(&b);
 struct i9100_mfc_worker *w=nullptr;assert(!i9100_mfc_worker_create(1,n,b,&w));mfc_dbg_w=w;mfc_dbg_n=n;
 auto pf=mfc_profile();pf.timeout_ms=5000;
 mfc_done d;assert(!i9100_mfc_worker_boot(w,&d,160,&pf,mfc_fw,sizeof(mfc_fw),mfc_stream,sizeof(mfc_stream),mfc_on_done));
 wait_pending_armed(n,I9100_MFC_N_BOOT);
 assert(!mfc_loops.empty());auto ls=mfc_state(mfc_loops[0].first);assert(ls);ls->gate.lock();
 mfc_response(9);
 mfc_regs[MF_CMU][0xc928/4]&=~7U;
 assert(mfc_dispatch_irq126());
 mfc_regs[MF_CMU][0xc928/4]|=7U;
 mfc_freeze_busy=100000;
 ls->gate.unlock();
 assert(mfc_wait_done(d)&&d.calls==1&&d.last==I9100_MP_OWNER);
 /* Drive the real bounded timers to the retire_stuck wedge. */
 {unsigned p=0;
  for(;p<4000;p++){
   struct i9100_mfc_native_status ns={};struct i9100_mfc_worker_status q={};
   if(!i9100_mfc_native_status(n,&ns)&&ns.pending&&
    !i9100_mfc_worker_snapshot(w,&q)&&q.in_flight&&q.freeze_pending)break;
   mfc_advance_us(100000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(p<4000);}
 {struct i9100_mfc_native_status ns;struct i9100_mfc_worker_status q;
  assert(!i9100_mfc_native_status(n,&ns)&&!i9100_mfc_worker_snapshot(w,&q));
  assert(q.in_flight&&ns.pending&&q.fault&&q.freeze_pending);}
 unsigned c0=m.cmd_writes,h0=m.ch_writes,x0=m.mbx_writes;
 /* Same owner/token cancel + injected BUSY status: the status budget must
  * drain (the unknown-state status really ran inside the worker pass) and
  * the retained request must not release. */
 mfc_status_busy=1;
 i9100_mfc_worker_cancel(w,&d,160);
 {unsigned p=0;
  for(;p<4000&&mfc_status_busy.load();p++){mfc_advance_us(2000);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  assert(!mfc_status_busy.load());}
 {struct i9100_mfc_native_status ns;struct i9100_mfc_worker_status q;
  assert(!i9100_mfc_native_status(n,&ns)&&!i9100_mfc_worker_snapshot(w,&q));
  assert(q.in_flight&&ns.pending&&q.fault&&d.calls==1);}
 /* A repeated same-owner cancel, a refused fault query and late timer
  * windows: none may release the retention or fire a second callback. */
 i9100_mfc_worker_cancel(w,&d,160);
 mfc_irq_fault_fail=1;
 for(unsigned i=0;i<16;i++){mfc_advance_us(20000);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 {struct i9100_mfc_native_status ns;struct i9100_mfc_worker_status q;
  assert(!i9100_mfc_native_status(n,&ns)&&!i9100_mfc_worker_snapshot(w,&q));
  assert(q.in_flight&&ns.pending&&q.fault&&d.calls==1);
  assert(ns.phase==I9100_MFC_N_BOOT&&ns.expected==9&&!ns.codec_frozen);}
 assert(m.cmd_writes==c0&&m.ch_writes==h0&&m.mbx_writes==x0);
 assert(!m.illegal_reads&&!m.illegal_writes);
 /* The explicit same-owner cleanup still ends the books: the shutdown
  * proves the mailbox quiet, the retained request retires with its single
  * delivered callback, and the binding releases every resource. */
 assert(!i9100_mfc_worker_stop(w));
 {struct i9100_mfc_native_status ns;struct i9100_mfc_worker_status q;
  assert(!i9100_mfc_native_status(n,&ns)&&!i9100_mfc_worker_snapshot(w,&q));
  assert(!ns.pending&&!q.in_flight&&d.calls==1);}
 close_all(b,n,w);fflush(0);fprintf(stderr,"retire_stuck_retention ok\n");
}
int main()
{
 raw_controller();fake_control();wrong_control();owner_fault_latches();owned_active();borrowed_stop();
 external_cancel();cancel_retirement();irq_no_clock_retirement();callback_gate_accounting();blocked_loop_accounting();
 no_completion_bit();stale_reply();wrong_reply_fault();all_borrowed();alternate_slot_eos();busy_mc_stop();faulted_slot();
 deadline_late_reply();deadline_firmware();deadline_explicit_stop();deadline_shutdown_stuck();cancel_deadline();
 arm_failure_retention();idle_domain_loss();active_clock_loss();
 irq_late_clock_loss();irq_late_power_loss();irq_spurious_powered();post_fault_mask_held();irq_mask_write_loss();
 irq_fault_transient_clock();irq_fault_transient_power();irq_fault_notify_merge();irq_fault_status_repeat();irq_fault_cleanup_retry();
 irq_fault_transient_boot();fault_freeze_sysinit();fault_freeze_open();fault_freeze_header();fault_freeze_last();irq_fault_idle_wake();irq_fault_generation();
 freeze_busy_once();freeze_busy_thrice();freeze_busy_persistent();freeze_hard_refused();freeze_query_negative();
 freeze_busy_sysinit();freeze_busy_open();freeze_busy_header();freeze_busy_buffers();freeze_busy_last();freeze_busy_idle();
 retire_stuck_retention();
 puts("PASS R011 ownership/IRQ/gate regressions: provider-verified control lease, wrong-token no-damage refusal, owner-fault latch, real shutdown + busy-MC failure, in-flight cancel retirement, callback/blocked-loop bounded accounting, alternate-slot EOS, repaired fault-slot owner cleanup, deadline retirement (late reply / firmware deadline / explicit stop / wedged cleanup), cancel deadline, arm-failure retention, idle+active domain loss, late-IRQ domain qualification (clock/power loss), spurious dispatch, post-fault mask held, ISR mask write-loss repair, persistent ISR domain-fault handoff (transient clock/power windows, merged notify, repeated status, foreign/stale token, cleanup retry), publish-once immutable record + generation retirement, fault-declared codec freeze (BOOT/SYSINIT/OPEN/HEADER/LAST/idle-wake never continue, drained mailbox only), freeze-return control flow (BUSY once/thrice/persistent, hard refusal, refused fault query latched as real diagnosis, retained wedge + exact-owner cleanup, idle residue gated), retire_stuck unknown-state retention (status BUSY/repeat cancel/refused query/late wake never release, explicit owner cleanup ends the books); explicit facades, hardware=false");
 return 0;
}
