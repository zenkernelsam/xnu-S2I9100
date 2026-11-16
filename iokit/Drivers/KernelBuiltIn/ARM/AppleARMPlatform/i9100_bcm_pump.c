/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_pump.h"
#include "i9100_bcm_backplane_private.h"
#include <string.h>
enum {STATUS=0x20,TO_SB=0x40,FROM_FW=0x4c,FC_STATE=0x10,FC_CHANGE=0x20,FRAME=0x40,MAIL=0x80,
 CHIP_ACTIVE=0x20000000U,OWN_EVENTS=0x200000e0U,FATAL_STATUS=0x5000ff00U};
static int overlap(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a>UINTPTR_MAX-an||b>UINTPTR_MAX-bn||(a+an>b&&b+bn>a);}
static void wipe(void *v,unsigned bytes){volatile uint8_t *p=v;while(bytes--)*p++=0;}
static void increment(uint32_t *v){if(*v!=UINT32_MAX)(*v)++;}
static struct i9100_bcm4330 *bus(struct i9100_bcm_pump *p){return p->control->bus;}
static int take(struct i9100_bcm_pump *p)
{return !p||!p->initialized||!p->enabled||p->self!=(uintptr_t)p?I9100_PUMP_ARGUMENT:p->io.lock(p->io.cookie)?I9100_PUMP_BUSY:0;}
static void drop(struct i9100_bcm_pump *p){p->io.unlock(p->io.cookie);}
static int live(struct i9100_bcm_pump *p)
{
 struct i9100_bcm4330 *b=bus(p);struct i9100_bcm_f2 *f=p->boot->packets;
 return p->boot->initialized&&p->boot->self==(uintptr_t)p->boot&&p->boot->ready&&p->boot->running&&
  p->boot->generation==p->generation&&b->boot_owner==p->boot&&b->enabled&&b->protocol_ready&&!b->fault&&
  b->protocol_generation==p->generation&&b->control_owner==p->control&&b->rx_owner==p&&
  f->enabled&&!f->fault&&f->generation==p->generation&&f->rx_owner==p&&f->card->active&&!f->card->fault;
}
static int fail(struct i9100_bcm_pump *p,int error)
{
 p->fault=1;p->ready=0;p->error=error;
 struct i9100_bcm4330 *b=bus(p);if(!b->io.claim(b->io.cookie)){b->fault=1;b->io.release(b->io.cookie);}
 p->quarantine_error=i9100_wlan_quarantine(p->control,error);
 return error;
}
static int rd(struct i9100_bcm_pump *p,unsigned offset,uint32_t *v)
{return i9100_bcm_bp_read_locked(bus(p),p->boot->sdio_base+offset,v)?I9100_PUMP_IO:0;}
static int wr(struct i9100_bcm_pump *p,unsigned offset,uint32_t v)
{return i9100_bcm_bp_write_locked(bus(p),p->boot->sdio_base+offset,v)?I9100_PUMP_IO:0;}
static int close_locked(struct i9100_bcm_pump *p)
{
 if(p->fault&&p->bound)(void)i9100_wlan_quarantine(p->control,p->error);
 if(p->bound&&p->control->state!=I9100_WLAN_IDLE&&p->control->state!=I9100_WLAN_FAULT)return I9100_PUMP_BUSY;
 p->closing=1;p->ready=0;struct i9100_bcm4330 *b=bus(p);int e=0;
 if(p->bound&2U){
  if(b->io.claim(b->io.cookie))return I9100_PUMP_BUSY;
  if(b->rx_owner!=p||p->boot->packets->rx_owner!=p)e=I9100_PUMP_ARGUMENT;
  else{b->rx_owner=0;p->boot->packets->rx_owner=0;p->bound&=~2U;}
  b->io.release(b->io.cookie);if(e)return e;
 }
 if(p->bound&1U){e=i9100_wlan_consumer_unbind(p->control,p);if(e)return e;p->bound&=~1U;}
 wipe(p->frame,sizeof(p->frame));p->enabled=p->initialized=0;p->self=0;return 0;
}
int i9100_bcm_pump_init(struct i9100_bcm_pump *p,int enabled,struct i9100_bcm_boot *boot,struct i9100_wlan_control *c,const struct i9100_pump_io *io)
{
 if(!p||p->initialized||(enabled!=0&&enabled!=1))return I9100_PUMP_ARGUMENT;if(!enabled)return I9100_PUMP_DISABLED;
 if(!boot||!boot->initialized||!boot->ready||boot->self!=(uintptr_t)boot||!boot->firmware||!boot->packets||
    !c||!c->initialized||c->self!=(uintptr_t)c||!c->bus||c->generation!=boot->generation||
    !io||!io->lock||!io->unlock||!io->ethernet||
    overlap((uintptr_t)p,sizeof(*p),(uintptr_t)boot,sizeof(*boot))||
    overlap((uintptr_t)p,sizeof(*p),(uintptr_t)c,sizeof(*c))||overlap((uintptr_t)p,sizeof(*p),(uintptr_t)c->bus,sizeof(*c->bus))||
    overlap((uintptr_t)p,sizeof(*p),(uintptr_t)boot->packets,sizeof(*boot->packets))||overlap((uintptr_t)p,sizeof(*p),(uintptr_t)io,sizeof(*io)))return I9100_PUMP_ARGUMENT;
 if(io->lock(io->cookie))return I9100_PUMP_BUSY;
 memset(p,0,sizeof(*p));p->boot=boot;p->control=c;p->io=*io;p->self=(uintptr_t)p;p->initialized=p->enabled=1;p->generation=boot->generation;
 int e=i9100_wlan_consumer_bind(c,p);if(!e)p->bound=1;
 if(!e){struct i9100_bcm4330 *b=bus(p);if(b->io.claim(b->io.cookie))e=I9100_PUMP_BUSY;
  else{
   if(b->rx_owner||boot->packets->rx_owner||b->control_owner!=c||b->boot_owner!=boot||b->fault||!b->protocol_ready||
      !boot->packets->enabled||boot->packets->fault||boot->packets->card->fault||
      boot->packets->generation!=p->generation||b->protocol_generation!=p->generation)e=I9100_PUMP_DISABLED;
   else{b->rx_owner=p;boot->packets->rx_owner=p;p->bound=3;p->ready=1;}
   b->io.release(b->io.cookie);
  }
 }
 if(e){p->error=e;p->cleanup_error=close_locked(p);}
 io->unlock(io->cookie);return e;
}
static int poll(struct i9100_bcm_pump *p)
{
 struct i9100_bcm4330 *b=bus(p);if(b->io.claim(b->io.cookie))return I9100_PUMP_BUSY;
 int e=0;uint32_t status=0,fresh=0;p->stage=1;
 if(!live(p)){e=I9100_PUMP_IO;goto done;}
 e=rd(p,STATUS,&status);if(e)goto done;p->interrupt_status=status;
 if(status&FATAL_STATUS){e=I9100_PUMP_IO;goto done;}
 b->data_paused=!!(status&FC_STATE);
 if(status&OWN_EVENTS){e=wr(p,STATUS,status&OWN_EVENTS);if(e)goto done;}
 if(status&FC_CHANGE){
  e=rd(p,STATUS,&fresh);if(e)goto done;
  if(fresh&FATAL_STATUS){e=I9100_PUMP_IO;goto done;}
  b->data_paused=!!(fresh&(FC_STATE|FC_CHANGE));status|=fresh;p->interrupt_status=status;increment(&p->flow_updates);
  /* Preserve a crossed FC change for the next debounce, but acknowledge
   * fresh frame/misc/chip events before draining or mailbox ACK. */
  if(fresh&(OWN_EVENTS&~FC_CHANGE)){e=wr(p,STATUS,fresh&(OWN_EVENTS&~FC_CHANGE));if(e)goto done;}
 }
 if(status&MAIL){
  p->stage=2;e=rd(p,FROM_FW,&p->mailbox);if(e)goto done;increment(&p->mailbox_count);
  if(p->mailbox&0x10U){e=I9100_PUMP_HALTED;goto done;}
  if((p->mailbox&~0xffff001fU)||(p->mailbox&1U)||((p->mailbox&0x0aU)&&((p->mailbox>>16)&255U)!=4U)){e=I9100_PUMP_PROTOCOL;goto done;}
  e=wr(p,TO_SB,2U);if(e)goto done;
  if(p->mailbox&4U)b->flow_mask=(uint8_t)(p->mailbox>>24);
 }
 if(status&FRAME)p->pending_rx=1;
done:
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_pump_tick(struct i9100_bcm_pump *p,unsigned budget)
{
 if(!budget||budget>16U)return I9100_PUMP_ARGUMENT;int e=take(p);if(e)return e;
 if(p->closing){drop(p);return I9100_PUMP_DISABLED;}if(p->fault){e=p->error;drop(p);return e;}
 increment(&p->ticks);e=poll(p);if(e==I9100_PUMP_BUSY){drop(p);return e;}if(e){e=fail(p,e);goto done;}
 for(unsigned attempt=0;p->pending_rx&&attempt<budget;attempt++){
  unsigned bytes=0;p->stage=3;
  e=i9100_bcm_f2_receive_owned(p->boot->packets,p,p->frame,sizeof(p->frame),&bytes);
  if(e==I9100_F2_BUSY){e=I9100_PUMP_BUSY;goto done;}if(e){e=fail(p,I9100_PUMP_IO);goto done;}
  if(!bytes){p->pending_rx=0;increment(&p->empty_reads);break;}
  struct i9100_bcm_frame f;p->stage=4;e=i9100_bcm_receive_owned(bus(p),p,p->frame,bytes,&f);
  if(e&&e!=I9100_BCM_FIRMWARE){e=fail(p,I9100_PUMP_PROTOCOL);goto done;}
  increment(&p->rx_frames);e=i9100_wlan_receive(p->control,&f);
  if(f.channel==0U)increment(&p->control_frames);else if(f.channel==1U)increment(&p->event_frames);
  if(e==1){p->stage=5;e=p->io.ethernet(p->io.cookie,f.payload,f.bytes,f.priority);
   if(e<0){e=fail(p,e);goto done;}if(e)increment(&p->dropped_frames);else increment(&p->data_frames);
  }else if(e){
   if((f.channel==2U&&e==I9100_WLAN_NOT_LINKED)||(f.channel==1U&&e==I9100_WLAN_UNSUPPORTED))increment(&p->dropped_frames);
   else{e=fail(p,e);goto done;}
  }
  e=0;wipe(p->frame,sizeof(p->frame));
 }
 if(p->pending_rx)increment(&p->budget_yields);
 p->stage=6;e=i9100_wlan_service(p->control);if(e==I9100_WLAN_BUSY)e=0;else if(e)e=fail(p,e);
done:
 wipe(p->frame,sizeof(p->frame));drop(p);return e;
}
int i9100_bcm_pump_abort(struct i9100_bcm_pump *p)
{int e=take(p);if(e)return e;e=fail(p,I9100_PUMP_CANCELLED);drop(p);return e;}
int i9100_bcm_pump_recheck(struct i9100_bcm_pump *p,unsigned *work)
{
 if(!work)return I9100_PUMP_ARGUMENT;*work=0;int e=take(p);if(e)return e;
 if(p->closing||p->fault){e=p->fault?p->error:I9100_PUMP_DISABLED;drop(p);return e;}
 struct i9100_bcm4330 *b=bus(p);if(b->io.claim(b->io.cookie)){drop(p);return I9100_PUMP_BUSY;}
 uint32_t status=0;e=live(p)?rd(p,STATUS,&status):I9100_PUMP_IO;
 if(!e)*work=p->pending_rx||!!(status&(OWN_EVENTS|FATAL_STATUS));
 b->io.release(b->io.cookie);drop(p);return e;
}
int i9100_bcm_pump_close(struct i9100_bcm_pump *p)
{int e=take(p);if(e)return e;e=close_locked(p);p->cleanup_error=e;drop(p);return e;}
