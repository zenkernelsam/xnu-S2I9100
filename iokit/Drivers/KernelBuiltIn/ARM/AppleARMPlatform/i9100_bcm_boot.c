/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_boot.h"
#include "i9100_bcm_backplane_private.h"
#include "i9100_bcm_core_download_private.h"
#include "i9100_bcm_firmware_private.h"
#include "i9100_bcm_f2_private.h"
#include <string.h>
/* bcmsdpcm.h allocates SW1 to FC_CHANGE and SW3 to HOST_INT. */
enum {INTSTATUS=0x20,TOSBMAILBOX=0x40,TOSBVERSION=0x48,TOHOSTDATA=0x4c,HMB_MASK=0xf0,HMB_EVENT=0x80};
static int overlap(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a>UINTPTR_MAX-an||b>UINTPTR_MAX-bn||(a+an>b&&b+bn>a);}
static struct i9100_bcm4330 *bus(struct i9100_bcm_boot *t){return t->firmware->chip->bus;}
static int take(struct i9100_bcm_boot *t,int cleanup)
{
 if(!t||!t->initialized||!t->enabled||t->self!=(uintptr_t)t||!t->firmware||!t->firmware->initialized||
    t->firmware->owner!=(uintptr_t)t->firmware||!t->packets||!t->packets->initialized)return I9100_BCM_BOOT_ARGUMENT;
 struct i9100_bcm4330 *b=bus(t);
 if(!b||b->boot_owner!=t||t->firmware->boot_holds!=1U)return I9100_BCM_BOOT_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BOOT_BUSY;
 int e=!b->enabled?I9100_BCM_BOOT_DISABLED:!cleanup&&(t->fault||b->fault||t->packets->fault)?I9100_BCM_BOOT_IO:0;
 if(e)b->io.release(b->io.cookie);return e;
}
static void drop(struct i9100_bcm_boot *t){struct i9100_bcm4330 *b=bus(t);b->io.release(b->io.cookie);}
static int read32(struct i9100_bcm_boot *t,unsigned off,uint32_t *v)
{return i9100_bcm_bp_read_locked(bus(t),t->sdio_base+off,v)?I9100_BCM_BOOT_IO:0;}
static int write32(struct i9100_bcm_boot *t,unsigned off,uint32_t v)
{return i9100_bcm_bp_write_locked(bus(t),t->sdio_base+off,v)?I9100_BCM_BOOT_IO:0;}
static int cfg(struct i9100_bcm_boot *t,int write,unsigned fn,uint32_t addr,uint8_t *v)
{return bus(t)->io.cmd52(bus(t)->io.cookie,write,fn,addr,v)?I9100_BCM_BOOT_IO:0;}
static int checked_cfg(struct i9100_bcm_boot *t,unsigned fn,uint32_t addr,uint8_t value,uint8_t mask)
{
 uint8_t actual=0;int e=cfg(t,1,fn,addr,&value);if(!e)e=cfg(t,0,fn,addr,&actual);
 if(!e&&(actual&mask)!=(value&mask))e=I9100_BCM_BOOT_IO;return e;
}
static int version(struct i9100_bcm_boot *t,uint32_t wanted)
{
 uint32_t v=0;int e=read32(t,TOSBVERSION,&v);
 if(!e)e=write32(t,TOSBVERSION,(v&~0xff0000U)|(wanted&0xff0000U));
 if(!e)e=read32(t,TOSBVERSION,&v);
 if(!e&&(v&0xff0000U)!=(wanted&0xff0000U))e=I9100_BCM_BOOT_IO;return e;
}
static int pause(struct i9100_bcm_boot *t,uint32_t us)
{return bus(t)->io.delay_us(bus(t)->io.cookie,us)?I9100_BCM_BOOT_IO:0;}
static int timed_out(struct i9100_bcm_boot *t,uint64_t span)
{uint64_t now=bus(t)->io.now_ns(bus(t)->io.cookie);return now<t->started||now-t->started>=span;}
static int wait_ht(struct i9100_bcm_boot *t)
{
 t->started=bus(t)->io.now_ns(bus(t)->io.cookie);
 for(unsigned n=0;n<1000U;n++){
  uint8_t clock=0;int e=cfg(t,0,1,0x1000eU,&clock);if(e)return e;
  /* Both bits are required; the availability bit roles swap on rev8. */
  if((clock&0xc0U)==0xc0U)return 0;
  if(timed_out(t,100000000ULL))break;if((e=pause(t,100U)))return e;
 }
 return I9100_BCM_BOOT_TIMEOUT;
}
int i9100_bcm_boot_init(struct i9100_bcm_boot *t,int enabled,struct i9100_bcm_firmware *fw,struct i9100_bcm_f2 *f2,uint32_t generation)
{
 if(!t||t->initialized||(enabled!=0&&enabled!=1))return I9100_BCM_BOOT_ARGUMENT;
 if(!enabled)return I9100_BCM_BOOT_DISABLED;
 if(!fw||!fw->initialized||fw->owner!=(uintptr_t)fw||!fw->chip||!fw->chip->initialized||
    fw->fault||fw->chip->fault||fw->execution_possible||fw->chip->topology.count>I9100_BCM_MAX_CORES||
    !fw->loaded||!fw->token_verified||!fw->chip->core_download_ready||!fw->chip->ram_ready||!fw->chip->ram_image_owned||
    !f2||!f2->initialized||!f2->card||!f2->card->active||f2->card->fault||f2->enabled||f2->fault||
    !generation||generation<=f2->generation||overlap((uintptr_t)t,sizeof(*t),(uintptr_t)fw,sizeof(*fw))||
    overlap((uintptr_t)t,sizeof(*t),(uintptr_t)f2,sizeof(*f2)))return I9100_BCM_BOOT_ARGUMENT;
 struct i9100_bcm4330 *b=fw->chip->bus;
 if(!b||!b->initialized||b->io.cookie!=f2||f2->card->block_size[1]!=64U||f2->card->functions<2U)return I9100_BCM_BOOT_ARGUMENT;
 if(overlap((uintptr_t)t,sizeof(*t),(uintptr_t)b,sizeof(*b))||
    overlap((uintptr_t)t,sizeof(*t),(uintptr_t)fw->chip,sizeof(*fw->chip))||
    overlap((uintptr_t)t,sizeof(*t),(uintptr_t)f2->card,sizeof(*f2->card)))return I9100_BCM_BOOT_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BOOT_BUSY;
 int e=0;uint32_t base=0;
 if(fw->boot_holds||b->boot_owner||b->control_owner||b->protocol_ready||b->pending||b->fault||generation<=b->protocol_generation)e=I9100_BCM_BOOT_BUSY;
 for(unsigned n=0;n<fw->chip->topology.count&&!e;n++)if(fw->chip->topology.cores[n].id==0x829U){
  if(base)e=I9100_BCM_BOOT_ARGUMENT;else base=fw->chip->topology.cores[n].base;
 }
 if(!base&&!e)e=I9100_BCM_BOOT_ARGUMENT;
 if(!e){memset(t,0,sizeof(*t));t->firmware=fw;t->packets=f2;t->self=(uintptr_t)t;t->generation=generation;t->sdio_base=base;
  t->initialized=t->enabled=1;fw->boot_holds=1;b->boot_owner=t;}
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_boot_start(struct i9100_bcm_boot *t)
{
 int e=take(t,0);if(e)return e;
 if(t->attempted||t->ready||t->owned){drop(t);return I9100_BCM_BOOT_BUSY;}
 t->attempted=1;t->stage=1;
 e=cfg(t,0,1,0x1000eU,&t->saved_clock);
 if(!e)e=cfg(t,0,0,2U,&t->saved_enable);
 if(!e)e=cfg(t,0,0,0x210U,&t->saved_fbr[0]);if(!e)e=cfg(t,0,0,0x211U,&t->saved_fbr[1]);
 if(!e)e=read32(t,TOSBVERSION,&t->saved_version);
 if(!e&&((t->saved_enable&6U)!=2U))e=I9100_BCM_BOOT_NOT_READY;
 uint32_t identity=0;if(!e&&i9100_bcm_bp_read_locked(bus(t),0x18000000U,&identity))e=I9100_BCM_BOOT_IO;
 if(!e&&identity!=t->firmware->chip->chip_id)e=I9100_BCM_BOOT_NOT_READY;
 uint8_t token[4]={0};
 if(!e&&i9100_bcm_bp_memory_locked(bus(t),0,t->firmware->layout.token_address,token,4))e=I9100_BCM_BOOT_IO;
 uint32_t token_value=token[0]|((uint32_t)token[1]<<8)|((uint32_t)token[2]<<16)|((uint32_t)token[3]<<24);
 if(!e&&token_value!=t->firmware->layout.token)e=I9100_BCM_BOOT_NOT_READY;
 if(e){t->error=e;t->fault=1;drop(t);return e;}
 t->owned=t->clock_owned=1;t->stage=2;
 uint8_t request=(uint8_t)(((t->saved_clock&0x3fU)&~5U)|0x12U);
 e=checked_cfg(t,1,0x1000eU,request,0x3fU);if(!e)e=wait_ht(t);
 if(!e){t->stage=3;t->version_owned=1;e=version(t,4U<<16);}
 if(!e){t->fbr_owned=1;t->saved_block_size=t->packets->card->block_size[2];e=checked_cfg(t,0,0x210U,0,0xffU);}
 if(!e)e=checked_cfg(t,0,0x211U,2,0xffU);if(!e)t->packets->card->block_size[2]=512U;
 if(!e){t->stage=4;e=write32(t,INTSTATUS,HMB_MASK);}
 if(!e)e=read32(t,INTSTATUS,&t->interrupt_status);
 if(!e&&(t->interrupt_status&HMB_MASK))e=I9100_BCM_BOOT_NOT_READY;
 if(!e){t->stage=5;t->running=1;t->firmware->execution_possible=1;
  e=i9100_bcm_core_start_cm3_locked(t->firmware->chip);if(e)e=I9100_BCM_BOOT_IO;}
 if(!e){t->stage=6;t->io_enable_owned=1;e=checked_cfg(t,0,2U,t->saved_enable|4U,0xffU);}
 if(!e){
  bus(t)->flow_mask=0;
  t->started=bus(t)->io.now_ns(bus(t)->io.cookie);e=I9100_BCM_BOOT_TIMEOUT;
  for(unsigned n=0;n<2000U;n++){
   uint8_t ready=0;int r=cfg(t,0,0,3U,&ready);if(r){e=r;break;}
   r=read32(t,INTSTATUS,&t->interrupt_status);if(r){e=r;break;}
   if((ready&6U)==6U&&(t->interrupt_status&HMB_EVENT)){
    r=read32(t,TOHOSTDATA,&t->mailbox);if(r){e=r;break;}
    if(t->mailbox&0x10U){e=I9100_BCM_BOOT_HALTED;break;}
    if(t->mailbox&~0xffff001fU){e=I9100_BCM_BOOT_PROTOCOL;break;}
    /* Clear only the observed misc indication BEFORE mailbox ACK; ACK may
     * immediately allow a new firmware notification to be posted. */
    r=write32(t,INTSTATUS,HMB_EVENT);if(!r)r=write32(t,TOSBMAILBOX,2U);if(r){e=r;break;}
    if(t->mailbox&4U)bus(t)->flow_mask=(uint8_t)(t->mailbox>>24);
    if(t->mailbox&0x0aU){
     if(((t->mailbox>>16)&255U)!=4U){e=I9100_BCM_BOOT_PROTOCOL;break;}
     e=0;break;
    }
   }
   if(timed_out(t,2000000000ULL))break;if((r=pause(t,1000U))){e=r;break;}
  }
 }
 if(!e){t->stage=7;e=i9100_bcm_f2_enable_locked(t->packets,1,t->generation);if(e)e=I9100_BCM_BOOT_IO;}
 if(!e){
  struct i9100_bcm4330 *b=bus(t);b->protocol_generation=t->generation;b->protocol_ready=1;b->data_paused=!!(t->interrupt_status&0x30U);b->rx_sequence=b->rx_seen=0;b->tx_sequence=255U;b->tx_max=0;
  t->ready=1;t->stage=8;
 }else t->fault=1;
 t->error=e;drop(t);return e;
}
int i9100_bcm_boot_stop(struct i9100_bcm_boot *t)
{
 int e=take(t,1);if(e)return e;struct i9100_bcm4330 *b=bus(t);
 if(b->control_owner||b->rx_owner||t->packets->rx_owner){drop(t);return I9100_BCM_BOOT_BUSY;}
 t->ready=0;b->protocol_ready=0;t->packets->enabled=0;t->packets->cleanup_f1=1;
 e=i9100_bcm_bp_restore_locked(b)?I9100_BCM_BOOT_IO:0;
 if(!e&&t->owned){int r=i9100_bcm_core_quiesce_locked(t->firmware->chip);if(r)e=I9100_BCM_BOOT_IO;
  else{t->running=0;t->firmware->execution_possible=0;}}
 if(!e&&t->io_enable_owned){e=checked_cfg(t,0,2U,t->saved_enable,0xffU);if(!e)t->io_enable_owned=0;}
 if(!e&&t->fbr_owned){e=checked_cfg(t,0,0x210U,t->saved_fbr[0],0xffU);if(!e)e=checked_cfg(t,0,0x211U,t->saved_fbr[1],0xffU);
  if(!e){t->packets->card->block_size[2]=(uint16_t)t->saved_block_size;t->fbr_owned=0;}}
 if(!e&&t->version_owned){e=version(t,t->saved_version);if(!e)t->version_owned=0;}
 if(!e&&t->owned){int r=i9100_bcm_fw_abort_locked(t->firmware);if(r)e=I9100_BCM_BOOT_IO;}
 if(!e&&t->clock_owned){e=checked_cfg(t,1,0x1000eU,t->saved_clock&0x3fU,0x3fU);if(!e)t->clock_owned=0;}
 if(!e){t->owned=0;t->stopped=1;b->pending=0;b->firmware_status=0;}
 t->packets->cleanup_f1=0;t->cleanup_error=e;if(e)t->fault=1;drop(t);return e;
}
int i9100_bcm_boot_close(struct i9100_bcm_boot *t)
{
 int e=take(t,1);if(e)return e;
 if(t->owned||t->running||t->ready||t->clock_owned||t->fbr_owned||t->io_enable_owned||t->version_owned||
    bus(t)->window_restore_pending||t->firmware->execution_possible){drop(t);return I9100_BCM_BOOT_BUSY;}
 bus(t)->boot_owner=0;t->firmware->boot_holds=0;drop(t);t->enabled=t->initialized=0;t->self=0;return 0;
}
