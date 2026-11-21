/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_irq.h"
#include <string.h>
struct i9100_mfc_irq {
 struct i9100_mfc_board *board;struct i9100_mfc_board_irq_token token;
 i9100_mfc_irq_notify sink;uintptr_t self;
 volatile uint32_t delivered,spurious;uint32_t bound,armed,generation;int error,cleanup_error;
};
static int forward(void *p)
{
 i9100_mfc_irq *q=(i9100_mfc_irq *)p;
 if(!q||q->self!=(uintptr_t)q)return -1;
 if(!q->sink.signal){__sync_add_and_fetch(&q->spurious,1);return 0;}
 __sync_add_and_fetch(&q->delivered,1);q->sink.signal(q->sink.cookie);return 0;
}
/* Storage is owned by the caller: C translation units cannot reach IOMalloc's
 * C++ linkage, so the (C++) owner allocates i9100_mfc_irq_storage() bytes and
 * frees them only after a successful close. A failed close keeps the object
 * and its board token live so the retention path can retry. */
unsigned long i9100_mfc_irq_storage(void){return (unsigned long)sizeof(i9100_mfc_irq);}
uintptr_t i9100_mfc_irq_token_id(i9100_mfc_irq *q)
{return q&&q->self==(uintptr_t)q&&(uintptr_t)&q->token?q->token.self:0;}
int i9100_mfc_irq_open(int enabled,struct i9100_mfc_board *board,void *storage,unsigned long bytes,i9100_mfc_irq **out)
{
 if(!out||*out||enabled!=1||!board||!storage||bytes<(unsigned long)sizeof(i9100_mfc_irq))return I9100_MFC_ARGUMENT;
 i9100_mfc_irq *q=(i9100_mfc_irq *)storage;
 memset(q,0,sizeof(*q));q->self=(uintptr_t)q;q->board=board;
 int e=i9100_mfc_board_irq_acquire(board,(void (*)(void *))forward,q,&q->token);
 if(e){q->self=0;return e;}q->bound=1;*out=q;return 0;
}
int i9100_mfc_irq_bind(i9100_mfc_irq *q,const i9100_mfc_irq_notify *sink)
{
 if(!q||q->self!=(uintptr_t)q||!sink||!sink->signal)return I9100_MFC_ARGUMENT;
 if(!q->bound||q->sink.signal||q->error)return I9100_MFC_OWNER;
 q->sink=*sink;return 0;
}
int i9100_mfc_irq_arm(i9100_mfc_irq *q)
{
 if(!q||q->self!=(uintptr_t)q)return I9100_MFC_ARGUMENT;
 if(!q->bound||!q->sink.signal)return I9100_MFC_OWNER;
 if(q->error)return q->error;
 int e=i9100_mfc_board_irq_unmask(q->board,&q->token);
 if(!e){q->armed=1;q->generation++;}else if(e!=I9100_MP_BUSY&&!q->error)q->error=e;
 return e;
}
int i9100_mfc_irq_disarm(i9100_mfc_irq *q)
{
 if(!q||q->self!=(uintptr_t)q)return I9100_MFC_ARGUMENT;
 if(!q->bound)return I9100_MFC_OWNER;
 int e=i9100_mfc_board_irq_mask(q->board,&q->token);
 if(!e)q->armed=0;else if(e!=I9100_MP_BUSY&&!q->error)q->error=e;
 return e;
}
int i9100_mfc_irq_repair(i9100_mfc_irq *q)
{
 if(!q||q->self!=(uintptr_t)q)return I9100_MFC_ARGUMENT;
 if(!q->bound)return I9100_MFC_OWNER;
 int e=i9100_mfc_board_irq_repair(q->board,&q->token);
 if(e&&e!=I9100_MP_BUSY&&!q->error)q->error=e;
 return e;
}
int i9100_mfc_irq_ack(i9100_mfc_irq *q)
{
 if(!q||q->self!=(uintptr_t)q)return I9100_MFC_ARGUMENT;
 if(!q->bound)return I9100_MFC_OWNER;
 return i9100_mfc_board_irq_ack(q->board,&q->token);
}
int i9100_mfc_irq_status(i9100_mfc_irq *q,struct i9100_mfc_irq_status *out)
{
 if(!q||q->self!=(uintptr_t)q||!out)return I9100_MFC_ARGUMENT;
 memset(out,0,sizeof(*out));out->bound=q->bound;out->armed=q->armed;
 out->delivered=q->delivered;out->spurious=q->spurious;out->generation=q->generation;
 out->error=q->error;out->cleanup_error=q->cleanup_error;
 /* A negative slot/dispatch query is a real fault, not enabled=0: propagate
  * it so a caller can never read a dropped write or lost lease as success. */
 if(q->bound){int e=i9100_mfc_board_irq_line(q->board,&q->token,&out->enabled,&out->active,&out->dispatch);
  if(e){if(!out->error)out->error=e;return e;}}
 return 0;
}
/* The board publishes a persistent ISR-observed domain fault keyed to this
 * exact token self+generation; consume it before any poll/service path so
 * a resource restored inside the dispatch-to-service window cannot launder
 * the fault into a clean pass. Non-destructive: the record survives
 * repeated queries and merged notifies until this binding's own
 * release/cleanup retires it. */
int i9100_mfc_irq_fault(i9100_mfc_irq *q,int *code)
{
 if(!q||q->self!=(uintptr_t)q||!code)return I9100_MFC_ARGUMENT;
 *code=0;
 if(!q->bound)return I9100_MFC_OWNER;
 return i9100_mfc_board_irq_fault(q->board,&q->token,code);
}
int i9100_mfc_irq_close(i9100_mfc_irq **owner)
{
 if(!owner||!*owner||(*owner)->self!=(uintptr_t)*owner)return I9100_MFC_ARGUMENT;
 i9100_mfc_irq *q=*owner;int e=0;
 /* Normal teardown masks then releases. A faulted slot refuses the normal
  * token gate forever, so either stage may fall back to the exact-owner
  * cleanup which re-proves mask plus dispatch/EOI quiet before releasing.
  * Any failure keeps the binding and storage so a later retry can resume
  * the same stage; the original slot diagnostics are never erased. */
 if(q->armed){
  e=i9100_mfc_irq_disarm(q);
  if(e==I9100_MP_OWNER){e=i9100_mfc_board_irq_cleanup(q->board,&q->token);if(!e){q->armed=0;q->bound=0;}}
  if(e){if(!q->cleanup_error)q->cleanup_error=e;return e;}q->armed=0;
 }
 if(q->bound){
  e=i9100_mfc_board_irq_release(q->board,&q->token);
  if(e==I9100_MP_OWNER){e=i9100_mfc_board_irq_cleanup(q->board,&q->token);if(!e)q->bound=0;}
  if(e){if(!q->cleanup_error)q->cleanup_error=e;return e;}q->bound=0;
 }
 q->self=0;*owner=0;return 0; /* caller frees the storage */
}
