/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_native.h"
#include "i9100_emmc_clock.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_camera_mmio_read(volatile uint8_t *,unsigned);
void i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
}
enum {EN_HOST,EN_CMU,EN_GPIO,EN_GIC,EN_CHIP,EN_MAPS,EN_BUILDING=1,EN_READY=2,EN_CLOSING=3};
struct i9100_emmc_native {
 i9100_peripheral_native *parent;i9100_native_parent_hold parent_hold;
 i9100_emmc_host_lease lease;i9100_emmc_inherited_profile profile;
 IOLock *lifecycle,*transaction;
 IOMemoryDescriptor *memory[EN_MAPS];IOMemoryMap *mapping[EN_MAPS];volatile uint8_t *regs[EN_MAPS];
 i9100_emmc_card card;i9100_emmc_writer *writer;i9100_emmc_layout layout;
 struct i9100_emmc_native_snapshot snapshot;
 uintptr_t consumers[4];uint32_t phase,mode,probe_attempted,sealed,clock_attempted;int error,cleanup_error;
 uint8_t clock_capture[I9100_EMMC_CLOCK_BYTES];
 i9100_emmc_sdr_session sdr;
 uint32_t pad_owned,pad_restoring,saved_gpk0[3],saved_gpk1[3];
};
static int take(IOLock *l){return l&&ml_get_interrupts_enabled()&&!ml_at_interrupt_context()&&IOLockTryLock(l)?0:-1;}
static int serialize(IOLock *l)
{if(!l||!ml_get_interrupts_enabled()||ml_at_interrupt_context())return -1;IOLockLock(l);return 0;}
static uint32_t rd(i9100_emmc_native *n,unsigned map,unsigned off)
{return map>=EN_MAPS||!n->regs[map]?0xffffffffU:i9100_camera_mmio_read(n->regs[map],off);}
static uint32_t clock_read(void *cookie,unsigned map,unsigned off)
{return rd((i9100_emmc_native *)cookie,map,off);}
static uint64_t now(void *);
static int resources(i9100_emmc_native *n)
{
 if(i9100_emmc_host_check(&n->lease))return I9100_EMMC_OWNER;
 uint32_t chip=rd(n,EN_CHIP,0),gate=rd(n,EN_CMU,0xc940),mask=rd(n,EN_CMU,0xc340),irq=rd(n,EN_GIC,0x10c);
 if(chip==0xffffffffU||(chip&0xfffe0000U)!=(0x43210000U&0xfffe0000U)||
    gate==0xffffffffU||mask==0xffffffffU||irq==0xffffffffU||!(gate&(1U<<9))||!(mask&(1U<<16))||irq&(1U<<13))return I9100_EMMC_OWNER;
 uint32_t mux=rd(n,EN_CMU,0xc240),div=rd(n,EN_CMU,0xc54c),sel=rd(n,EN_HOST,0x9c);
 if(mux==0xffffffffU||div==0xffffffffU||sel==0xffffffffU||(mux&0xf0000U)!=n->profile.source_mux||
    (div&0xff0fU)!=n->profile.source_divider||sel!=n->profile.clksel)return I9100_EMMC_CLOCK;
 struct i9100_emmc_clock_result observed;
 const i9100_emmc_clock_io clock={n,clock_read,0,0};
 if(i9100_emmc_clock_rate(&clock,n->profile.xxti_hz,n->profile.xusbxti_hz,&observed)||
    observed.ciu_hz!=n->profile.ciu_clock_hz)return I9100_EMMC_CLOCK;
 uint32_t con=rd(n,EN_GPIO,0x40),pull=rd(n,EN_GPIO,0x48),drive=rd(n,EN_GPIO,0x4c),dat=rd(n,EN_GPIO,0x44);
 uint32_t expected_pull=n->pad_owned?(n->profile.gpk0_pull&0x3fU):n->profile.gpk0_pull;
 uint32_t expected_drive=n->pad_owned?((n->profile.gpk0_drive&0x3fU)|0x80U):n->profile.gpk0_drive;
 if(con==0xffffffffU||pull==0xffffffffU||drive==0xffffffffU||dat==0xffffffffU||
    (con&0xffffU)!=n->profile.gpk0_mux||(pull&0xffU)!=expected_pull||(drive&0xffU)!=expected_drive||!(dat&4U))return I9100_EMMC_OWNER;
 if(n->pad_owned){
  if((con&0x0ffff000U)!=0x03333000U||(pull&0x3fc0U)||(drive&0x3fc0U)!=0x2a80U)return I9100_EMMC_OWNER;
  if(n->sdr.width==8U&&((rd(n,EN_GPIO,0x60)&0x0ffff000U)!=0x04444000U||
     (rd(n,EN_GPIO,0x68)&0x3fc0U)||(rd(n,EN_GPIO,0x6c)&0x3fc0U)!=0x2a80U))return I9100_EMMC_OWNER;
 }
 return 0;
}
static int actual_idle(i9100_emmc_native *n)
{
 int e=resources(n);if(e)return e;
 uint32_t cmd=rd(n,EN_HOST,EM_CMD),status=rd(n,EN_HOST,EM_STATUS),ctrl=rd(n,EN_HOST,EM_CTRL),bmod=rd(n,EN_HOST,EM_BMOD);
 if(cmd==0xffffffffU||status==0xffffffffU||ctrl==0xffffffffU||bmod==0xffffffffU)return I9100_EMMC_IO;
 if(cmd&(1U<<31)||status&((15U<<4)|(1U<<9)|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31))||ctrl&7U||
    ctrl&((1U<<5)|(1U<<25))||bmod&(1U<<7)||rd(n,EN_HOST,EM_IDINTEN)||rd(n,EN_HOST,EM_INTMASK))return I9100_EMMC_BUSY;
 return 0;
}
static int inherited_idle(i9100_emmc_native *n)
{
 /* Pre-probe failure did not alter the card. A profile-rate mismatch must
  * not itself prevent map cleanup, but an externally busy/gated host must
  * retain its reservation until actual quiescence is observed. */
 uint32_t gate=rd(n,EN_CMU,0xc940),mask=rd(n,EN_CMU,0xc340);
 if(gate==0xffffffffU||mask==0xffffffffU||!(gate&(1U<<9))||!(mask&(1U<<16)))return I9100_EMMC_BUSY;
 uint32_t cmd=rd(n,EN_HOST,EM_CMD),status=rd(n,EN_HOST,EM_STATUS),ctrl=rd(n,EN_HOST,EM_CTRL),bmod=rd(n,EN_HOST,EM_BMOD);
 if(cmd==0xffffffffU||status==0xffffffffU||ctrl==0xffffffffU||bmod==0xffffffffU||cmd&(1U<<31)||
    status&((15U<<4)|(1U<<9)|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31))||ctrl&(7U|(1U<<5)|(1U<<25))||
    bmod&(1U<<7)||rd(n,EN_HOST,EM_IDINTEN)||rd(n,EN_HOST,EM_INTMASK)||rd(n,EN_GIC,0x10c)&(1U<<13))return I9100_EMMC_BUSY;
 return 0;
}
static uint32_t card_read(void *p,unsigned off){return rd((i9100_emmc_native *)p,EN_HOST,off);}
static void card_write(void *p,unsigned off,uint32_t value)
{i9100_emmc_native *n=(i9100_emmc_native *)p;if(n->regs[EN_HOST])i9100_camera_mmio_write(n->regs[EN_HOST],off,value);}
static uint64_t now(void *){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int delay(void *,uint32_t us){if(!ml_get_interrupts_enabled()||ml_at_interrupt_context()||us>1000U)return -1;IODelay(us);return 0;}
static int pad_write(i9100_emmc_native *n,unsigned off,uint32_t mask,uint32_t value)
{
 uint32_t old=rd(n,EN_GPIO,off);if(old==0xffffffffU)return I9100_EMMC_IO;
 i9100_camera_mmio_write(n->regs[EN_GPIO],off,(old&~mask)|(value&mask));
 uint32_t actual=rd(n,EN_GPIO,off);return actual==0xffffffffU||(actual&mask)!=(value&mask)?I9100_EMMC_READBACK:0;
}
static int sdr_board_check(void *p){return resources((i9100_emmc_native *)p);}
static int sdr_prepare_pads(void *p,unsigned width)
{
 auto *n=(i9100_emmc_native *)p;if(n->pad_owned||(width!=4U&&width!=8U))return I9100_EMMC_ARGUMENT;
 int e=resources(n);if(e)return e;
 const unsigned offsets[3]={0,8,12};
 for(unsigned i=0;i<3;i++){
  n->saved_gpk0[i]=rd(n,EN_GPIO,0x40+offsets[i]);n->saved_gpk1[i]=rd(n,EN_GPIO,0x60+offsets[i]);
  if(n->saved_gpk0[i]==0xffffffffU||n->saved_gpk1[i]==0xffffffffU)return I9100_EMMC_IO;
 }
 /* Global peripheral brokers reserve these banks. Only the exact MMC
  * host lease can program data-pad fields, never CMD/CLK/eMMC_EN/DAT. */
 n->pad_owned=1;
 const uint32_t mask[3]={0x0ffff000U,0x3fc0U,0x3fc0U},value[3]={0x03333000U,0,0x2a80U};
 for(unsigned i=0;i<3;i++){e=pad_write(n,0x40+offsets[i],mask[i],value[i]);if(e)return e;}
 if(width==8U)for(unsigned i=0;i<3;i++){e=pad_write(n,0x60+offsets[i],mask[i],i==0?0x04444000U:value[i]);if(e)return e;}
 return resources(n);
}
static int sdr_restore_pads(i9100_emmc_native *n)
{
 if(!n->pad_owned)return 0;
 const unsigned offsets[3]={0,8,12};const uint32_t masks[3]={0x0ffff000U,0x3fc0U,0x3fc0U};
 n->pad_restoring=1;
 for(unsigned i=0;i<3;i++){int e=pad_write(n,0x40+offsets[i],masks[i],n->saved_gpk0[i]);if(e)return e;}
 if(n->sdr.width==8U)for(unsigned i=0;i<3;i++){int e=pad_write(n,0x60+offsets[i],masks[i],n->saved_gpk1[i]);if(e)return e;}
 n->pad_owned=n->pad_restoring=0;return resources(n);
}
static int claim(void *p){return take(((i9100_emmc_native *)p)->transaction);}
static void release_claim(void *p){IOLockUnlock(((i9100_emmc_native *)p)->transaction);}
static int exclusive(void *p,const i9100_emmc_card *c)
{i9100_emmc_native *n=(i9100_emmc_native *)p;return c!=&n->card||resources(n)||i9100_emmc_transport_check(&n->card)?-1:0;}
static int layout_read(void *p,uint64_t lba,uint8_t out[512])
{return i9100_emmc_read(&((i9100_emmc_native *)p)->card,lba,1,out);}
static bool same_identity(const i9100_emmc_writer_identity *a,const i9100_emmc_writer_identity *b)
{
 return a&&b&&a->sectors==b->sectors&&a->primary_crc==b->primary_crc&&a->backup_crc==b->backup_crc&&a->entries_crc==b->entries_crc&&
  !memcmp(a->cid,b->cid,sizeof(a->cid))&&!memcmp(a->disk_guid,b->disk_guid,16)&&
  !memcmp(a->system_guid,b->system_guid,16)&&!memcmp(a->data_guid,b->data_guid,16);
}
static void save(i9100_emmc_native *n)
{
 n->snapshot.mode=n->mode;n->snapshot.phase=n->phase;n->snapshot.host_generation=n->card.generation;
 n->snapshot.command=n->card.last_command;n->snapshot.rint=n->card.last_rint;n->snapshot.status=n->card.last_status;
 memcpy(n->snapshot.cid,n->card.cid,sizeof(n->card.cid));n->snapshot.cache_control=n->card.ext_csd[33];
 n->snapshot.writer_dirty=n->writer?n->writer->grant.dirty:0;n->snapshot.writer_poisoned=n->writer?n->writer->grant.poisoned:0;
 n->snapshot.error=n->error;n->snapshot.cleanup_error=n->cleanup_error;
 n->snapshot.sealed=n->sealed;
}
static int read_layout(i9100_emmc_native *n)
{
 if(claim(n))return I9100_EMMC_BUSY;
 i9100_layout_io io={n,layout_read,n->card.sectors};int e=resources(n);
 if(!e)e=i9100_emmc_read_layout(&io,&n->layout);if(!e)e=i9100_emmc_check_pit(&io,&n->layout);
 if(!e){
  const i9100_emmc_partition *s=i9100_emmc_find_partition(&n->layout,"FACTORYFS"),*d=i9100_emmc_find_partition(&n->layout,"DATAFS");
  if(n->card.sectors!=30777344ULL||n->layout.count!=12U||!s||!d||s->start_lba!=344064ULL||s->sectors!=3145728ULL||
     d->start_lba!=3489792ULL||d->sectors!=12582912ULL||s->gpt_number!=9U||d->gpt_number!=10U||
     s->pit_identifier!=10U||d->pit_identifier!=11U)e=I9100_EMMC_PARTITION;
  else{
   n->snapshot.system=*s;n->snapshot.data=*d;
   n->snapshot.system_hfs_valid=i9100_emmc_hfs_volume(&io,s,&n->snapshot.system_hfs)==0;
   n->snapshot.data_hfs_valid=i9100_emmc_hfs_volume(&io,d,&n->snapshot.data_hfs)==0;
   if(!n->card.ready)e=I9100_LAYOUT_IO;
   i9100_emmc_writer_identity *i=&n->snapshot.identity;memcpy(i->cid,n->card.cid,sizeof(i->cid));
   i->sectors=n->layout.sectors;i->primary_crc=n->layout.primary_crc;i->backup_crc=n->layout.backup_crc;i->entries_crc=n->layout.entries_crc;
   memcpy(i->disk_guid,n->layout.disk_guid,16);memcpy(i->system_guid,s->unique_guid,16);memcpy(i->data_guid,d->unique_guid,16);
  }
 }release_claim(n);return e;
}
static int cleanup(i9100_emmc_native *n)
{
 for(unsigned i=0;i<4U;i++)if(n->consumers[i])return I9100_EMMC_BUSY;
 if(n->pad_restoring)return n->cleanup_error?n->cleanup_error:I9100_EMMC_READBACK;
 if(n->sdr.attempted&&n->sdr.poisoned)return n->sdr.error?n->sdr.error:I9100_EMMC_NO_DEVICE;
 if(n->writer&&n->writer->opened){int e=i9100_emmc_writer_close(n->writer);if(e)return e;}
 if(n->sdr.attempted&&n->sdr.phase==I9100_SDR_ACTIVE){
  int e=actual_idle(n);if(e)return e;e=i9100_emmc_sdr_restore(&n->card,&n->sdr);if(e)return e;
 }
 if(n->pad_owned){int e=actual_idle(n);if(e)return e;e=sdr_restore_pads(n);if(e)return e;}
 if(n->probe_attempted&&n->regs[EN_HOST]){int e=actual_idle(n);if(e)return e;}
 else if(n->clock_attempted&&n->regs[EN_HOST]){int e=inherited_idle(n);if(e)return e;}
 if(n->lease.owned){int e=i9100_emmc_host_release(&n->lease,1);if(e)return e;}
 for(unsigned i=0;i<EN_MAPS;i++){
  if(n->mapping[i]){n->mapping[i]->release();n->mapping[i]=0;n->regs[i]=0;}
  if(n->memory[i]){n->memory[i]->release();n->memory[i]=0;}
 }
 if(n->parent_hold.owned){int e=i9100_peripheral_native_hold_put(&n->parent_hold);if(e)return e;}
 return 0;
}
static void dispose(i9100_emmc_native *n)
{if(n->writer)IOFree(n->writer,sizeof(*n->writer));if(n->lifecycle)IOLockFree(n->lifecycle);if(n->transaction)IOLockFree(n->transaction);IOFree(n,sizeof(*n));}
static int native_open(int enabled,i9100_peripheral_native *parent,const i9100_emmc_inherited_profile *p,
 enum i9100_emmc_native_mode mode,const i9100_emmc_writer_identity *approved,const i9100_emmc_sdr_plan *sdr,i9100_emmc_native **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_EMMC_ARGUMENT;if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(!parent||!p||p->board_data_reviewed!=1U||p->ciu_clock_hz<400000U||p->ciu_clock_hz>200000000U||
    p->source_mux&~0xf0000U||p->source_divider&~0xff0fU||p->clksel==0xffffffffU||
    p->gpk0_mux!=0x3133U||p->gpk0_pull&~0xffU||p->gpk0_drive&~0xffU||
    p->xxti_hz!=24000000U||p->xusbxti_hz!=24000000U||
    (mode!=I9100_EMMC_N_INSPECT&&mode!=I9100_EMMC_N_DATA_RW)||(mode==I9100_EMMC_N_DATA_RW&&!approved)||
    !ml_get_interrupts_enabled()||ml_at_interrupt_context())return I9100_EMMC_ARGUMENT;
 if(sdr&&(sdr->board_reviewed!=1U||(sdr->width!=4U&&sdr->width!=8U)||sdr->high_speed>1U||sdr->vcc_mv!=3300U||
    !sdr->target_hz||sdr->target_hz>52000000U||sdr->ciu_hz!=p->ciu_clock_hz||
    (approved&&memcmp(sdr->cid,approved->cid,16))))return I9100_EMMC_ARGUMENT;
 i9100_emmc_native *n=(i9100_emmc_native *)IOMalloc(sizeof(*n));if(!n)return I9100_EMMC_IO;
 memset(n,0,sizeof(*n));n->phase=EN_BUILDING;n->parent=parent;n->profile=*p;n->mode=mode;
 int e=i9100_emmc_host_acquire(&n->lease);if(e){dispose(n);return e;}
 e=i9100_peripheral_native_hold_get(parent,&n->parent_hold);if(e){(void)i9100_emmc_host_release(&n->lease,1);dispose(n);return e;}*out=n;
 n->lifecycle=IOLockAlloc();n->transaction=IOLockAlloc();if(!n->lifecycle||!n->transaction){e=I9100_EMMC_IO;goto fail;}
 {
  const uint32_t pa[EN_MAPS]={0x12550000U,0x10030000U,0x11000000U,0x10490000U,0x10000000U};
  for(unsigned k=0;k<EN_MAPS;k++){
   n->memory[k]=IOMemoryDescriptor::withPhysicalAddress(pa[k],k==EN_CMU?131072U:4096U,kIODirectionInOut);
   n->mapping[k]=n->memory[k]?n->memory[k]->map(kIOMapInhibitCache):0;
   n->regs[k]=n->mapping[k]?(volatile uint8_t *)n->mapping[k]->getAddress():0;if(!n->regs[k]){e=I9100_EMMC_IO;goto fail;}
  }
 }
 {
  const i9100_emmc_clock_io io={n,clock_read,now,0};struct i9100_emmc_clock_result observed;
  n->clock_attempted=1;e=i9100_emmc_clock_capture(1,&io,p->xxti_hz,p->xusbxti_hz,n->clock_capture,&observed);
  if(!e&&observed.ciu_hz!=p->ciu_clock_hz)e=I9100_EMMC_CLOCK;
 }
 if(e)goto fail;e=actual_idle(n);if(e)goto fail;
 {
  i9100_emmc_io io={n,card_read,card_write,now,delay,p->ciu_clock_hz,20000000U,50000U};
  i9100_emmc_init(&n->card,&io);n->probe_attempted=1;e=i9100_emmc_probe(&n->card);if(e)goto fail;
 }
 e=read_layout(n);if(e)goto fail;
 if(approved&&!same_identity(approved,&n->snapshot.identity)){e=I9100_EMMC_OWNER;goto fail;}
 if(sdr){
  struct i9100_emmc_native_snapshot initial=n->snapshot;
  const i9100_emmc_sdr_board_ops board={n,sdr_board_check,sdr_prepare_pads};
  e=i9100_emmc_sdr_apply(1,&n->card,&n->sdr,sdr,&board);if(e)goto fail;
  /* Requalify both partition tables/HFS identities at the final rate. */
  e=read_layout(n);if(e)goto fail;if(approved&&!same_identity(approved,&n->snapshot.identity)){e=I9100_EMMC_OWNER;goto fail;}
  if(!same_identity(&initial.identity,&n->snapshot.identity)||initial.system_hfs_valid!=n->snapshot.system_hfs_valid||
     initial.data_hfs_valid!=n->snapshot.data_hfs_valid||
     (initial.system_hfs_valid&&memcmp(&initial.system_hfs,&n->snapshot.system_hfs,sizeof(initial.system_hfs)))||
     (initial.data_hfs_valid&&memcmp(&initial.data_hfs,&n->snapshot.data_hfs,sizeof(initial.data_hfs)))){e=I9100_EMMC_OWNER;goto fail;}
 }
 if(mode==I9100_EMMC_N_DATA_RW){
  if(!n->snapshot.system_hfs_valid||!n->snapshot.data_hfs_valid||n->snapshot.system_hfs.bytes!=n->snapshot.system.sectors*512ULL||
     n->snapshot.data_hfs.bytes!=n->snapshot.data.sectors*512ULL||!(n->snapshot.data_hfs.attributes&0x2000U))
   {e=I9100_LAYOUT_HFS;goto fail;}
  n->writer=(i9100_emmc_writer *)IOMalloc(sizeof(*n->writer));if(!n->writer){e=I9100_EMMC_IO;goto fail;}
  memset(n->writer,0,sizeof(*n->writer));i9100_emmc_writer_io io={n,claim,release_claim,exclusive};
  e=i9100_emmc_writer_init(n->writer,&io);
  if(!e)e=i9100_emmc_writer_open(n->writer,1,&n->card,approved,I9100_EMMC_DATA_VOLUME,
    n->snapshot.data.start_lba,n->snapshot.data.sectors);
  if(e)goto fail;
 }
 n->phase=EN_READY;save(n);return 0;
fail:n->error=e;n->phase=EN_CLOSING;
 {int off=cleanup(n);n->cleanup_error=off;save(n);if(!off){dispose(n);*out=0;}}return e;
}
int i9100_emmc_native_open(int enabled,i9100_peripheral_native *parent,const i9100_emmc_inherited_profile *p,
 enum i9100_emmc_native_mode mode,const i9100_emmc_writer_identity *approved,i9100_emmc_native **out)
{return native_open(enabled,parent,p,mode,approved,0,out);}
int i9100_emmc_native_open_sdr(int enabled,i9100_peripheral_native *parent,const i9100_emmc_inherited_profile *p,
 enum i9100_emmc_native_mode mode,const i9100_emmc_writer_identity *approved,const i9100_emmc_sdr_plan *sdr,i9100_emmc_native **out)
{if(enabled&&!sdr)return I9100_EMMC_ARGUMENT;return native_open(enabled,parent,p,mode,approved,sdr,out);}
int i9100_emmc_native_sdr_snapshot(i9100_emmc_native *n,i9100_emmc_sdr_session *out)
{if(!n||!out)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 memcpy(out,&n->sdr,sizeof(*out));IOLockUnlock(n->lifecycle);return 0;}
int i9100_emmc_native_snapshot(i9100_emmc_native *n,struct i9100_emmc_native_snapshot *out)
{if(!n||!out)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;save(n);*out=n->snapshot;IOLockUnlock(n->lifecycle);return 0;}
int i9100_emmc_native_clock_snapshot(i9100_emmc_native *n,uint8_t *out)
{
 if(!n||!out||serialize(n->lifecycle))return I9100_EMMC_ARGUMENT;
 /* This is the original PRE-PROBE read-only capture. It is not refreshed
  * after MMC initialized CLKDIV/CTYPE, so host evidence cannot quietly
  * lose the inherited loader state. Native IO verifies the live PLL rate. */
 memcpy(out,n->clock_capture,I9100_EMMC_CLOCK_BYTES);IOLockUnlock(n->lifecycle);return 0;
}
int i9100_emmc_native_validate(i9100_emmc_native *n)
{
 if(!n)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY||!n->card.ready?I9100_EMMC_NO_DEVICE:n->error?n->error:resources(n);
 if(!e)e=i9100_emmc_transport_check(&n->card);
 if(!e&&n->writer&&n->writer->grant.poisoned)e=I9100_EMMC_DIRTY;
 IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_read(i9100_emmc_native *n,uint64_t lba,uint32_t sectors,uint8_t *out)
{
 if(!n||!out)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY?I9100_EMMC_NO_DEVICE:n->error?n->error:resources(n);
 if(!e){if(claim(n))e=I9100_EMMC_BUSY;else{e=i9100_emmc_read(&n->card,lba,sectors,out);release_claim(n);}}
 if(e&&e!=I9100_EMMC_ARGUMENT&&e!=I9100_EMMC_BUSY&&e!=I9100_EMMC_NO_DEVICE&&!n->error)n->error=e;
 save(n);IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_write_data(i9100_emmc_native *n,uint64_t lba,uint32_t sectors,const uint8_t *bytes)
{
 if(!n||!bytes)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY?I9100_EMMC_NO_DEVICE:n->error?n->error:resources(n);
 if(!e&&(!n->writer||n->mode!=I9100_EMMC_N_DATA_RW))e=I9100_EMMC_PARTITION;
 if(!e&&n->sealed)e=I9100_EMMC_DIRTY;
 if(!e&&(lba<n->snapshot.data.start_lba||lba>=n->snapshot.data.start_lba+n->snapshot.data.sectors))e=I9100_EMMC_PARTITION;
 if(!e)e=i9100_emmc_writer_write(n->writer,lba-n->snapshot.data.start_lba,sectors,bytes);
 if(e&&e!=I9100_EMMC_PARTITION&&e!=I9100_EMMC_ARGUMENT&&e!=I9100_EMMC_BUSY&&!(n->sealed&&e==I9100_EMMC_DIRTY)&&!n->error)n->error=e;
 save(n);IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_synchronize(i9100_emmc_native *n)
{
 if(!n)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY?I9100_EMMC_NO_DEVICE:n->error?n->error:resources(n);
 if(!e)e=n->writer?i9100_emmc_writer_synchronize(n->writer):I9100_EMMC_UNSUPPORTED;
 if(e&&e!=I9100_EMMC_UNSUPPORTED&&e!=I9100_EMMC_BUSY&&!n->error)n->error=e;
 save(n);IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_seal_data(i9100_emmc_native *n)
{
 if(!n)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY?I9100_EMMC_NO_DEVICE:n->error?n->error:resources(n);
 if(!e&&(!n->writer||n->mode!=I9100_EMMC_N_DATA_RW))e=I9100_EMMC_UNSUPPORTED;
 if(!e&&n->sealed)e=I9100_EMMC_DIRTY;
 if(!e)e=i9100_emmc_writer_synchronize(n->writer);
 if(!e){e=actual_idle(n);if(!e)n->sealed=1;}
 if(e&&e!=I9100_EMMC_UNSUPPORTED&&e!=I9100_EMMC_BUSY&&!(n->sealed&&e==I9100_EMMC_DIRTY)&&!n->error)n->error=e;
 save(n);IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_hold_get(i9100_emmc_native *n,uintptr_t consumer)
{
 if(!n||!consumer)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 int e=n->phase!=EN_READY||n->error?I9100_EMMC_NO_DEVICE:resources(n);
 unsigned slot=4;
 for(unsigned i=0;i<4U;i++){if(n->consumers[i]==consumer)e=I9100_EMMC_BUSY;if(!n->consumers[i]&&slot==4U)slot=i;}
 if(!e&&slot==4U)e=I9100_EMMC_BUSY;if(!e)n->consumers[slot]=consumer;IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_hold_put(i9100_emmc_native *n,uintptr_t consumer)
{
 if(!n||!consumer)return I9100_EMMC_ARGUMENT;if(serialize(n->lifecycle))return I9100_EMMC_BUSY;
 unsigned slot=4;
 for(unsigned i=0;i<4U;i++)if(n->consumers[i]==consumer)slot=i;
 int e=slot==4U?I9100_EMMC_OWNER:0;if(!e)n->consumers[slot]=0;IOLockUnlock(n->lifecycle);return e;
}
int i9100_emmc_native_close(i9100_emmc_native **owner)
{
 if(!owner||!*owner)return I9100_EMMC_ARGUMENT;i9100_emmc_native *n=*owner;
 if(n->lifecycle&&take(n->lifecycle))return I9100_EMMC_BUSY;
 for(unsigned i=0;i<4U;i++)if(n->consumers[i]){if(n->lifecycle)IOLockUnlock(n->lifecycle);return I9100_EMMC_BUSY;}
 n->phase=EN_CLOSING;int e=cleanup(n);n->cleanup_error=e;save(n);
 if(n->lifecycle)IOLockUnlock(n->lifecycle);if(e)return e;*owner=0;dispose(n);return 0;
}
