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
int main()
{
 raw_controller();fake_control();wrong_control();owner_fault_latches();owned_active();borrowed_stop();
 external_cancel();cancel_retirement();irq_no_clock_retirement();callback_gate_accounting();blocked_loop_accounting();
 no_completion_bit();stale_reply();wrong_reply_fault();all_borrowed();alternate_slot_eos();busy_mc_stop();faulted_slot();
 deadline_late_reply();deadline_firmware();deadline_explicit_stop();deadline_shutdown_stuck();cancel_deadline();
 arm_failure_retention();idle_domain_loss();active_clock_loss();
 puts("PASS R006 ownership/IRQ/gate regressions: provider-verified control lease, wrong-token no-damage refusal, owner-fault latch, real shutdown + busy-MC failure, in-flight cancel retirement, callback/blocked-loop bounded accounting, alternate-slot EOS, repaired fault-slot owner cleanup, deadline retirement (late reply / firmware deadline / explicit stop / wedged cleanup), cancel deadline, arm-failure retention, idle+active domain loss; explicit facades, hardware=false");
 return 0;
}
