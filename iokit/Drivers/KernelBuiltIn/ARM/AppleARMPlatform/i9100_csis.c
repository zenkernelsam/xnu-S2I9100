/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_csis.h"
#include <string.h>
enum {CTRL,DPHY,CONFIG,RESOLUTION,MASK};
#define CSIS_ENABLE 1U
#define CSIS_RESET (1U<<4)
#define CSIS_SHADOW (1U<<16)
#define CSIS_ERRORS 0x0000f03fU
#define CSIS_PACKETS 0xf0000000U
static uint32_t rd(struct i9100_csis *c,unsigned off){return c->io.read32(c->io.cookie,off);}
static void wr(struct i9100_csis *c,unsigned off,uint32_t v)
{c->io.write32(c->io.cookie,off,v);c->io.barrier(c->io.cookie);}
static int valid(struct i9100_csis *c)
{return !c||!c->initialized?I9100_CE_ARGUMENT:c->self!=(uintptr_t)c?I9100_CE_OWNER:0;}
static int note(struct i9100_csis *c,int e){if(e&&!c->first_error)c->first_error=e;return e;}
static int command(struct i9100_csis *c,uint32_t bit)
{
 uint32_t v=rd(c,0);if(v==UINT32_MAX)return I9100_CE_IO;
 if(v&(CSIS_RESET|CSIS_SHADOW))return I9100_CE_BUSY;
 if((v&c->regs[CTRL].mask)!=c->regs[CTRL].last)return I9100_CE_OWNER;
 wr(c,0,v|bit);return 0;
}
int i9100_csis_idle(void *cookie)
{
 struct i9100_csis *c=cookie;if(valid(c))return -1;
 uint32_t ctrl=rd(c,0),phy=rd(c,4);
 return ctrl==UINT32_MAX||phy==UINT32_MAX||(ctrl&(CSIS_ENABLE|CSIS_RESET|CSIS_SHADOW))||(phy&31U)?-1:0;
}
static int stop_locked(struct i9100_csis *c)
{
 if(!c->owned)return 0;
 if(rd(c,0)&(CSIS_RESET|CSIS_SHADOW))return I9100_CE_BUSY;
 int e=i9100_camera_reg_set(&c->io,&c->regs[CTRL],c->regs[CTRL].last&~CSIS_ENABLE);
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[DPHY],c->regs[DPHY].last&~31U);
 if(!e&&i9100_csis_idle(c))e=I9100_CE_BUSY;
 if(!e)c->running=0;
 c->cleanup_error=e;return e;
}
int i9100_csis_init(struct i9100_csis *c,const struct i9100_camera_engine_io *io)
{
 if(!c||c->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||!io->barrier||!io->now_ns||!io->delay_us)
  return I9100_CE_ARGUMENT;
 memset(c,0,sizeof(*c));c->io=*io;c->self=(uintptr_t)c;c->initialized=1;
 const unsigned offsets[5]={0,4,8,0x2c,0x10};
 const uint32_t masks[5]={(1U<<31)|(1U<<20)|(1U<<8)|1U,0xf800001fU,0xffU,UINT32_MAX,CSIS_ERRORS|CSIS_PACKETS};
 for(unsigned n=0;n<5;n++){c->regs[n].offset=offsets[n];c->regs[n].mask=masks[n];}return 0;
}
int i9100_csis_enable(struct i9100_csis *c,int enabled,int verified)
{
 int e=valid(c);if(e)return e;if(enabled!=0&&enabled!=1)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(enabled){
  if(verified!=1){e=I9100_CE_ARGUMENT;goto out;}
  if(c->owned||c->enabled||c->generation==UINT32_MAX){e=I9100_CE_BUSY;goto out;}
  uint32_t ctrl=rd(c,0),phy=rd(c,4),mask=rd(c,0x10),pending=rd(c,0x14);
  if(ctrl==UINT32_MAX||phy==UINT32_MAX||mask==UINT32_MAX||pending==UINT32_MAX){e=I9100_CE_IO;goto out;}
  if(i9100_csis_idle(c)||(mask&c->regs[MASK].mask)||(pending&c->regs[MASK].mask)){e=I9100_CE_BUSY;goto out;}
  for(unsigned n=0;n<5&&!e;n++)e=i9100_camera_reg_save(&c->io,&c->regs[n]);
  if(!e){c->generation++;c->owned=c->enabled=1;c->first_error=c->cleanup_error=0;c->polls=0;}
 }else{
  e=stop_locked(c);
  /* Restore local receiver fields before its interrupt mask. No auto reset. */
  const unsigned order[5]={CONFIG,RESOLUTION,DPHY,CTRL,MASK};
  for(unsigned n=0;n<5&&!e;n++)e=i9100_camera_reg_restore(&c->io,&c->regs[order[n]]);
  if(!e)c->owned=c->enabled=0;else c->cleanup_error=e;
 }
out:c->io.unlock(c->io.cookie);return e;
}
static int reset_locked(struct i9100_csis *c)
{
 int e=command(c,CSIS_RESET);if(e)return e;uint32_t v;
 uint64_t start=c->io.now_ns(c->io.cookie);
 for(unsigned n=0;n<100;n++){
  v=rd(c,0);if(v==UINT32_MAX)return I9100_CE_IO;
  if(!(v&CSIS_RESET)){
   /* Exclusive reset can change our writable fields. Preserve the original
    * snapshot, and track the actual reset result rather than assumed zeros. */
   for(unsigned r=0;r<5;r++){
    v=rd(c,c->regs[r].offset);if(v==UINT32_MAX)return I9100_CE_IO;
    c->regs[r].last=v&c->regs[r].mask;c->regs[r].uncertain=0;c->regs[r].dirty=1;
   }return 0;
  }
  uint64_t t=c->io.now_ns(c->io.cookie);if(t<start||t-start>=1000000ULL)break;
  if(c->io.delay_us(c->io.cookie,10))return I9100_CE_IO;
 }return I9100_CE_TIMEOUT;
}
int i9100_csis_start(struct i9100_csis *c,const struct i9100_csis_config *cfg)
{
 int e=valid(c);if(e)return e;
 if(!cfg||cfg->width<16U||cfg->width>1920U||(cfg->width&1U)||cfg->height<16U||cfg->height>1080U||
    (cfg->height&1U)||cfg->lanes<1U||cfg->lanes>2U||cfg->hs_settle>31U||
    (cfg->alignment!=24U&&cfg->alignment!=32U)||cfg->external_wclk>1U||cfg->data_type!=0x1eU)return I9100_CE_ARGUMENT;
 if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(!c->enabled){e=I9100_CE_DISABLED;goto out;}if(c->first_error||c->running){e=I9100_CE_BUSY;goto out;}
 if(i9100_csis_idle(c)){e=I9100_CE_BUSY;goto out;}
 e=reset_locked(c);if(e)goto fail;
 e=i9100_camera_reg_set(&c->io,&c->regs[MASK],0); /* Polled; no uninstalled GIC handler. */
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[CONFIG],(cfg->data_type<<2)|(cfg->lanes-1U));
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[RESOLUTION],(cfg->width<<16)|cfg->height);
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[DPHY],cfg->hs_settle<<27);
 uint32_t ctrl=(c->regs[CTRL].last&(1U<<31))|(cfg->alignment==32U?(1U<<20):0U)|(cfg->external_wclk<<8);
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[CTRL],ctrl);
 if(e)goto fail;
 e=command(c,CSIS_SHADOW);if(e)goto fail;
 for(unsigned n=0;n<100;n++){
  uint32_t v=rd(c,0);if(v==UINT32_MAX){e=I9100_CE_IO;break;}if(!(v&CSIS_SHADOW))break;
  if(n==99U){e=I9100_CE_TIMEOUT;break;}if(c->io.delay_us(c->io.cookie,10)){e=I9100_CE_IO;break;}
 }
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[CTRL],ctrl|CSIS_ENABLE);
 if(!e)e=i9100_camera_reg_set(&c->io,&c->regs[DPHY],(cfg->hs_settle<<27)|31U);
 if(e)goto fail;
 c->config=*cfg;c->running=1;c->raw_status=0;c->polls=0;goto out;
fail:note(c,e);c->cleanup_error=stop_locked(c);
out:c->io.unlock(c->io.cookie);return e;
}
int i9100_csis_poll(struct i9100_csis *c)
{
 int e=valid(c);if(e)return e;if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 if(!c->enabled){e=I9100_CE_DISABLED;goto out;}if(c->first_error){e=c->first_error;goto out;}
 uint32_t v=rd(c,0x14);c->raw_status=v;if(c->polls!=UINT32_MAX)c->polls++;
 if(v==UINT32_MAX){e=I9100_CE_IO;goto fail;}
 uint32_t observed=v&(CSIS_ERRORS|CSIS_PACKETS);
 if(observed)wr(c,0x14,observed); /* W1C only observed owned sources. */
 if(v&CSIS_PACKETS){if(c->non_image_packets!=UINT32_MAX)c->non_image_packets++;}
 if(v&CSIS_ERRORS){c->errors|=v&CSIS_ERRORS;e=I9100_CE_FRAME;goto fail;}goto out;
fail:note(c,e);c->cleanup_error=stop_locked(c);
out:c->io.unlock(c->io.cookie);return e;
}
int i9100_csis_stop(struct i9100_csis *c)
{
 int e=valid(c);if(e)return e;if(c->io.lock(c->io.cookie))return I9100_CE_BUSY;
 e=stop_locked(c);if(e)note(c,e);c->io.unlock(c->io.cookie);return e;
}
