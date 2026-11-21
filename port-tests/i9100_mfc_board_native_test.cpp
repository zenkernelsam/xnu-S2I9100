/* SPDX-License-Identifier: BSD-2-Clause
 * Real board, native codec, IRQ lease and allocator control flow. Facades
 * replace only IOKit/PTE/DSB, PMU status/time and firmware responses. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <mach/vm_param.h>
#include <vm/pmap.h>
#include "i9100_mfc_board_native.h"
#include "i9100_dma_native.h"
enum {MFC,MMUL,MMUR,CMU,PMU,CHIP,GIC,MAPS};
static uint32_t regs[MAPS][131072/4];
static const uint32_t addresses[MAPS]={0x13400000U,0x13620000U,0x13630000U,0x10030000U,0x10020000U,0x10000000U,0x10490000U};
static struct model {
 unsigned count,fail_at,contexts,locks,maps,descriptors,dma,prepared,barriers,parent_fail;
 unsigned drop_region,drop_offset,drop_count,stuck_power,stuck_reset,stuck_div,freeze,writes,board_writes,gic_writes;
 unsigned riscs,reads,illegal_reads,poison_return,suppress_access;
 uint64_t ns;bool interrupts;
} m;
static unsigned fail(){return ++m.count==m.fail_at;}
void *IOMalloc(size_t bytes){if(fail())return nullptr;m.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(p&&m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;return new IOLock{false};}
void IOLockFree(IOLock *l){assert(l&&!l->held&&m.locks);m.locks--;delete l;}
bool IOLockTryLock(IOLock *l){if(l->held)return false;l->held=true;return true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;}
void OSSynchronizeIO(){m.barriers++;}
void OSMemoryBarrier(){m.barriers++;}
extern "C" int ml_get_interrupts_enabled(){return m.interrupts;}
extern "C" int ml_at_interrupt_context(){return 0;}
void IODelay(unsigned us){if(!m.freeze)m.ns+=us*1000ULL;}
extern "C" uint64_t mach_absolute_time(){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
struct i9100_peripheral_native {unsigned holds; i9100_irq_registry irqs;};
static i9100_peripheral_native parent;
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);h->context=p;h->self=(uintptr_t)h;h->owned=1;p->holds++;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->owned&&h->self==(uintptr_t)h&&h->context->holds);if(m.parent_fail)return -99;h->owned=0;h->context->holds--;return 0;}
i9100_irq_registry *i9100_peripheral_native_irqs(i9100_peripheral_native *p){return &p->irqs;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t pa,size_t bytes,unsigned direction)
{if(fail())return nullptr;assert(direction==kIODirectionInOut);m.descriptors++;auto *d=new IOMemoryDescriptor;d->physical=pa;d->bytes=bytes;return d;}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 if(fail())return nullptr;assert(flags==kIOMapInhibitCache);
 for(unsigned i=0;i<MAPS;i++)if(addresses[i]==physical){
  assert(bytes==(i==CMU?(bytes==131072?131072:65536):i==MFC?65536:i==PMU?16384:4096));
  auto *v=new IOMemoryMap;v->address=(uintptr_t)regs[i];m.maps++;return v;
 }assert(false);return nullptr;
}
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
static unsigned region(volatile uint8_t *base)
{for(unsigned n=0;n<MAPS;n++)if(base==(volatile uint8_t *)regs[n])return n;assert(false);return 0;}
extern "C" uint32_t __real_i9100_camera_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_camera_mmio_read(volatile uint8_t *base,unsigned off)
{
 unsigned n=region(base);m.reads++;
 if(n==MFC||n==MMUL||n==MMUR){
  if((regs[PMU][0x3c44/4]&7U)!=7U||(regs[CMU][0xc928/4]&7U)!=7U)m.illegal_reads++;
  assert(!m.illegal_reads);
 }
 return __real_i9100_camera_mmio_read(base,off);
}
extern "C" void __wrap_i9100_camera_mmio_write(volatile uint8_t *base,unsigned off,uint32_t v)
{
 unsigned n=region(base);m.writes++;
 if(n==CMU||n==PMU){unsigned mask=n==PMU&&off==0x3c40?7U:n==CMU&&off==0xc928?7U:n==CMU&&off==0xc228?0x101U:n==CMU&&off==0xc528?15U:0;
  assert(mask&&((v^regs[n][off/4])&~mask)==0);m.board_writes++;
 }else assert(n==MFC);
 if(m.drop_count&&m.drop_region==n&&m.drop_offset==off){m.drop_count--;return;}
 __real_i9100_camera_mmio_write(base,off,v);
 if(n==PMU&&off==0x3c40&&!m.stuck_power)regs[PMU][0x3c44/4]=(regs[PMU][0x3c44/4]&~7U)|(v&7U);
 if(n==CMU&&off==0xc528&&m.stuck_div)regs[CMU][0xc628/4]=1;
 if(n==MFC&&off==0){if(v==0x3ffU)m.riscs++;else if(!m.stuck_reset)regs[MFC][0x510/4]=0;}
 if(n==MFC&&off==0x30&&v)regs[MFC][off/4]=0;
}
static uint32_t irq_read(void *,unsigned off){return regs[GIC][off/4];}
static void irq_write(void *,unsigned off,uint32_t v)
{
 m.gic_writes++;
 assert(off==0x18c||off==0x47c||off==0x87c||off==0xc1c);
 if(m.drop_count&&m.drop_region==GIC&&m.drop_offset==off){m.drop_count--;return;}
 if(off==0x18c){assert(v==(1U<<30));regs[GIC][0x10c/4]&=~v;}else regs[GIC][off/4]=v;
}
static uint32_t irq_enter(void *){return 0;}
static void irq_leave(void *,uint32_t){}
static uint8_t bank_a[8388608] __attribute__((aligned(131072))),bank_b[8388608] __attribute__((aligned(131072)));
static uint32_t cpu_ptes[2][2048],linear_ptes[2][2048];static bool allocated[2];
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withOptions(unsigned options,size_t bytes,size_t alignment)
{
 if(fail())return nullptr;assert(options==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache)&&alignment==131072U&&bytes<=8388608U);
 unsigned i=allocated[0]?1U:0U;assert(!allocated[i]);allocated[i]=true;m.dma++;
 auto *d=new IOBufferMemoryDescriptor;d->data=i?bank_b:bank_a;d->pa=i?0x48000000U:0x46000000U;d->bytes=bytes;d->prepared=0;return d;
}
IOReturn IOBufferMemoryDescriptor::prepare(unsigned direction){assert(direction==kIODirectionInOut&&!prepared);if(fail())return -1;prepared=1;m.prepared++;return 0;}
IOReturn IOBufferMemoryDescriptor::complete(unsigned direction){assert(direction==kIODirectionInOut&&prepared&&m.prepared);prepared=0;m.prepared--;return 0;}
addr64_t IOBufferMemoryDescriptor::getPhysicalSegment(unsigned off,IOByteCount *span){assert(!off&&prepared);*span=bytes;return pa;}
void *IOBufferMemoryDescriptor::getBytesNoCopy(){return data;}
void IOBufferMemoryDescriptor::release(){assert(!prepared&&m.dma);unsigned i=data==bank_b;assert(allocated[i]);allocated[i]=false;m.dma--;delete this;}
extern "C" {
unsigned long gPhysBase=0x40000000U,gVirtBase=0x80000000U;
pmap_t kernel_pmap=(void *)1;
void pmap_set_cache_attributes(ppnum_t page,unsigned flags)
{assert(flags==(VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED));unsigned i=page>=0x48000U;unsigned index=page-(i?0x48000U:0x46000U);assert(index<2048);
 cpu_ptes[i][index]=linear_ptes[i][index]=(page<<12)|0x42U;}
vm_offset_t pmap_pte(pmap_t map,vm_offset_t va)
{assert(map==kernel_pmap);for(unsigned i=0;i<2;i++){
 uintptr_t low=(uintptr_t)(i?bank_b:bank_a),linear=i?0x88000000U:0x86000000U;
 if(va>=low&&va<low+8388608U)return (uintptr_t)&cpu_ptes[i][(va-low)/4096U];
 if(va>=linear&&va<linear+8388608U)return (uintptr_t)&linear_ptes[i][(va-linear)/4096U];}
 return 0;}
int i9100_bcm_fw_kernel_sha256(void *,const void *bytes,uint32_t size,uint8_t out[32]){return SHA256((const uint8_t *)bytes,size,out)?0:-1;}
}
static uint8_t firmware[262144];static const uint8_t stream[]={0,0,0,1,0x67,0x42,0,0x1e,0xf4,0x05,0x01,0xec,0x80};
static i9100_mfc_firmware_profile profile()
{i9100_mfc_firmware_profile f={};for(unsigned i=0;i<sizeof(firmware);i++)firmware[i]=(uint8_t)(i*13U+7U);
 f.board_data_reviewed=1;f.system_bytes=0x80000;f.firmware_bytes=sizeof(firmware);f.expected_version=0x211116;f.timeout_ms=500;assert(SHA256(firmware,sizeof(firmware),f.sha256));return f;}
static const i9100_mfc_power_config cfg={1,24000000U,200000000U};
static void setup(unsigned warm=0)
{
 assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.prepared&&!parent.holds);
 memset(&m,0,sizeof(m));memset(&parent,0,sizeof(parent));memset(regs,0,sizeof(regs));m.ns=1000000;m.interrupts=true;
 regs[CHIP][0]=0x43210011;regs[CMU][0x14200/4]=256;regs[CMU][0x14108/4]=0xa0640301;
 regs[CMU][0xc928/4]=0x80000000U|warm;regs[CMU][0xc228/4]=0x8101;regs[CMU][0xc528/4]=0x4007;
 regs[PMU][0x3c40/4]=0x900U|(warm?7:0);regs[PMU][0x3c44/4]=0x100U|(warm?7:0);
 regs[GIC][0x47c/4]=0xabcd1234;regs[GIC][0x87c/4]=0x10203450;regs[GIC][0xc1c/4]=0xffffffff;
 uint32_t allowed[8]={};allowed[126/32]=1U<<(126%32);i9100_irq_io io={nullptr,irq_read,irq_write,irq_enter,irq_leave};
 assert(!i9100_irq_registry_init(&parent.irqs,&io,allowed));assert(!i9100_irq_registry_enable(&parent.irqs,1));
}
static void close_board(i9100_mfc_board **board)
{int e=i9100_mfc_board_close(board);if(e)fprintf(stderr,"boardclose=%d maps=%u parents=%u power=%x/%x gate=%x\n",e,m.maps,parent.holds,regs[PMU][0x3c40/4],regs[PMU][0x3c44/4],regs[CMU][0xc928/4]);
 assert(!e&&!*board&&!m.maps&&!m.descriptors&&!m.contexts&&!m.locks&&!parent.holds);}
static void response(unsigned r,unsigned arg=0)
{regs[MFC][0x44/4]=r;regs[MFC][0x48/4]=arg;regs[MFC][0x4c/4]=regs[MFC][0x50/4]=regs[MFC][0x54/4]=0;regs[MFC][8/4]=1;regs[MFC][0x58/4]=0x211116;}
static void start(i9100_mfc_native *n)
{auto f=profile();assert(!i9100_mfc_native_start(n,&f,firmware,sizeof(firmware),stream,sizeof(stream)));
 response(9);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);response(8,0x60000);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);
 response(1,2);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);
 regs[MFC][0x2000/4]=2;regs[MFC][0x2008/4]=640;regs[MFC][0x2004/4]=480;regs[MFC][0x200c/4]=4;regs[MFC][0x2018/4]=sizeof(stream);
 response(4);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);regs[MFC][0x2000/4]=2;response(15);assert(!i9100_mfc_native_poll(n));}
static void integration(unsigned warm)
{
 setup(warm);i9100_mfc_board *board=nullptr,*other=nullptr;assert(!i9100_mfc_board_open(1,&parent,&cfg,&board));
 assert(m.maps==7&&!m.riscs&&(regs[CMU][0xc528/4]&15U)==3U);
 unsigned writes=m.writes;assert(i9100_mfc_board_open(1,&parent,&cfg,&other)==I9100_MP_BUSY&&!other&&m.writes==writes);
 i9100_mfc_supply supply={};assert(!i9100_mfc_board_supply(board,&supply)&&supply.host);
 i9100_mfc_native *n=nullptr,*legacy=nullptr;i9100_mfc_decode_config dc={1,8388608,2097152};
 i9100_mfc_supply wrong=supply;wrong.host=nullptr;
 assert(i9100_mfc_native_open(1,&parent,&wrong,&legacy)==I9100_MP_BUSY&&!legacy&&m.maps==7&&parent.holds==1&&m.writes==writes);
 assert(!i9100_mfc_native_open_decode(1,&parent,&supply,&dc,&n));assert(parent.holds==2&&m.maps==13&&m.dma==2);
 assert(i9100_mfc_native_open(1,&parent,&supply,&legacy)==I9100_MFC_BUSY&&!legacy);
 assert(i9100_mfc_board_close(&board)==I9100_MP_BUSY&&board);start(n);
 assert(!i9100_mfc_native_frame(n,stream,sizeof(stream),1,0));regs[MFC][0x2000/4]=2;regs[MFC][0x201c/4]=1;
 regs[MFC][0x202c/4]=0;regs[MFC][0x2020/4]=1;regs[MFC][0x2018/4]=sizeof(stream);
 regs[MFC][0x2010/4]=regs[MFC][0x2024/4]=0x48000000U>>11;
 regs[MFC][0x2014/4]=regs[MFC][0x2028/4]=(0x46000000U>>11)+regs[MFC][0x600/4];
 uint32_t *sh=(uint32_t *)(bank_a+0x116000);sh[8/4]=1;sh[0xc0/4]=4;response(5);assert(!i9100_mfc_native_poll(n));
 i9100_mfc_frame_lease lease={};assert(!i9100_mfc_native_acquire(n,&lease));assert(i9100_mfc_native_close(&n)==I9100_MFC_BUSY&&n);
 assert(i9100_mfc_board_close(&board)==I9100_MP_BUSY&&board&&m.dma==2&&(regs[PMU][0x3c44/4]&7U)==7U);
 assert(!i9100_mfc_native_release(n,&lease));assert(!i9100_mfc_native_close(&n)&&!n&&m.maps==7&&!m.dma&&parent.holds==1);
 close_board(&board);assert(regs[CMU][0xc928/4]==(0x80000000U|warm)&&regs[PMU][0x3c40/4]==(0x900U|(warm?7:0)));
 assert(regs[CMU][0xc228/4]==0x8101&&regs[CMU][0xc528/4]==0x4007);
 assert(regs[GIC][0x47c/4]==0xabcd1234&&regs[GIC][0x87c/4]==0x10203450&&regs[GIC][0xc1c/4]==0xffffffff);
}
static void native_failures()
{
 for(unsigned fault=0;fault<4;fault++){
  setup();i9100_mfc_board *b=nullptr;assert(!i9100_mfc_board_open(1,&parent,&cfg,&b));
  i9100_mfc_supply s={};assert(!i9100_mfc_board_supply(b,&s));i9100_mfc_decode_config dc={1,8388608,2097152};
  i9100_mfc_native *n=nullptr;assert(!i9100_mfc_native_open_decode(1,&parent,&s,&dc,&n));start(n);
  i9100_mfc_supply_lease fake={};unsigned writes=m.writes;
  assert(s.acquire(s.cookie,&fake)==I9100_MP_BUSY&&m.writes==writes&&!fake.owned);
  assert(s.validate(s.cookie,&fake)==I9100_MP_OWNER); /* Must not corrupt the real owner. */
  if(fault==0){
   regs[CMU][0x14108/4]^=1U<<16;
   assert(i9100_mfc_native_frame(n,stream,sizeof(stream),1,0)==I9100_MFC_OWNER);
   struct i9100_mfc_native_status ns={};assert(!i9100_mfc_native_status(n,&ns)&&ns.phase==I9100_MFC_N_FAILED);
   assert(i9100_mfc_native_close(&n)!=0&&n&&m.dma==2&&i9100_mfc_board_close(&b)==I9100_MP_BUSY);
   regs[CMU][0x14108/4]^=1U<<16;
   assert(i9100_mfc_native_frame(n,stream,sizeof(stream),1,0)==I9100_MFC_OWNER); /* Original failure remains latched. */
  }else if(fault==1){
   assert(!i9100_mfc_native_frame(n,stream,sizeof(stream),1,0));m.stuck_reset=1;regs[MFC][0x510/4]=3;m.ns+=500000001ULL;
   assert(i9100_mfc_native_poll(n)==I9100_MFC_TIMEOUT&&n&&m.dma==2);
   assert(i9100_mfc_board_close(&b)==I9100_MP_BUSY&&b);m.stuck_reset=0;
  }else if(fault==2){
   m.drop_region=CMU;m.drop_offset=0xc928;m.drop_count=1;
   /* Run codec shutdown without disabling its supply; board cleanup later
    * must retain the gate/IRQ/root if the restore write is lost. */
  }else{
   for(unsigned i=0;i<2048;i++)linear_ptes[0][i]&=~0x40U;
   assert(i9100_mfc_native_frame(n,stream,sizeof(stream),1,0)==I9100_MFC_OWNER);
   for(unsigned i=0;i<2048;i++)linear_ptes[0][i]|=0x40U;
  }
  assert(!i9100_mfc_native_close(&n)&&!n&&!m.dma);
  if(fault==2){assert(i9100_mfc_board_close(&b)==I9100_MP_IO&&b&&m.maps==7);assert(!s.host->parent);
   i9100_mfc_host_lease attempted={};assert(i9100_mfc_host_acquire(s.host,&attempted)==I9100_MP_BUSY);}
  close_board(&b);
 }
 for(unsigned allocation=1;allocation<=23;allocation++){
  setup();i9100_mfc_board *b=nullptr;assert(!i9100_mfc_board_open(1,&parent,&cfg,&b));i9100_mfc_supply s={};assert(!i9100_mfc_board_supply(b,&s));
  i9100_mfc_decode_config dc={1,8388608,2097152};i9100_mfc_native *n=nullptr;
  m.fail_at=m.count+allocation;int e=i9100_mfc_native_open_decode(1,&parent,&s,&dc,&n);
  if(e)assert(!n&&m.maps==7&&!m.dma&&parent.holds==1);else assert(!i9100_mfc_native_close(&n));
  close_board(&b);
 }
}
static void failures()
{
 i9100_mfc_board *board=nullptr;
 setup();assert(i9100_mfc_board_open(0,nullptr,nullptr,&board)==I9100_MP_DISABLED&&!m.count&&!m.reads);
 for(unsigned i=1;i<=17;i++){setup();m.fail_at=i;assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_MP_IO&&!board);assert(!m.contexts&&!parent.holds);}
 setup();m.drop_region=GIC;m.drop_offset=0x47c;m.drop_count=1;
 assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_IRQ_IO&&!board&&!m.writes);
 setup();m.drop_region=CMU;m.drop_offset=0xc928;m.drop_count=1;
 assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_MP_IO&&!board);
 setup();m.stuck_power=m.freeze=1;assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_MP_TIMEOUT&&board&&m.maps==7);
 m.stuck_power=m.freeze=0;regs[PMU][0x3c44/4]|=7;close_board(&board);
 setup(7);regs[MFC][0]=0x3ff;assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_MP_BUSY&&board&&!m.riscs);
 regs[MFC][0]=0;close_board(&board);
 setup();regs[CHIP][0]=0x43220000;assert(i9100_mfc_board_open(1,&parent,&cfg,&board)==I9100_MP_OWNER&&!board&&!m.gic_writes&&!m.writes);
 setup();assert(!i9100_mfc_board_open(1,&parent,&cfg,&board));m.drop_region=PMU;m.drop_offset=0x3c40;m.drop_count=1;
 assert(i9100_mfc_board_close(&board)==I9100_MP_IO&&board&&m.maps==7);close_board(&board);
 setup();assert(!i9100_mfc_board_open(1,&parent,&cfg,&board));regs[CMU][0xc228/4]=0x8100;
 assert(i9100_mfc_board_close(&board)==I9100_MP_OWNER&&board);regs[CMU][0xc228/4]=0x8000;close_board(&board);
 setup();assert(!i9100_mfc_board_open(1,&parent,&cfg,&board));m.parent_fail=1;
 assert(i9100_mfc_board_close(&board)==-99&&board&&!m.maps&&parent.holds==1);m.parent_fail=0;close_board(&board);
}
int main(){integration(0);integration(7);failures();native_failures();puts("PASS actual MFC board -> real supplier -> native/allocator -> mailbox/DPB -> lease -> reset -> local clock/power restore; explicit IOKit/PTE/DSB/PMU/firmware facades, construction/fault/conflict/borrowed-close/retained cleanup; hardware=false");return 0;}
