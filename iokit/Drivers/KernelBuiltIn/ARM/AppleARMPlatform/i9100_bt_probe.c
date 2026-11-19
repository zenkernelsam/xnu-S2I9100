/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_probe.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn||(x+an>y&&y+bn>x);}
static int valid(const struct i9100_bt_probe *p)
{return p&&p->status.initialized&&p->self==(uintptr_t)p;}
static int fault(struct i9100_bt_probe *p,int e)
{p->status.fault=1;p->status.phase=I9100_BP_FAILED;if(!p->status.error)p->status.error=e;return e;}
static int verify(struct i9100_bt_probe *p)
{
 if(p->io.verify_link(p->io.cookie,p->status.generation))return fault(p,I9100_BT_BOOT_OWNER);
 uint64_t now=p->io.h4.now_ns(p->io.h4.cookie);
 if(now<p->status.last_ns)return fault(p,I9100_BT_BOOT_CLOCK);p->status.last_ns=now;
 if(p->status.phase!=I9100_BP_PREPARED&&now-p->status.started_ns>(uint64_t)p->timeout_ms*1000000ULL)return fault(p,I9100_BT_BOOT_TIMEOUT);
 return 0;
}
static int guarded_lock(void *cookie)
{struct i9100_bt_probe *p=(struct i9100_bt_probe *)cookie;
 if(!p->status.fault&&!p->status.closed&&p->status.phase!=I9100_BP_FAILED&&p->io.verify_link(p->io.cookie,p->status.generation)){
  (void)fault(p,I9100_BT_BOOT_OWNER);return -1;}
 return p->io.h4.lock(p->io.h4.cookie);}
static void guarded_unlock(void *cookie)
{struct i9100_bt_probe *p=(struct i9100_bt_probe *)cookie;p->io.h4.unlock(p->io.h4.cookie);}
static int guarded_write(void *cookie,const uint8_t *bytes,unsigned n,unsigned *sent)
{struct i9100_bt_probe *p=(struct i9100_bt_probe *)cookie;return p->io.h4.write(p->io.h4.cookie,bytes,n,sent);}
static int guarded_quiesce(void *cookie)
{struct i9100_bt_probe *p=(struct i9100_bt_probe *)cookie;return p->io.h4.quiesce(p->io.h4.cookie);}
static uint64_t guarded_time(void *cookie)
{struct i9100_bt_probe *p=(struct i9100_bt_probe *)cookie;return p->io.h4.now_ns(p->io.h4.cookie);}
static int send(struct i9100_bt_probe *p,uint32_t phase,uint16_t opcode)
{
 int e=verify(p);if(e)return e;
 if(p->h4.pending||p->deferred_opcode)return fault(p,I9100_BT_BOOT_BUSY);
 if(!p->h4.credits){p->deferred_opcode=opcode;p->deferred_phase=phase;p->credit_started=p->status.last_ns;return 0;}
 p->status.phase=phase;e=i9100_bt_command(&p->h4,opcode,0,0);return e?fault(p,e):0;
}
static int unusable(const uint8_t *p)
{unsigned zero=0,ones=0;for(unsigned n=0;n<6;n++){zero|=p[n];ones|=p[n]^255U;}
 const uint8_t original_default[6]={0,0,0,0xb1,0x30,0x43};return !zero||!ones||!memcmp(p,original_default,6);}
static int completed(struct i9100_bt_probe *p,const struct i9100_bt_frame *f)
{
 if(f->type!=4U||f->length<6U||f->bytes[0]!=0x0eU||f->bytes[1]+2U!=f->length||
    u16(f->bytes+3)!=p->h4.last_opcode||p->h4.last_completion!=0x0eU||f->bytes[5])return fault(p,I9100_BT_FRAME);
 const uint8_t *data=f->bytes+5;unsigned n=f->length-5U;
 switch(p->status.phase){
 case I9100_BP_VERSION:
  if(n!=9U)return fault(p,I9100_BT_FRAME);memcpy(p->status.version_raw,data,9);
  p->status.version=(struct i9100_bt_version){data[1],data[4],u16(data+2),u16(data+5),u16(data+7)};
  p->status.valid_fields|=1U;if(p->status.version.manufacturer!=15U)return fault(p,I9100_BT_BOOT_ID);
  return send(p,I9100_BP_NAME,0x0c14U);
 case I9100_BP_NAME:
  if(n!=249U)return fault(p,I9100_BT_FRAME);memcpy(p->status.name_raw,data,249);p->status.valid_fields|=2U;
  return send(p,I9100_BP_ADDRESS,0x1009U);
 case I9100_BP_ADDRESS:
  if(n!=7U)return fault(p,I9100_BT_FRAME);memcpy(p->status.address_raw,data,7);p->status.address_unusable=unusable(data+1);p->status.valid_fields|=4U;
  return send(p,I9100_BP_BUFFER,0x1005U);
 case I9100_BP_BUFFER:
  if(n!=8U)return fault(p,I9100_BT_FRAME);memcpy(p->status.buffer_raw,data,8);p->status.valid_fields|=8U;
  return send(p,I9100_BP_FEATURES,0x1003U);
 case I9100_BP_FEATURES:
  if(n!=9U)return fault(p,I9100_BT_FRAME);memcpy(p->status.features_raw,data,9);p->status.valid_fields|=16U;
  p->status.completed=1;p->status.phase=I9100_BP_COMPLETE;return 0;
 default:return fault(p,I9100_BT_UNMATCHED);
 }
}
int i9100_bt_probe_init(struct i9100_bt_probe *p,int enabled,const struct i9100_bt_probe_io *io,uint32_t generation,uint32_t timeout)
{
 if(!p||p->status.initialized)return I9100_BT_BOOT_ARGUMENT;if(enabled!=1)return I9100_BT_BOOT_DISABLED;
 if(!io||!generation||timeout<5000U||timeout>30000U||!io->read||!io->verify_link||!io->h4.lock||!io->h4.unlock||
    !io->h4.write||!io->h4.quiesce||!io->h4.now_ns||overlap(p,sizeof(*p),io,sizeof(*io)))return I9100_BT_BOOT_ARGUMENT;
 if(io->verify_link(io->cookie,generation))return I9100_BT_BOOT_OWNER;
 memset(p,0,sizeof(*p));p->io=*io;p->self=(uintptr_t)p;p->timeout_ms=timeout;
 p->status.initialized=p->status.enabled=1;p->status.phase=I9100_BP_PREPARED;p->status.generation=generation;
 struct i9100_bt_io guarded={p,guarded_lock,guarded_unlock,guarded_write,guarded_quiesce,guarded_time};
 int e=i9100_bt_init(&p->h4,&guarded);if(!e)e=i9100_bt_enable(&p->h4,1,generation);return e?fault(p,e):0;
}
int i9100_bt_probe_start(struct i9100_bt_probe *p)
{
 if(!valid(p))return I9100_BT_BOOT_ARGUMENT;if(p->status.closed||p->status.fault||p->status.phase!=I9100_BP_PREPARED)return I9100_BT_BOOT_BUSY;
 int e=verify(p);if(e)return e;unsigned bytes=0;
 if(p->io.read(p->io.cookie,p->rx,sizeof(p->rx),&bytes))return fault(p,I9100_BT_BOOT_IO);
 if(bytes)return fault(p,I9100_BT_FRAME);p->status.started_ns=p->status.last_ns;return send(p,I9100_BP_VERSION,0x1001U);
}
int i9100_bt_probe_step(struct i9100_bt_probe *p,unsigned budget)
{
 if(!valid(p)||!budget||budget>16U)return I9100_BT_BOOT_ARGUMENT;
 if(p->status.closed||!p->status.enabled)return I9100_BT_BOOT_DISABLED;if(p->status.fault)return I9100_BT_BOOT_FAULT;
 if(p->status.completed)return 0;if(p->status.phase==I9100_BP_PREPARED)return I9100_BT_BOOT_BUSY;
 uint64_t last=p->status.last_ns;int e=verify(p);if(e)return e;
 if(last==p->status.last_ns){if(++p->frozen_steps>=4096U)return fault(p,I9100_BT_BOOT_CLOCK);}else p->frozen_steps=0;
 if(p->status.steps!=UINT32_MAX)p->status.steps++;
 e=i9100_bt_poll(&p->h4);if(e&&e!=I9100_BT_BUSY)return fault(p,e);
 unsigned frames=0,read_bytes=0;
 while(frames<budget&&read_bytes<1024U&&!p->status.completed){
  if(p->rx_used==p->rx_offset){p->rx_used=p->rx_offset=0;unsigned got=0;
   if(p->io.read(p->io.cookie,p->rx,sizeof(p->rx),&got))return fault(p,I9100_BT_BOOT_IO);
   if(got>sizeof(p->rx))return fault(p,I9100_BT_FRAME);p->rx_used=got;if(!got)break;}
  unsigned consumed=0;struct i9100_bt_frame f={0};e=i9100_bt_feed(&p->h4,p->rx+p->rx_offset,p->rx_used-p->rx_offset,&consumed,&f);
  p->rx_offset+=consumed;read_bytes+=consumed;if(!consumed||p->rx_offset>p->rx_used)return fault(p,I9100_BT_FRAME);
  if(f.type==4U&&f.bytes&&f.length<=sizeof(p->status.last_event)){memcpy(p->status.last_event,f.bytes,f.length);p->status.last_event_bytes=f.length;}
  if(e<0)return fault(p,e);if(!f.bytes)continue;frames++;if(p->status.packets!=UINT32_MAX)p->status.packets++;
  if(f.type!=4U)return fault(p,I9100_BT_FRAME);
  if(f.bytes[0]==0x0eU&&f.length==5U&&!u16(f.bytes+3))continue;
  if(f.bytes[0]==0x0eU||f.bytes[0]==0x0fU){e=completed(p,&f);if(e)return e;}
  else if(f.bytes[0]==0x10U)return fault(p,I9100_BT_CONTROLLER);
  else if(p->status.unsolicited!=UINT32_MAX)p->status.unsolicited++;
 }
 if(p->deferred_opcode){
  if(p->status.last_ns-p->credit_started>1000000000ULL)return fault(p,I9100_BT_TIMEOUT);
  if(p->h4.credits){uint16_t opcode=p->deferred_opcode;uint32_t phase=p->deferred_phase;p->deferred_opcode=0;return send(p,phase,opcode);}
 }
 return 0;
}
int i9100_bt_probe_stop(struct i9100_bt_probe *p)
{
 if(!valid(p))return I9100_BT_BOOT_ARGUMENT;if(p->status.closed)return 0;p->status.phase=I9100_BP_FAILED;
 int e=p->h4.enabled?i9100_bt_enable(&p->h4,0,0):0;if(e){p->status.cleanup_error=e;return e;}
 p->status.enabled=0;p->status.closed=1;p->status.phase=I9100_BP_CLOSED;p->status.cleanup_error=0;return 0;
}
int i9100_bt_probe_status(const struct i9100_bt_probe *p,struct i9100_bt_probe_status *out)
{if(!valid(p)||!out||overlap(p,sizeof(*p),out,sizeof(*out)))return I9100_BT_BOOT_ARGUMENT;
 *out=p->status;out->pending_opcode=p->h4.pending?p->h4.opcode:p->deferred_opcode;return 0;}
