/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "i9100_emmc_native.h"
extern "C" {
int fixture_setup(const char *);void fixture_finish();void fixture_identity(i9100_emmc_writer_identity *);
uint32_t fixture_read(unsigned);void fixture_write(unsigned,uint32_t);uint64_t fixture_time();void fixture_delay(unsigned);
unsigned fixture_writes();unsigned fixture_sector_writes();unsigned fixture_flushes();void fixture_fault(unsigned);void fixture_clear_fault();
}
enum {HOST,CMU,GPIO,GIC,CHIP,MAPS};
static uint32_t regs[MAPS][65536/4];
static struct model {unsigned count,fail_at,contexts,locks,maps,descriptors,put_fail;bool interrupts,interrupt_context;} m;
struct i9100_peripheral_native {unsigned holds;};static i9100_peripheral_native parent;
static bool fail(){return ++m.count==m.fail_at;}
static std::map<IOLock *,std::mutex *> host_locks;
void *IOMalloc(size_t bytes){if(fail())return nullptr;m.contexts++;return calloc(1,bytes);}
void IOFree(void *p,size_t){assert(m.contexts);m.contexts--;free(p);}
IOLock *IOLockAlloc(){if(fail())return nullptr;m.locks++;auto *l=new IOLock{false};host_locks[l]=new std::mutex;return l;}
void IOLockFree(IOLock *l){assert(l&&!l->held&&m.locks);m.locks--;delete host_locks.at(l);host_locks.erase(l);delete l;}
bool IOLockTryLock(IOLock *l){if(!host_locks.at(l)->try_lock())return false;l->held=true;return true;}
void IOLockUnlock(IOLock *l){assert(l->held);l->held=false;host_locks.at(l)->unlock();}
#ifndef I9100_EMMC_STORAGE_MODEL
void IOLockLock(IOLock *l){host_locks.at(l)->lock();assert(!l->held);l->held=true;}
#endif
extern "C" int ml_get_interrupts_enabled(){return m.interrupts;}
extern "C" int ml_at_interrupt_context(){return m.interrupt_context;}
void OSSynchronizeIO(){}void IODelay(unsigned us){fixture_delay(us);}
extern "C" uint64_t mach_absolute_time(){return fixture_time();}
extern "C" void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t pa,size_t n,unsigned dir)
{if(fail())return nullptr;assert(dir==kIODirectionInOut);auto *d=new IOMemoryDescriptor;d->physical=pa;d->bytes=n;
#ifdef I9100_EMMC_STORAGE_MODEL
 d->allocated=true;
#endif
 m.descriptors++;return d;}
IOMemoryMap *IOMemoryDescriptor::map(unsigned flags)
{
 if(fail())return nullptr;assert(flags==kIOMapInhibitCache);
 const uint32_t address[MAPS]={0x12550000U,0x10030000U,0x11000000U,0x10490000U,0x10000000U};
 for(unsigned k=0;k<MAPS;k++)if(physical==address[k]){assert(bytes==(k==CMU?65536U:4096U));
  auto *v=new IOMemoryMap;v->address=(uintptr_t)regs[k];m.maps++;return v;}
 assert(false);return nullptr;
}
#ifndef I9100_EMMC_STORAGE_MODEL
void IOMemoryDescriptor::release(){assert(m.descriptors);m.descriptors--;delete this;}
#endif
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(m.maps);m.maps--;delete this;}
extern "C" uint32_t __real_i9100_camera_mmio_read(volatile uint8_t *,unsigned);
extern "C" void __real_i9100_camera_mmio_write(volatile uint8_t *,unsigned,uint32_t);
extern "C" uint32_t __wrap_i9100_camera_mmio_read(volatile uint8_t *base,unsigned off)
{return base==(volatile uint8_t *)regs[HOST]&&off!=0x9c?fixture_read(off):__real_i9100_camera_mmio_read(base,off);}
extern "C" void __wrap_i9100_camera_mmio_write(volatile uint8_t *base,unsigned off,uint32_t value)
{assert(base==(volatile uint8_t *)regs[HOST]);fixture_write(off,value);}
int i9100_peripheral_native_hold_get(i9100_peripheral_native *p,i9100_native_parent_hold *h)
{assert(!h->owned);p->holds++;h->context=p;h->self=(uintptr_t)h;h->owned=1;return 0;}
int i9100_peripheral_native_hold_put(i9100_native_parent_hold *h)
{assert(h->owned&&h->self==(uintptr_t)h&&h->context->holds);if(m.put_fail)return -1;h->context->holds--;h->owned=0;return 0;}
static i9100_emmc_inherited_profile profile={1,100000000U,6U<<16,0x500U,0x10001U,0x3133U,0U,0xaaU};
static void setup(const char *source)
{
 assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!parent.holds);memset(&m,0,sizeof(m));m.interrupts=true;
 memset(regs,0,sizeof(regs));regs[CMU][0xc940/4]=1U<<9;regs[CMU][0xc340/4]=1U<<16;
 regs[CMU][0xc240/4]=profile.source_mux;regs[CMU][0xc54c/4]=profile.source_divider;regs[HOST][0x9c/4]=profile.clksel;
 regs[GPIO][0x40/4]=profile.gpk0_mux;regs[GPIO][0x48/4]=profile.gpk0_pull;regs[GPIO][0x4c/4]=profile.gpk0_drive;regs[GPIO][0x44/4]=4U;
 regs[CHIP][0]=0x43210011U;assert(!fixture_setup(source));
}
static void finish(i9100_emmc_native **n)
{int e=i9100_emmc_native_close(n);if(e)fprintf(stderr,"close=%d maps=%u contexts=%u locks=%u\n",e,m.maps,m.contexts,m.locks);
 assert(!e&&!*n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!parent.holds);fixture_finish();}
int main()
{
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);i9100_emmc_native *n=nullptr,*other=nullptr;
 setup(source);assert(i9100_emmc_native_open(0,nullptr,nullptr,I9100_EMMC_N_INSPECT,nullptr,&n)==I9100_EMMC_UNSUPPORTED&&!m.count);
 assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&n));
 struct i9100_emmc_native_snapshot snap={};assert(!i9100_emmc_native_snapshot(n,&snap));
 assert(snap.identity.sectors==30777344U&&snap.system_hfs_valid&&snap.data_hfs_valid&&snap.data_hfs.bytes==6442450944ULL);
 i9100_emmc_writer_identity approved=snap.identity;
 assert(i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&other)==I9100_EMMC_BUSY&&!other);
 i9100_emmc_host_lease old={};assert(i9100_emmc_host_acquire(&old)==I9100_EMMC_BUSY);
 unsigned writes=fixture_writes();assert(i9100_emmc_native_write_data(n,3489792ULL,1,(const uint8_t *)&approved)==I9100_EMMC_PARTITION&&fixture_writes()==writes);
 assert(!i9100_emmc_native_hold_get(n,42U));assert(i9100_emmc_native_close(&n)==I9100_EMMC_BUSY&&n);
 assert(i9100_emmc_native_hold_put(n,43U)==I9100_EMMC_OWNER);assert(!i9100_emmc_native_hold_put(n,42U));finish(&n);
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n));
 uint8_t bytes[16384],readback[16384];for(unsigned k=0;k<sizeof(bytes);k++)bytes[k]=(uint8_t)(k*19U+7U);
 const uint64_t high=3489792ULL+8388608ULL;
 assert(!i9100_emmc_native_write_data(n,high,32U,bytes));assert(!i9100_emmc_native_read(n,high,32U,readback));assert(!memcmp(bytes,readback,sizeof(bytes)));
 assert(i9100_emmc_native_write_data(n,344064ULL,1U,bytes)==I9100_EMMC_PARTITION);
 assert(!i9100_emmc_native_synchronize(n)&&fixture_flushes()==1U);finish(&n);
 for(unsigned k=1;k<=14;k++){
  setup(source);m.fail_at=k;int e=i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n);
  if(e){assert(!n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!parent.holds);fixture_finish();}else finish(&n);
 }
 setup(source);auto wrong=approved;wrong.cid[0]^=1U;
 assert(i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&wrong,&n)==I9100_EMMC_OWNER&&!n);fixture_finish();
 setup(source);regs[CMU][0xc54c/4]^=1U;
 assert(i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n)==I9100_EMMC_CLOCK&&!n&&!fixture_writes());fixture_finish();
 setup(source);regs[GIC][0x10c/4]|=1U<<13;
 assert(i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&n)==I9100_EMMC_OWNER&&!n);fixture_finish();
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n));
 regs[CMU][0xc240/4]^=1U;assert(!i9100_emmc_native_write_data(n,high,1U,bytes));finish(&n);
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n));
 std::atomic<bool> go{false};std::atomic<unsigned> successful{0};
 std::thread reader([&]{uint8_t response[512];while(!go.load())std::this_thread::yield();
  for(unsigned k=0;k<64U;k++){assert(!i9100_emmc_native_read(n,1U,1U,response));successful.fetch_add(1U);}});
 std::thread writing([&]{while(!go.load())std::this_thread::yield();
  for(unsigned k=0;k<64U;k++){assert(!i9100_emmc_native_write_data(n,high,1U,bytes));successful.fetch_add(1U);}});
 std::thread consumer([&]{while(!go.load())std::this_thread::yield();
  for(unsigned k=0;k<64U;k++){assert(!i9100_emmc_native_hold_get(n,73U));assert(!i9100_emmc_native_hold_put(n,73U));successful.fetch_add(1U);}});
 go.store(true);reader.join();writing.join();consumer.join();assert(successful.load()==192U);finish(&n);
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n));m.put_fail=1;
 assert(i9100_emmc_native_close(&n)==-1&&n&&!m.maps&&parent.holds==1);m.put_fail=0;finish(&n);
 setup(source);m.interrupt_context=true;assert(i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&n)==I9100_EMMC_ARGUMENT&&!m.count);fixture_finish();
 /* Hardware/protocol failure is never retried by this owner. The test
  * intentionally retains a poisoned context, like a real offline audit;
  * no fake successful cleanup or card reprobe is inserted to erase it. */
 setup(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&approved,&n));fixture_fault(1);
 assert(i9100_emmc_native_write_data(n,high,1U,bytes)==I9100_EMMC_READBACK);unsigned commands=fixture_sector_writes();
 assert(i9100_emmc_native_write_data(n,high,1U,bytes)==I9100_EMMC_READBACK&&fixture_sector_writes()==commands);
 assert(i9100_emmc_native_close(&n)==I9100_EMMC_READBACK&&n&&m.maps==5&&parent.holds==1);
 assert(!i9100_emmc_native_snapshot(n,&snap)&&snap.writer_dirty&&snap.writer_poisoned&&snap.error==I9100_EMMC_READBACK);
 /* This deliberately retained hardware-fault context is reachable from
  * static state so ASan does not mistake safety retention for an orphan. */
 static i9100_emmc_native *retained_fault;retained_fault=n;
 static std::mutex *retained_model_mutex[2];unsigned model_lock=0;
 for(auto &entry:host_locks){assert(model_lock<2U);retained_model_mutex[model_lock++]=entry.second;}
 assert(model_lock==2U&&retained_model_mutex[0]);
 assert(retained_fault);fixture_finish();
 puts("PASS actual native eMMC + true MMC/read/write/GPT/PIT/HFS modules with explicit register/media/IOKit facades: 5maps/singlehost/currentCID/6GiB>4GiBwrite/readback/flush/consumers/failure-retention");return 0;
}
