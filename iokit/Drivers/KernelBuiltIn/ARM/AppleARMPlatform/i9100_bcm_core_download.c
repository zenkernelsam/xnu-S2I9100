/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_core_download.h"
#include "i9100_bcm_backplane_private.h"
#include "i9100_bcm_core_download_private.h"
enum {IOCTRL=0x408,RESETCTRL=0x800,RESETSTATUS=0x804};
struct cores {const struct i9100_bcm_core *cm3,*d11,*ram;};
static int get_cores(struct i9100_bcm_chip *c,struct cores *out)
{
 out->cm3=out->d11=out->ram=0;
 if(!c->probed||c->topology.count>I9100_BCM_MAX_CORES||(c->chip_id&0xffffU)!=0x4330U||(c->chip_id>>28)!=1U)
  return I9100_BCM_CHIP_ID;
 for(unsigned n=0;n<c->topology.count;n++){
  const struct i9100_bcm_core *core=&c->topology.cores[n];
  const struct i9100_bcm_core **slot=core->id==0x82aU?&out->cm3:core->id==0x812U?&out->d11:core->id==0x80eU?&out->ram:0;
  if(slot){if(*slot)return I9100_BCM_CHIP_LAYOUT;*slot=core;}
 }
 return out->cm3&&out->d11&&out->ram&&out->ram->revision>=4U?0:I9100_BCM_CHIP_UNSUPPORTED;
}
static int take(struct i9100_bcm_chip *c,int cleanup)
{
 if(!c||!c->initialized||c->owner!=(uintptr_t)c||!c->bus||!c->bus->initialized||c->bus->chip_holds!=1U)
  return I9100_BCM_CHIP_ARGUMENT;
 struct i9100_bcm4330 *b=c->bus;if(b->io.claim(b->io.cookie))return I9100_BCM_CHIP_BUSY;
 int e=!b->enabled?I9100_BCM_CHIP_DISABLED:b->protocol_ready?I9100_BCM_CHIP_BUSY:
       !cleanup&&(b->fault||c->fault)?I9100_BCM_CHIP_IO:0;
 if(e)b->io.release(b->io.cookie);return e;
}
static int read32(struct i9100_bcm_chip *c,uint32_t addr,uint32_t *v)
{return i9100_bcm_bp_read_locked(c->bus,addr,v)?I9100_BCM_CHIP_IO:0;}
static int rmw(struct i9100_bcm_chip *c,uint32_t addr,uint32_t mask,uint32_t value)
{
 uint32_t before=0,after=0;int e=read32(c,addr,&before);
 if(!e&&i9100_bcm_bp_write_locked(c->bus,addr,(before&~mask)|value))e=I9100_BCM_CHIP_IO;
 if(!e)e=read32(c,addr,&after);
 if(!e&&(after&mask)!=value)e=I9100_BCM_CHIP_IO;return e;
}
static int pause(struct i9100_bcm_chip *c,uint32_t us)
{return c->bus->io.delay_us(c->bus->io.cookie,us)?I9100_BCM_CHIP_IO:0;}
static int request_reset(struct i9100_bcm_chip *c,const struct i9100_bcm_core *core,unsigned asserted)
{
 uint32_t value=0;int e=read32(c,core->wrapper+RESETCTRL,&value);
 if(!e&&i9100_bcm_bp_write_locked(c->bus,core->wrapper+RESETCTRL,(value&~1U)|asserted))e=I9100_BCM_CHIP_IO;
 return e; /* propagation is verified after the settle delay, not at write time */
}
static int wait_bits(struct i9100_bcm_chip *c,uint32_t addr,uint32_t mask,uint32_t value)
{
 uint64_t started=c->bus->io.now_ns(c->bus->io.cookie);
 for(unsigned n=0;n<200U;n++){
  uint32_t actual=0;int e=read32(c,addr,&actual);if(e)return e;
  if((actual&mask)==value)return 0;
  uint64_t now=c->bus->io.now_ns(c->bus->io.cookie);
  if(now<started||now-started>=20000000ULL)break;
  if((e=pause(c,50U)))return e;
 }
 return I9100_BCM_CHIP_TIMEOUT;
}
static int hold_reset(struct i9100_bcm_chip *c,const struct i9100_bcm_core *core,unsigned d11)
{
 uint32_t reset=0;int e=read32(c,core->wrapper+RESETCTRL,&reset);
 uint32_t mask=d11?15U:3U,pre=d11?15U:3U,held=d11?7U:3U;
 if(!e&&!(reset&1U))e=wait_bits(c,core->wrapper+RESETSTATUS,0xffffffffU,0);
 if(!e&&!(reset&1U))e=rmw(c,core->wrapper+IOCTRL,mask,pre);
 if(!e&&!(reset&1U))e=request_reset(c,core,1U);
 if(!e)e=pause(c,10U);
 if(!e)e=wait_bits(c,core->wrapper+RESETCTRL,1U,1U);
 if(!e)e=rmw(c,core->wrapper+IOCTRL,mask,held);
 return e;
}
static int stop_locked(struct i9100_bcm_chip *c,const struct cores *cores)
{
 if(c->firmware_holds||c->ram_image_owned||c->ram_selector_owned)return I9100_BCM_CHIP_BUSY;
 c->core_download_ready=c->ram_ready=0; /* shutdown attempts invalidate old readiness even on failure */
 int e=i9100_bcm_bp_restore_locked(c->bus)?I9100_BCM_CHIP_IO:0;
 if(e)return e;
 int r=hold_reset(c,cores->cm3,0);if(r)e=r;
 r=hold_reset(c,cores->d11,1);if(r)e=r;
 uint32_t cm3=0,d11=0;
 r=read32(c,cores->cm3->wrapper+RESETCTRL,&cm3);if(r)e=r;
 r=read32(c,cores->d11->wrapper+RESETCTRL,&d11);if(r)e=r;
 /* Never remove RAM availability while a writer may still be running. */
 if(!(cm3&1U)||!(d11&1U))return e?e:I9100_BCM_CHIP_NOT_READY;
 r=hold_reset(c,cores->ram,0);if(r)e=r;
 if(!e){c->core_download_owned=c->core_download_ready=0;c->ram_ready=0;}
 return e;
}
int i9100_bcm_core_download_prepare(struct i9100_bcm_chip *c,int verified)
{
 if(verified!=1)return I9100_BCM_CHIP_ARGUMENT;
 int e=take(c,0);if(e)return e;struct cores cores;
 if(c->core_download_owned||c->firmware_holds||c->ram_image_owned||c->ram_selector_owned){
  c->bus->io.release(c->bus->io.cookie);return I9100_BCM_CHIP_BUSY;
 }
 e=get_cores(c,&cores);if(e){c->bus->io.release(c->bus->io.cookie);return e;}
 uint32_t identity=0;e=read32(c,0x18000000U,&identity);
 if(!e&&identity!=c->chip_id)e=I9100_BCM_CHIP_ID;
 const struct i9100_bcm_core *list[3]={cores.cm3,cores.d11,cores.ram};
 for(unsigned n=0;n<3U&&!e;n++){
  e=read32(c,list[n]->wrapper+IOCTRL,&c->core_saved[n][0]);
  if(!e)e=read32(c,list[n]->wrapper+RESETCTRL,&c->core_saved[n][1]);
 }
 if(e){c->core_error=e;if(e==I9100_BCM_CHIP_IO)c->fault=c->bus->fault=1;c->bus->io.release(c->bus->io.cookie);return e;}
 c->core_download_owned=1;c->core_download_ready=c->ram_ready=0;c->core_error=c->core_cleanup_error=0;
 c->core_stage=1;e=hold_reset(c,cores.cm3,0);
 if(!e){c->core_stage=2;e=hold_reset(c,cores.d11,1);}
 if(!e){c->core_stage=3;e=hold_reset(c,cores.ram,0);}
 if(!e){c->core_stage=4;e=request_reset(c,cores.ram,0U);}
 if(!e)e=pause(c,50U);
 if(!e)e=wait_bits(c,cores.ram->wrapper+RESETCTRL,1U,0);
 if(!e)e=rmw(c,cores.ram->wrapper+IOCTRL,3U,1U);
 uint32_t cm3=0,d11=0,ram=0,clock=0;
 if(!e){c->core_stage=5;e=read32(c,cores.cm3->wrapper+RESETCTRL,&cm3);}
 if(!e)e=read32(c,cores.d11->wrapper+RESETCTRL,&d11);
 if(!e)e=read32(c,cores.ram->wrapper+RESETCTRL,&ram);
 if(!e)e=read32(c,cores.ram->wrapper+IOCTRL,&clock);
 if(!e&&(!(cm3&1U)||!(d11&1U)||(ram&1U)||(clock&3U)!=1U))e=I9100_BCM_CHIP_NOT_READY;
 if(!e){c->core_download_ready=1;c->core_stage=6;}
 else{c->core_error=e;c->core_cleanup_error=stop_locked(c,&cores);c->fault=1;if(e==I9100_BCM_CHIP_IO)c->bus->fault=1;}
 c->bus->io.release(c->bus->io.cookie);return e;
}
int i9100_bcm_core_download_stop(struct i9100_bcm_chip *c)
{
 int e=take(c,1);if(e)return e;struct cores cores;e=get_cores(c,&cores);
 if(!e&&!c->core_download_owned)e=I9100_BCM_CHIP_DISABLED;
 if(!e)e=stop_locked(c,&cores);
 c->core_cleanup_error=e;if(e&&e!=I9100_BCM_CHIP_DISABLED&&e!=I9100_BCM_CHIP_BUSY)c->fault=1;
 c->bus->io.release(c->bus->io.cookie);return e;
}
int i9100_bcm_core_start_cm3_locked(struct i9100_bcm_chip *c)
{
 struct cores cores;int e=get_cores(c,&cores);if(e)return e;
 if(!c->core_download_owned||!c->core_download_ready||!c->ram_ready||!c->ram_image_owned||!c->firmware_holds)
  return I9100_BCM_CHIP_NOT_READY;
 uint32_t cm3=0,d11=0,ram=0,clock=0;
 e=read32(c,cores.cm3->wrapper+RESETCTRL,&cm3);
 if(!e)e=read32(c,cores.d11->wrapper+RESETCTRL,&d11);
 if(!e)e=read32(c,cores.ram->wrapper+RESETCTRL,&ram);
 if(!e)e=read32(c,cores.ram->wrapper+IOCTRL,&clock);
 if(!e&&(!(cm3&1U)||!(d11&1U)||(ram&1U)||(clock&3U)!=1U))e=I9100_BCM_CHIP_NOT_READY;
 if(e)return e;
 c->core_download_ready=0;
 e=request_reset(c,cores.cm3,0U);
 if(!e)e=pause(c,50U);if(!e)e=wait_bits(c,cores.cm3->wrapper+RESETCTRL,1U,0);
 if(!e)e=rmw(c,cores.cm3->wrapper+IOCTRL,3U,1U);
 return e;
}
int i9100_bcm_core_quiesce_locked(struct i9100_bcm_chip *c)
{
 struct cores cores;int e=get_cores(c,&cores);if(e)return e;
 if(!c->core_download_owned)return I9100_BCM_CHIP_NOT_READY;
 c->core_download_ready=0;
 e=hold_reset(c,cores.cm3,0);int r=hold_reset(c,cores.d11,1);if(r)e=r;
 uint32_t cm3=0,d11=0,ram=0,clock=0;
 r=read32(c,cores.cm3->wrapper+RESETCTRL,&cm3);if(r)e=r;
 r=read32(c,cores.d11->wrapper+RESETCTRL,&d11);if(r)e=r;
 r=read32(c,cores.ram->wrapper+RESETCTRL,&ram);if(r)e=r;
 r=read32(c,cores.ram->wrapper+IOCTRL,&clock);if(r)e=r;
 if(!e&&(!(cm3&1U)||!(d11&1U)||(ram&1U)||(clock&3U)!=1U))e=I9100_BCM_CHIP_NOT_READY;
 if(!e)c->core_download_ready=1;
 return e;
}
