/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_chip.h"
#include "i9100_bcm_backplane_private.h"
#include "i9100_bcm_chip_private.h"
#include <string.h>
#define ENUM_BASE 0x18000000U
#define ENUM_END 0x18200000U
#define CORE_CC 0x800U
#define CORE_RAM 0x80eU
#define CORE_SDIO 0x829U
#define CORE_CM3 0x82aU
struct reader {const void *cookie;int (*word)(const void *,uint32_t,uint32_t *);uint32_t count,used;};
struct component {
 struct i9100_bcm_core core;
 uint32_t nmp,nsp,nmw,nsw,masters,mw,sw,sports,bridge,addresses;
};
static int next(struct reader *r,uint32_t *v)
{if(r->used>=r->count)return I9100_BCM_CHIP_LAYOUT;int e=r->word(r->cookie,r->used,v);if(!e)r->used++;return e;}
static int aperture(uint32_t base,uint32_t bytes)
{return base>=ENUM_BASE&&bytes&&!(base&0xfffU)&&(uint64_t)base+bytes<=ENUM_END;}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return (uint64_t)a+an>b&&(uint64_t)b+bn>a;}
static const struct i9100_bcm_core *find(const struct i9100_bcm_topology *t,uint16_t id)
{for(unsigned n=0;n<t->count;n++)if(t->cores[n].id==id)return &t->cores[n];return 0;}
static int critical(uint16_t id){return id==CORE_CC||id==CORE_RAM||id==CORE_SDIO||id==CORE_CM3;}
static int finish_component(struct i9100_bcm_topology *t,const struct component *c)
{
 if(c->bridge||!c->nsp||!(c->nmw+c->nsw))return 0;
 if(!c->core.base||!c->core.wrapper||c->masters!=c->nmp||c->mw!=c->nmw||c->sw!=c->nsw||
    c->sports!=((1U<<c->nsp)-1U))return I9100_BCM_CHIP_LAYOUT;
 if(!aperture(c->core.base,c->core.bytes)||!aperture(c->core.wrapper,4096U)||
    overlap(c->core.base,c->core.bytes,c->core.wrapper,4096U))return I9100_BCM_CHIP_LAYOUT;
 if(t->count==I9100_BCM_MAX_CORES)return I9100_BCM_CHIP_UNSUPPORTED;
 for(unsigned n=0;n<t->count;n++){
  const struct i9100_bcm_core *old=&t->cores[n];
  if((critical(c->core.id)&&old->id==c->core.id)||
     overlap(old->base,old->bytes,c->core.base,c->core.bytes)||
     overlap(old->wrapper,4096U,c->core.wrapper,4096U)||
     overlap(old->base,old->bytes,c->core.wrapper,4096U)||
     overlap(old->wrapper,4096U,c->core.base,c->core.bytes))return I9100_BCM_CHIP_LAYOUT;
 }
 t->cores[t->count++]=c->core;return 0;
}
static int parse(struct reader *r,struct i9100_bcm_topology *out)
{
 struct i9100_bcm_topology t;struct component c;memset(&t,0,sizeof(t));memset(&c,0,sizeof(c));
 unsigned active=0;int e;uint32_t v;
 while(!(e=next(r,&v))){
  if(!(v&1U))continue;
  unsigned type=v&15U;
  if((type&~8U)==5U)type=5U;
  if(type==15U){
   if(v!=15U)return I9100_BCM_CHIP_LAYOUT;
   if(active&&(e=finish_component(&t,&c)))return e;
   const struct i9100_bcm_core *cc=find(&t,CORE_CC);
   if(!cc||cc->base!=ENUM_BASE||!find(&t,CORE_RAM)||!find(&t,CORE_CM3)||!find(&t,CORE_SDIO))
    return I9100_BCM_CHIP_LAYOUT;
   t.erom_words=r->used;*out=t;return 0;
  }
  if(type==1U){
   if(active&&(e=finish_component(&t,&c)))return e;
   memset(&c,0,sizeof(c));c.core.id=(uint16_t)((v>>8)&0xfffU);c.core.designer=(uint16_t)(v>>20);
   uint32_t cib;if((e=next(r,&cib)))return e;
   if((cib&15U)!=1U)return I9100_BCM_CHIP_LAYOUT;
   c.core.revision=(uint8_t)(cib>>24);c.nmp=(cib>>4)&31U;c.nsp=(cib>>9)&31U;
   c.nmw=(cib>>14)&31U;c.nsw=(cib>>19)&31U;
   if(c.nsp>16U||c.nmp>16U||c.nmw>16U||c.nsw>16U)return I9100_BCM_CHIP_UNSUPPORTED;
   active=1;continue;
  }
  if(!active)return I9100_BCM_CHIP_LAYOUT;
  if(type==3U){
   if(c.addresses||c.masters>=c.nmp||((v>>4)&15U)!=c.masters)return I9100_BCM_CHIP_LAYOUT;
   c.masters++;continue;
  }
  if(type!=5U)return I9100_BCM_CHIP_LAYOUT;
  uint32_t high=0,size_high=0,bytes=4096U<<((v>>4)&3U);
  if((v&8U)&&(e=next(r,&high)))return e;
  if((v&0x30U)==0x30U){
   uint32_t sd;if((e=next(r,&sd)))return e;bytes=sd&0xfffff000U;
   if((sd&8U)&&(e=next(r,&size_high)))return e;
  }
  if(c.masters!=c.nmp)return I9100_BCM_CHIP_LAYOUT;
  c.addresses++;
  unsigned port=(v>>8)&15U,kind=(v>>6)&3U;uint32_t base=v&0xfffff000U;
  if(kind<2U){
   if(port>=c.nsp)return I9100_BCM_CHIP_LAYOUT;c.sports|=1U<<port;
   if(kind==1U&&!port)c.bridge=1;
   if(!port&&kind==0U&&!c.core.base){
    if(high||size_high||(bytes!=4096U&&bytes!=8192U))return I9100_BCM_CHIP_UNSUPPORTED;
    c.core.base=base;c.core.bytes=bytes;
   }
  }else{
   unsigned first=c.nsp==1U?0U:1U;
   if((kind==3U&&(port!=c.mw||c.mw>=c.nmw))||
      (kind==2U&&(port!=first+c.sw||c.sw>=c.nsw)))return I9100_BCM_CHIP_LAYOUT;
   if(high||size_high||bytes!=4096U)return I9100_BCM_CHIP_UNSUPPORTED;
   if(!c.core.wrapper&&((kind==3U&&c.nmw)||(kind==2U&&!c.nmw)))c.core.wrapper=base;
   if(kind==3U)c.mw++;else c.sw++;
  }
 }
 return e;
}
static int array_word(const void *cookie,uint32_t index,uint32_t *v)
{*v=((const uint32_t *)cookie)[index];return 0;}
int i9100_bcm_erom_decode(const uint32_t *words,uint32_t count,struct i9100_bcm_topology *out)
{
 if(!words||!out||!count||count>I9100_BCM_EROM_WORDS)return I9100_BCM_CHIP_ARGUMENT;
 uintptr_t a=(uintptr_t)words,b=(uintptr_t)out;
 if((a&3U)||(b&3U)||a>UINTPTR_MAX-(uintptr_t)count*4U||b>UINTPTR_MAX-sizeof(*out)||
    (a+(uintptr_t)count*4U>b&&b+sizeof(*out)>a))return I9100_BCM_CHIP_ARGUMENT;
 struct reader r={words,array_word,count,0};return parse(&r,out);
}
int i9100_bcm_chip_init(struct i9100_bcm_chip *c,struct i9100_bcm4330 *b)
{
 if(!c||c->initialized||!b||!b->initialized)return I9100_BCM_CHIP_ARGUMENT;
 uintptr_t cp=(uintptr_t)c,bp=(uintptr_t)b;
 if(cp>UINTPTR_MAX-sizeof(*c)||bp>UINTPTR_MAX-sizeof(*b)||
    (cp+sizeof(*c)>bp&&bp+sizeof(*b)>cp))return I9100_BCM_CHIP_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_CHIP_BUSY;
 if(b->chip_holds||b->protocol_ready){b->io.release(b->io.cookie);return I9100_BCM_CHIP_BUSY;}
 memset(c,0,sizeof(*c));c->bus=b;c->owner=(uintptr_t)c;c->initialized=1;b->chip_holds=1;
 b->io.release(b->io.cookie);return 0;
}
static int claim(struct i9100_bcm_chip *c,int cleanup)
{
 if(!c||!c->initialized||c->owner!=(uintptr_t)c||!c->bus||!c->bus->initialized||c->bus->chip_holds!=1U)
  return I9100_BCM_CHIP_ARGUMENT;
 struct i9100_bcm4330 *b=c->bus;
 if(b->io.claim(b->io.cookie))return I9100_BCM_CHIP_BUSY;
 int e=!b->enabled?I9100_BCM_CHIP_DISABLED:b->protocol_ready?I9100_BCM_CHIP_BUSY:
       !cleanup&&(b->fault||c->fault)?I9100_BCM_CHIP_IO:0;
 if(e)b->io.release(b->io.cookie);return e;
}
static int read32(const struct i9100_bcm_chip *c,uint32_t addr,uint32_t *v)
{return i9100_bcm_bp_read_locked(c->bus,addr,v)?I9100_BCM_CHIP_IO:0;}
static int write32(struct i9100_bcm_chip *c,uint32_t addr,uint32_t v)
{return i9100_bcm_bp_write_locked(c->bus,addr,v)?I9100_BCM_CHIP_IO:0;}
static int bus_word(const void *cookie,uint32_t index,uint32_t *v)
{const struct i9100_bcm_chip *c=cookie;uint64_t now=c->bus->io.now_ns(c->bus->io.cookie);
 if(now<c->started||now-c->started>2000000000ULL)return I9100_BCM_CHIP_TIMEOUT;
 return read32(c,c->erom_base+index*4U,v);}
static int record(struct i9100_bcm_chip *c,int e)
{
 c->error=e;if(e&&e!=I9100_BCM_CHIP_NOT_READY){c->fault=1;if(e==I9100_BCM_CHIP_IO)c->bus->fault=1;}
 c->bus->io.release(c->bus->io.cookie);return e;
}
int i9100_bcm_chip_probe(struct i9100_bcm_chip *c,int enabled,int verified)
{
 if(!c||!c->initialized||(enabled!=0&&enabled!=1)||(verified!=0&&verified!=1))return I9100_BCM_CHIP_ARGUMENT;
 if(!enabled)return I9100_BCM_CHIP_DISABLED;if(!verified)return I9100_BCM_CHIP_ARGUMENT;
 int e=claim(c,0);if(e)return e;
 if(c->probed){c->bus->io.release(c->bus->io.cookie);return I9100_BCM_CHIP_BUSY;}
 c->started=c->bus->io.now_ns(c->bus->io.cookie);c->stage=1;e=read32(c,ENUM_BASE,&c->chip_id);
 if(!e&&((c->chip_id&0xffffU)!=0x4330U||(c->chip_id>>28)!=1U))e=I9100_BCM_CHIP_ID;
 if(!e){c->stage=2;e=read32(c,ENUM_BASE+0xfcU,&c->erom_base);}
 if(!e&&!aperture(c->erom_base,0xe00U))e=I9100_BCM_CHIP_LAYOUT;
 if(!e){struct reader r={c,bus_word,I9100_BCM_EROM_WORDS,0};e=parse(&r,&c->topology);}
 if(!e)c->probed=1;return record(c,e);
}
int i9100_bcm_chip_ram_guard_locked(struct i9100_bcm_chip *c)
{
 const struct i9100_bcm_core *cpu_core=find(&c->topology,CORE_CM3),*ram=find(&c->topology,CORE_RAM);
 uint32_t reset=0,clock=0,rreset=0;int e=read32(c,cpu_core->wrapper+0x800U,&reset);
 if(!e)e=read32(c,ram->wrapper+0x408U,&clock);
 if(!e)e=read32(c,ram->wrapper+0x800U,&rreset);
 if(!e&&(!(reset&1U)||(clock&3U)!=1U||(rreset&1U)))e=I9100_BCM_CHIP_NOT_READY;
 return e;
}
static int restore_selector(struct i9100_bcm_chip *c)
{
 if(!c->ram_selector_owned)return 0;
 const struct i9100_bcm_core *ram=find(&c->topology,CORE_RAM);uint32_t value=0;
 int e=i9100_bcm_bp_restore_locked(c->bus)?I9100_BCM_CHIP_IO:i9100_bcm_chip_ram_guard_locked(c);
 if(!e)e=write32(c,ram->base+0x10U,c->ram_selector_saved);
 if(!e)e=read32(c,ram->base+0x10U,&value);
 if(!e&&value!=c->ram_selector_saved)e=I9100_BCM_CHIP_IO;
 if(!e)c->ram_selector_owned=0;return e;
}
int i9100_bcm_chip_ram_restore(struct i9100_bcm_chip *c)
{
 int e=claim(c,1);if(e)return e;
 e=i9100_bcm_bp_restore_locked(c->bus)?I9100_BCM_CHIP_IO:restore_selector(c);c->cleanup_error=e;
 if(e)c->fault=1;c->bus->io.release(c->bus->io.cookie);return e;
}
int i9100_bcm_chip_close(struct i9100_bcm_chip *c)
{
 if(!c||!c->initialized||c->owner!=(uintptr_t)c||!c->bus||!c->bus->initialized)
  return I9100_BCM_CHIP_ARGUMENT;
 struct i9100_bcm4330 *b=c->bus;if(b->io.claim(b->io.cookie))return I9100_BCM_CHIP_BUSY;
 if(c->ram_selector_owned||c->firmware_holds||c->ram_image_owned||c->core_download_owned||b->window_restore_pending||b->chip_holds!=1U){
  b->io.release(b->io.cookie);return I9100_BCM_CHIP_BUSY;
 }
 b->chip_holds=0;c->initialized=0;c->owner=0;c->bus=0;b->io.release(b->io.cookie);return 0;
}
int i9100_bcm_chip_ram_probe(struct i9100_bcm_chip *c)
{
 int e=claim(c,0);if(e)return e;
 if(!c->probed||c->ram_ready||c->ram_selector_owned||c->firmware_holds||c->ram_image_owned){c->bus->io.release(c->bus->io.cookie);return I9100_BCM_CHIP_BUSY;}
 const struct i9100_bcm_core *ram=find(&c->topology,CORE_RAM);c->stage=3;
 e=i9100_bcm_chip_ram_guard_locked(c);if(e)return record(c,e);
 if(ram->revision<4U)return record(c,I9100_BCM_CHIP_UNSUPPORTED);
 c->stage=4;e=read32(c,ram->base,&c->coreinfo);
 uint32_t banks=(c->coreinfo>>4)&15U;uint64_t size=0,retention=0;
 if(ram->revision<=7U||ram->revision==12U){
  uint32_t last=(c->coreinfo>>20)&15U;
  if(!e&&!banks)e=I9100_BCM_CHIP_LAYOUT;
  if(!e){size=(uint64_t)(banks-(last?1U:0U))*(1ULL<<(14U+(c->coreinfo&15U)));
   if(last)size+=1ULL<<(13U+last);}
 }else{
  if(ram->revision>=23U)banks=(c->coreinfo>>4)&31U;
  if(!e&&!banks)e=I9100_BCM_CHIP_LAYOUT;
  if(!e)e=read32(c,ram->base+0x10U,&c->ram_selector_saved);
  if(!e)c->ram_selector_owned=1;
  for(unsigned n=0;n<banks&&!e;n++){
   uint32_t selected=0;e=write32(c,ram->base+0x10U,n);
   if(!e)e=read32(c,ram->base+0x10U,&selected);
   if(!e&&selected!=n)e=I9100_BCM_CHIP_IO;
   if(!e)e=read32(c,ram->base+0x40U,&c->bankinfo[n]);
   if(!e){uint32_t bytes=((c->bankinfo[n]&0x7fU)+1U)*8192U;
    size+=bytes;if(c->bankinfo[n]&0x10000U)retention+=bytes;}
  }
 }
 c->stage=5;c->cleanup_error=restore_selector(c);if(c->cleanup_error&&!e)e=c->cleanup_error;
 if(!e&&(!size||size>I9100_BCM_MAX_RAM||retention>size))e=I9100_BCM_CHIP_LAYOUT;
 if(!e)e=i9100_bcm_chip_ram_guard_locked(c);
 if(!e){c->ram_size=(uint32_t)size;c->retention_size=(uint32_t)retention;c->bank_count=banks;c->ram_ready=1;}
 return record(c,e);
}
