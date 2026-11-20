/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_clock_native.h"
#include "i9100_emmc_host.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <string.h>
#undef mach_absolute_time
#undef absolutetime_to_nanoseconds
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
uint32_t i9100_camera_mmio_read(volatile uint8_t *,unsigned);
uint8_t i9100_emmc_clock_observation[512];
volatile uint32_t i9100_emmc_clock_observation_diag[8]={1U};
}
struct i9100_emmc_clock_observer {
 uintptr_t self;i9100_emmc_host_lease host;IOLock *lock;
 IOMemoryDescriptor *memory[EC_REGIONS];IOMemoryMap *mapping[EC_REGIONS];volatile uint8_t *regs[EC_REGIONS];
 uint8_t record[512];uint32_t captured;int result,cleanup_error;
};
static uint32_t read32(void *cookie,unsigned region,unsigned off)
{i9100_emmc_clock_observer *o=(i9100_emmc_clock_observer *)cookie;return region<EC_REGIONS&&o->regs[region]?i9100_camera_mmio_read(o->regs[region],off):0xffffffffU;}
static uint64_t now(void *){uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}
static int context(){return ml_get_interrupts_enabled()&&!ml_at_interrupt_context();}
static int cleanup(i9100_emmc_clock_observer *o)
{
 /* Before host mapping, the observer did not touch its registers. With
  * host mapped, prove current command/FIFO/data/DMA idle before releasing
  * the shared command owner. Busy or gated maps/lease remain pinned. */
 if(o->regs[EC_HOST]){
  uint32_t gate=read32(o,EC_CMU,0xc940),mask=read32(o,EC_CMU,0xc340);
  if(gate==0xffffffffU||mask==0xffffffffU||!(gate&(1U<<9))||!(mask&(1U<<16)))return I9100_EMMC_BUSY;
  uint32_t ctrl=read32(o,EC_HOST,EM_CTRL),cmd=read32(o,EC_HOST,EM_CMD),status=read32(o,EC_HOST,EM_STATUS),bmod=read32(o,EC_HOST,EM_BMOD);
  if(ctrl==0xffffffffU||cmd==0xffffffffU||status==0xffffffffU||bmod==0xffffffffU||ctrl&(7U|(1U<<5)|(1U<<25))||cmd&(1U<<31)||
     status&((15U<<4)|(1U<<9)|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31))||bmod&(1U<<7)||
     read32(o,EC_HOST,EM_INTMASK)||read32(o,EC_HOST,EM_IDINTEN)||read32(o,EC_GIC,0x10c)&(1U<<13))return I9100_EMMC_BUSY;
 }
 if(o->host.owned){int e=i9100_emmc_host_release(&o->host,1);if(e)return e;}
 for(unsigned i=0;i<EC_REGIONS;i++){
  if(o->mapping[i]){o->mapping[i]->release();o->mapping[i]=0;o->regs[i]=0;}
  if(o->memory[i]){o->memory[i]->release();o->memory[i]=0;}
 }return 0;
}
int i9100_emmc_clock_observe_open(int enabled,uint32_t xxti,uint32_t usb,i9100_emmc_clock_observer **owner)
{
 if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(enabled!=1||!owner||*owner||!context()||xxti!=24000000U||usb!=24000000U)return I9100_EMMC_ARGUMENT;
 i9100_emmc_clock_observer *o=(i9100_emmc_clock_observer *)IOMalloc(sizeof(*o));if(!o)return I9100_EMMC_IO;
 memset(o,0,sizeof(*o));o->self=(uintptr_t)o;int e=i9100_emmc_host_acquire(&o->host);
 if(e){IOFree(o,sizeof(*o));return e;}*owner=o;
 o->lock=IOLockAlloc();if(!o->lock){e=I9100_EMMC_IO;goto failed;}
 {
  const uint32_t pa[5]={0x12550000,0x10030000,0x11000000,0x10490000,0x10000000};
  const unsigned order[5]={EC_CMU,EC_GPIO,EC_GIC,EC_CHIP,EC_HOST};
  for(unsigned n=0;n<5;n++){
   unsigned i=order[n];if(i==EC_HOST){uint32_t gate=read32(o,EC_CMU,0xc940),mask=read32(o,EC_CMU,0xc340);
    if(gate==0xffffffffU||mask==0xffffffffU||!(gate&(1U<<9))||!(mask&(1U<<16)))break;}
   o->memory[i]=IOMemoryDescriptor::withPhysicalAddress(pa[i],i==EC_CMU?131072U:4096U,kIODirectionInOut);
   o->mapping[i]=o->memory[i]?o->memory[i]->map(kIOMapInhibitCache):0;
   o->regs[i]=o->mapping[i]?(volatile uint8_t *)o->mapping[i]->getAddress():0;if(!o->regs[i]){e=I9100_EMMC_IO;goto failed;}
  }
 }
 {
  const i9100_emmc_clock_io io={o,read32,now,0};i9100_emmc_clock_result result;
  e=i9100_emmc_clock_capture(1,&io,xxti,usb,o->record,&result);o->captured=1;
 }
 o->result=e;return e;
failed:
 o->result=e;o->cleanup_error=cleanup(o);
 if(!o->cleanup_error){if(o->lock)IOLockFree(o->lock);IOFree(o,sizeof(*o));*owner=0;}return e;
}
int i9100_emmc_clock_observe_copy(i9100_emmc_clock_observer *o,uint8_t *out)
{
 if(!o||o->self!=(uintptr_t)o||!out||!o->captured||!context())return I9100_EMMC_ARGUMENT;
 IOLockLock(o->lock);memcpy(out,o->record,512);IOLockUnlock(o->lock);return 0;
}
int i9100_emmc_clock_observe_close(i9100_emmc_clock_observer **owner)
{
 if(!owner||!*owner||(*owner)->self!=(uintptr_t)*owner||!context())return I9100_EMMC_ARGUMENT;
 auto *o=*owner;if(o->lock)IOLockLock(o->lock);int e=cleanup(o);o->cleanup_error=e;if(o->lock)IOLockUnlock(o->lock);
 if(e)return e;if(o->lock)IOLockFree(o->lock);IOFree(o,sizeof(*o));*owner=0;return 0;
}
void ios6i9100_register_emmc_clock_observation()
{
 unsigned enabled=0,root=0,legacy=0,xtal=0,usb=0;
 if(!PE_parse_boot_argn("i9100emmcobserve",&enabled,4)||enabled!=1)return;
 (void)PE_parse_boot_argn("i9100emmcdata",&root,4);(void)PE_parse_boot_argn("i9100emmcro",&legacy,4);
 (void)PE_parse_boot_argn("i9100xxtihz",&xtal,4);(void)PE_parse_boot_argn("i9100xusbxtihz",&usb,4);
 char selected[128]={0};
 if(!PE_parse_boot_argn("rd",selected,sizeof(selected)))
  (void)PE_parse_boot_argn("rootdev",selected,sizeof(selected));
 const char *root_name=selected[0]=='*'?selected+1:selected;
 static volatile uint32_t once;static i9100_emmc_clock_observer *retained;
 if(!__sync_bool_compare_and_swap(&once,0U,1U))return;
 int e=root||legacy||!strcmp(root_name,"i9100-system")?I9100_EMMC_BUSY:
  i9100_emmc_clock_observe_open(1,xtal,usb,&retained);
 if(retained&&retained->captured)(void)i9100_emmc_clock_observe_copy(retained,i9100_emmc_clock_observation);
 i9100_emmc_clock_observation_diag[1]=1;i9100_emmc_clock_observation_diag[2]=(uint32_t)e;
 i9100_emmc_clock_observation_diag[3]=retained?retained->captured:0;
 int off=retained?i9100_emmc_clock_observe_close(&retained):0;i9100_emmc_clock_observation_diag[4]=(uint32_t)off;
 i9100_emmc_clock_observation_diag[5]=retained!=0;
 IOLog("i9100 EMMCCLOCK captured=%u rc=%d cleanup=%d retained=%u; no MMC commands/writes\n",
  i9100_emmc_clock_observation_diag[3],e,off,i9100_emmc_clock_observation_diag[5]);
}
