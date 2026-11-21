/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/vm_param.h>
#include <vm/pmap.h>
#include "i9100_mfc_native.h"
#include "i9100_mfc_board_native.h"
#include "i9100_dma_native.h"
/* This model has no board provider: no IRQ-owner token can ever be issued,
 * so provider verification honestly refuses every candidate as foreign
 * before any hardware path. The raw (unbound) entries never reach it. */
int i9100_mfc_board_irq_verify(i9100_mfc_board *,uintptr_t,unsigned *)
{return I9100_MP_OWNER;}
enum {MFC,MMU_L,MMU_R,CMU,PMU,GIC,MAPS};
static uint32_t regs[MAPS][65536/4];
static const uint32_t addresses[MAPS]={0x13400000U,0x13620000U,0x13630000U,0x10030000U,0x10020000U,0x10490000U};
static struct model {
 unsigned allocations,fail_at,contexts,locks,maps,descriptors,dma,parent_put_error;
 unsigned supply_owned,generation,acquire_error,release_error,verify_error,reset_stuck,freeze;
 unsigned writes,hash_error,syncs,sync_fail,drop_offset,drop_count,h2r_command,ch_command,nc_bad,nc_checks;
 uint64_t ns;bool interrupts;
} m;
struct i9100_peripheral_native {unsigned holds;};
static i9100_peripheral_native parent;
static unsigned fail(){return ++m.allocations==m.fail_at;}
void *IOMalloc(size_t bytes){if(fail())return nullptr;m.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(p&&m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;return new IOLock{false};}
void IOLockFree(IOLock *l){assert(l&&!l->held&&m.locks);m.locks--;delete l;}
bool IOLockTryLock(IOLock *l){if(l->held)return false;l->held=true;return true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;}
extern "C" bool ml_get_interrupts_enabled(){return m.interrupts;}
void OSSynchronizeIO(){}
void IODelay(unsigned us){if(!m.freeze)m.ns+=us*1000ULL;}
extern "C" uint64_t mach_absolute_time(){return m.ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t pa,size_t bytes,unsigned direction)
{
 if(fail())return nullptr;assert(direction==kIODirectionInOut);m.descriptors++;
 auto *d=new IOMemoryDescriptor;d->physical=pa;d->bytes=bytes;return d;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 if(fail())return nullptr;assert(flags==kIOMapInhibitCache);
 for(unsigned k=0;k<MAPS;k++)if(physical==addresses[k]){
  assert(bytes==(k==MFC||k==CMU?65536U:k==PMU?16384U:4096U));m.maps++;
  auto *v=new IOMemoryMap;v->address=(uintptr_t)regs[k];return v;
 }assert(false);return nullptr;
}
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
extern "C" uint32_t __real_i9100_camera_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_camera_mmio_read(volatile uint8_t *base,unsigned off)
{return __real_i9100_camera_mmio_read(base,off);}
extern "C" void __wrap_i9100_camera_mmio_write(volatile uint8_t *base,unsigned off,uint32_t value)
{
 assert(base==(volatile uint8_t *)regs[MFC]);m.writes++;
 if(m.drop_count&&m.drop_offset==off){m.drop_count--;return;}
 __real_i9100_camera_mmio_write(base,off,value);
 if(off==0&&value!=0x3ffU&&!m.reset_stuck)regs[MFC][0x510/4]=0;
 if(off==0x30&&value){m.h2r_command=value;regs[MFC][off/4]=0;}
 if(off==0x2040&&value!=0xffffffffU)m.ch_command=value;
}
extern "C" int i9100_bcm_fw_kernel_sha256(void *,const void *bytes,uint32_t size,uint8_t digest[32])
{if(m.hash_error)return -1;return SHA256((const uint8_t *)bytes,size,digest)?0:-1;}
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);p->holds++;h->context=p;h->self=(uintptr_t)h;h->owned=1;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->self==(uintptr_t)h&&h->owned&&h->context->holds);if(m.parent_put_error)return -1;h->context->holds--;h->owned=0;return 0;}
static uint8_t bank_a[8388608] __attribute__((aligned(131072))),bank_b[8388608] __attribute__((aligned(131072)));
static bool dma_slots[2];
#if !defined(I9100_MFC_ACTUAL_ALLOCATOR)
struct i9100_dma_native {i9100_dma_buffer b;void *engine;int (*idle)(void *);bool locked;unsigned index;};
static int d_lock(void *p){auto *d=(i9100_dma_native *)p;if(d->locked)return -1;d->locked=true;return 0;}
static void d_unlock(void *p){auto *d=(i9100_dma_native *)p;assert(d->locked);d->locked=false;}
static int d_idle(void *p){auto *d=(i9100_dma_native *)p;return d->idle(d->engine);}
static int d_sync(void *p,uintptr_t cpu,uint32_t physical,uint32_t bytes,unsigned action)
{auto *d=(i9100_dma_native *)p;assert(d->locked&&cpu==d->b.cpu&&physical==d->b.physical&&bytes==d->b.bytes);
 if(action==I9100_DMA_INVALIDATE)assert(!d_idle(d));return ++m.syncs==m.sync_fail?-1:0;}
static void d_free(void *p,uintptr_t,uint32_t,uint32_t){assert(!d_idle(p));}
int i9100_dma_native_allocate_aligned(int enabled,uint32_t bytes,uint32_t alignment,void *engine,int (*idle)(void *),i9100_dma_native **out)
{
 assert(enabled==1&&alignment==131072U&&out&&!*out);if(fail())return I9100_DMA_IO;
 unsigned index=dma_slots[0]?1U:0U;assert(!dma_slots[index]);auto *d=new i9100_dma_native{};
 d->index=index;d->engine=engine;d->idle=idle;
 i9100_dma_io io={d,d_lock,d_unlock,d_sync,d_idle,d_free};
 assert(!i9100_dma_bind(&d->b,&io,(uintptr_t)(index?bank_b:bank_a),index?0x48000000U:0x46000000U,bytes));
 m.dma++;dma_slots[index]=true;*out=d;return 0;
}
i9100_dma_buffer *i9100_dma_native_buffer(i9100_dma_native *d){return d?&d->b:nullptr;}
int i9100_dma_native_destroy(i9100_dma_native **out)
{
 assert(out&&*out);auto *d=*out;int e=i9100_dma_release(&d->b);if(e)return e;
 assert(dma_slots[d->index]&&m.dma);m.dma--;dma_slots[d->index]=false;delete d;*out=nullptr;return 0;
}
#else
#include "i9100_mfc_dma_facade.inc"
#endif
static uint32_t cpu_ptes[2][2048],linear_ptes[2][2048];
extern "C" {
unsigned long gPhysBase=0x40000000U,gVirtBase=0x80000000U;
pmap_t kernel_pmap=(void *)1;
#if defined(I9100_MFC_ACTUAL_ALLOCATOR)
void pmap_set_cache_attributes(ppnum_t page,unsigned flags)
{
 assert(flags==(VM_MEM_COHERENT|VM_MEM_NOT_CACHEABLE|VM_MEM_GUARDED));
 unsigned k=page>=(0x48000000U>>12)?1U:0U,index=page-((k?0x48000000U:0x46000000U)>>12);assert(index<2048);
 cpu_ptes[k][index]=(page<<12)|2U|0x40U;linear_ptes[k][index]=(page<<12)|2U|0x40U;
}
#endif
vm_offset_t pmap_pte(pmap_t map,vm_offset_t va)
{
 assert(map==kernel_pmap);m.nc_checks++;
 for(unsigned k=0;k<2;k++){
  uintptr_t lo=(uintptr_t)(k?bank_b:bank_a);uint32_t linear=k?0x88000000U:0x86000000U;
  if(va>=lo&&va<lo+8388608U){unsigned i=(unsigned)((va-lo)/4096U);return (uintptr_t)&cpu_ptes[k][i];}
  if(va>=linear&&va<linear+8388608U){unsigned i=(unsigned)((va-linear)/4096U);
   if(m.nc_bad)linear_ptes[k][i]&=~0x40U;return (uintptr_t)&linear_ptes[k][i];}
 }return 0;
}
}
static int acquire(void *,i9100_mfc_supply_lease *l)
{
 assert(!m.supply_owned&&!l->owned);m.supply_owned=1;m.generation++;l->self=(uintptr_t)l;l->provider=(uintptr_t)&m;l->owned=1;l->generation=m.generation;
 regs[CMU][0xc928/4]=7;regs[PMU][0x3c40/4]=regs[PMU][0x3c44/4]=7;return m.acquire_error?-123:0;
}
static int validate(void *,const i9100_mfc_supply_lease *l)
{return m.verify_error||l->self!=(uintptr_t)l||l->provider!=(uintptr_t)&m||!l->owned||!m.supply_owned||l->generation!=m.generation?-1:0;}
static int release(void *,i9100_mfc_supply_lease *l)
{
 assert(l->owned&&m.supply_owned);if(m.release_error)return -124;
 regs[CMU][0xc928/4]=0;regs[PMU][0x3c40/4]=regs[PMU][0x3c44/4]=0;l->owned=0;m.supply_owned=0;return 0;
}
static const i9100_mfc_supply supply={nullptr,acquire,validate,release,nullptr};
static uint8_t firmware[262144];
static const uint8_t stream[]={0,0,0,1,0x67,0x42,0,0x1e,0xf4,0x05,0x01,0xec,0x80};
static i9100_mfc_firmware_profile profile()
{
 for(unsigned k=0;k<sizeof(firmware);k++)firmware[k]=(uint8_t)(k*13U+7U);
 i9100_mfc_firmware_profile p={};p.board_data_reviewed=1;p.firmware_bytes=sizeof(firmware);p.system_bytes=0x80000;
 p.expected_version=0x211116;p.timeout_ms=500;assert(SHA256(firmware,sizeof(firmware),p.sha256));return p;
}
static void setup()
{
 assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.supply_owned&&!parent.holds&&!dma_slots[0]&&!dma_slots[1]);
 memset(&m,0,sizeof(m));memset(regs,0,sizeof(regs));m.ns=1000000ULL;m.interrupts=true;
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<2048;i++)cpu_ptes[k][i]=linear_ptes[k][i]=(k?0x48000000U:0x46000000U)+i*4096U+0x42U;
}
static void reply(unsigned response,unsigned arg0)
{regs[MFC][0x44/4]=response;regs[MFC][0x48/4]=arg0;regs[MFC][0x4c/4]=regs[MFC][0x50/4]=regs[MFC][0x54/4]=0;
 regs[MFC][8/4]=1;regs[MFC][0x58/4]=0x211116;}
static void finish(i9100_mfc_native **n)
{
 int e=i9100_mfc_native_close(n);if(e)fprintf(stderr,"MFC close=%d maps=%u dma=%u supply=%u reset=%x mc=%x\n",e,m.maps,m.dma,m.supply_owned,regs[MFC][0],regs[MFC][0x510/4]);
 assert(!e&&!*n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.supply_owned&&!parent.holds);
}
int main()
{
 setup();i9100_mfc_native *n=nullptr,*other=nullptr;
 assert(i9100_mfc_native_open(0,nullptr,nullptr,&n)==I9100_MFC_DISABLED&&!m.allocations);
 assert(!i9100_mfc_native_open(1,&parent,&supply,&n));assert(m.maps==6&&m.dma==2&&parent.holds==1);
 assert(i9100_mfc_native_open(1,&parent,&supply,&other)==I9100_MFC_BUSY&&!other);
 auto p=profile();assert(!i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),stream,sizeof(stream)));
 assert(!memcmp(bank_a,firmware,sizeof(firmware))&&regs[MFC][0]==0x3ffU);
 assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);reply(9,0);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY&&m.h2r_command==3U);
 assert(regs[MFC][0x34/4]==0x80000U);reply(8,0x60000);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY&&m.h2r_command==1U);
 assert(regs[MFC][0x40/4]==614400U);reply(1,2);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY&&m.ch_command==0x10002U);
 regs[MFC][0x2000/4]=2;regs[MFC][0x2008/4]=640;regs[MFC][0x2004/4]=480;regs[MFC][0x200c/4]=4;regs[MFC][0x2018/4]=8;
 reply(4,0);assert(!i9100_mfc_native_poll(n));struct i9100_mfc_native_status s={};assert(!i9100_mfc_native_status(n,&s));
 assert(s.phase==I9100_MFC_N_READY&&s.width==640&&s.height==480&&s.dpb_count==4&&s.firmware_version==0x211116&&
  s.bank_a_state==I9100_DMA_DEVICE&&s.bank_b_state==I9100_DMA_DEVICE&&s.held_mask==3U);finish(&n);
 for(unsigned k=1;k<=23;k++){
  setup();m.fail_at=k;int e=i9100_mfc_native_open(1,&parent,&supply,&n);
  if(e)assert(!n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!m.dma&&!m.supply_owned&&!parent.holds);else finish(&n);
 }
 setup();m.acquire_error=m.release_error=1;assert(i9100_mfc_native_open(1,&parent,&supply,&n)==-123&&n&&m.supply_owned);
 assert(i9100_mfc_native_close(&n)==-124&&n);m.release_error=0;finish(&n);
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));p=profile();p.sha256[0]^=1U;
 assert(i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),stream,sizeof(stream))==I9100_MFC_HASH&&!regs[MFC][0]);finish(&n);
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));m.verify_error=1;unsigned writes=m.writes;
 p=profile();assert(i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),nullptr,0)==I9100_MFC_OWNER&&m.writes==writes);
 assert(i9100_mfc_native_close(&n)==I9100_MFC_OWNER&&n&&m.dma==2);m.verify_error=0;finish(&n);
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));p=profile();assert(!i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),nullptr,0));
 m.freeze=m.reset_stuck=1;m.ns+=500000001ULL;regs[MFC][0x510/4]=3;reply(9,0);
 assert(i9100_mfc_native_poll(n)==I9100_MFC_TIMEOUT);assert(!i9100_mfc_native_status(n,&s));
 assert(s.phase==I9100_MFC_N_FAILED&&s.bank_a_state==I9100_DMA_QUARANTINED&&s.held_mask==3);
 assert(i9100_mfc_native_close(&n)!=0&&n&&m.dma==2&&m.maps==6);m.reset_stuck=0;finish(&n);
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));m.release_error=1;
 assert(i9100_mfc_native_close(&n)==-124&&n&&!m.dma&&m.maps==6);m.release_error=0;finish(&n);
 #if !defined(I9100_MFC_ACTUAL_ALLOCATOR)
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));m.sync_fail=m.syncs+1U;
 assert(i9100_mfc_native_close(&n)==I9100_DMA_IO&&n&&m.dma==2);m.sync_fail=0;finish(&n);
 #endif
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));m.parent_put_error=1;
 assert(i9100_mfc_native_close(&n)==-1&&n&&!m.maps&&!m.dma&&parent.holds==1);m.parent_put_error=0;finish(&n);
 setup();assert(!i9100_mfc_native_open(1,&parent,&supply,&n));p=profile();uint8_t invalid[8]={1};
 assert(i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),invalid,sizeof(invalid))==I9100_MFC_ARGUMENT);
 assert(!i9100_mfc_native_status(n,&s)&&s.phase==I9100_MFC_N_FAILED&&!s.held_mask);finish(&n);
 /* Same real native/decoder path, with explicit firmware response injection.
  * CPU aliases are rechecked through actual page-table callback on access. */
 setup();struct i9100_mfc_decode_config cfg={1,8388608,2097152};
 cfg.bank_b_bytes=9U*1024U*1024U;
 assert(i9100_mfc_native_open_decode(1,&parent,&supply,&cfg,&n)==I9100_MFC_ARGUMENT&&!m.allocations);
 cfg.bank_b_bytes=2097152;assert(!i9100_mfc_native_open_decode(1,&parent,&supply,&cfg,&n));
 p=profile();assert(i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),nullptr,0)==I9100_MFC_ARGUMENT);
 assert(!i9100_mfc_native_start(n,&p,firmware,sizeof(firmware),stream,sizeof(stream)));
 reply(9,0);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);reply(8,0x60000);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);
 reply(1,2);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY);
 regs[MFC][0x2000/4]=2;regs[MFC][0x2008/4]=640;regs[MFC][0x2004/4]=480;regs[MFC][0x200c/4]=4;regs[MFC][0x2018/4]=sizeof(stream);
 reply(4,0);assert(i9100_mfc_native_poll(n)==I9100_MFC_BUSY&&m.ch_command==0x40002U&&m.nc_checks);
 regs[MFC][0x2000/4]=2;reply(15,0);assert(!i9100_mfc_native_poll(n));assert(!i9100_mfc_native_status(n,&s)&&s.decode_enabled);
 writes=m.writes;m.nc_bad=1;assert(i9100_mfc_native_frame(n,stream,sizeof(stream),1,0)==I9100_MFC_OWNER&&m.writes==writes);
 m.nc_bad=0;for(unsigned k=0;k<2;k++)for(unsigned i=0;i<2048;i++)linear_ptes[k][i]=(k?0x48000000U:0x46000000U)+i*4096U+0x42U;
 assert(!i9100_mfc_native_frame(n,stream,sizeof(stream),1,0));struct i9100_mfc_frame_lease lease={};
 assert(i9100_mfc_native_acquire(n,&lease)==I9100_MFC_BUSY&&!lease.owned);
 regs[MFC][0x2000/4]=2;regs[MFC][0x201c/4]=1;regs[MFC][0x202c/4]=0;regs[MFC][0x2020/4]=1;regs[MFC][0x2018/4]=sizeof(stream);
 regs[MFC][0x2010/4]=regs[MFC][0x2024/4]=0x48000000U>>11;
 regs[MFC][0x2014/4]=regs[MFC][0x2028/4]=(0x46000000U>>11)+regs[MFC][0x600/4];
 uint32_t *sh=(uint32_t *)(bank_a+0x116000U);sh[8/4]=1;sh[0xc0/4]=4;reply(5,0);
 assert(!i9100_mfc_native_poll(n));assert(!i9100_mfc_native_acquire(n,&lease)&&lease.frame.tag==1&&lease.frame.width==640);
 static uint8_t y[640*480],c[640*240];assert(!i9100_mfc_native_copy_nv12(n,&lease,y,sizeof(y),640,c,sizeof(c),640));
 assert(i9100_mfc_native_close(&n)==I9100_MFC_BUSY&&n&&m.maps==6&&m.dma==2);
 assert(i9100_mfc_native_copy_nv12(n,&lease,y,sizeof(y),640,c,sizeof(c),640)==I9100_MFC_BUSY);
 struct i9100_mfc_frame_lease clone=lease;assert(i9100_mfc_native_release(n,&clone)==I9100_MFC_OWNER);
 m.verify_error=1;assert(!i9100_mfc_native_release(n,&lease));assert(i9100_mfc_native_close(&n)==I9100_MFC_OWNER&&n);
 m.verify_error=0;finish(&n);
 puts("PASS actual native MFCv5.1/crypto/2 banks with explicit MMIO/DMA/IOKit/provider facades: original mailbox bytes, 128KiB alignment, real header fields, pinning, stale/timeout/failure cleanup and single owner");return 0;
}
