/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <vector>
/* Compile the actual native implementation in this TU: private fields are
 * visible only to fault/generation injection, no production test hook added. */
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_peripheral_native.cpp"
static std::map<uint32_t,std::vector<uint32_t> > memory;
static unsigned allocations,active_allocations,fail_alloc,barriers,drop_sdio_release;
static uint64_t ns;
static bool interrupts=true,sdio_gate_seen;
static bool allocate(){allocations++;if(allocations==fail_alloc)return false;active_allocations++;return true;}
void *IOMalloc(size_t n){return allocate()?calloc(1,n):0;}
void IOFree(void *p,size_t){if(p){assert(active_allocations);active_allocations--;free(p);}}
IOLock *IOLockAlloc(){return allocate()?new IOLock{false}:0;}
void IOLockFree(IOLock *p){if(p){assert(!p->held&&active_allocations);active_allocations--;delete p;}}
bool IOLockTryLock(IOLock *p){assert(p);if(p->held)return false;p->held=true;return true;}
void IOLockUnlock(IOLock *p){assert(p&&p->held);p->held=false;}
IOSimpleLock *IOSimpleLockAlloc(){return allocate()?new IOSimpleLock{false}:0;}
void IOSimpleLockFree(IOSimpleLock *p){if(p){assert(!p->held&&active_allocations);active_allocations--;delete p;}}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *p){assert(p&&!p->held);p->held=true;unsigned saved=interrupts;interrupts=false;return saved;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *p,IOInterruptState saved){assert(p&&p->held);p->held=false;interrupts=saved;}
extern "C" int ml_get_interrupts_enabled(){return interrupts;}
extern "C" int ml_set_interrupts_enabled(int value){int saved=interrupts;interrupts=value!=0;return saved;}
extern "C" uint64_t mach_absolute_time(){return ns;}
extern "C" void absolutetime_to_nanoseconds(uint64_t value,uint64_t *out){*out=value;}
void IODelay(unsigned us){ns+=(uint64_t)us*1000U;}
void OSSynchronizeIO()
{
 barriers++;
 if(memory.count(0x12540000U)){uint8_t *sd=(uint8_t *)memory[0x12540000U].data();sd[47]=0;}
 if(memory.count(0x10030000U)){
  uint32_t *cmu=memory[0x10030000U].data();
  if(cmu[0xc940/4]&(1U<<8))sdio_gate_seen=true;
  if(drop_sdio_release&&sdio_gate_seen&&!(cmu[0xc940/4]&(1U<<8)))cmu[0xc940/4]|=1U<<8;
 }
}
IOMemoryDescriptor *IOMemoryDescriptor::withPhysicalAddress(uint32_t physical,size_t n,unsigned)
{
 if(!allocate())return 0;
 IOMemoryDescriptor *p=new IOMemoryDescriptor;p->physical=physical;p->bytes=n;
 if(!memory.count(physical))memory[physical].resize(n/4U);
 assert(memory[physical].size()*4U>=n);return p;
}
IOMemoryMap *IOMemoryDescriptor::map(unsigned)
{if(!allocate())return 0;IOMemoryMap *p=new IOMemoryMap;p->address=(uintptr_t)memory[physical].data();return p;}
uintptr_t IOMemoryMap::getAddress(){return address;}
void IOMemoryMap::release(){assert(active_allocations);active_allocations--;delete this;}
void IOMemoryDescriptor::release(){assert(active_allocations);active_allocations--;delete this;}
static void setup()
{
 assert(!active_allocations);memory.clear();allocations=fail_alloc=barriers=drop_sdio_release=0;sdio_gate_seen=false;ns=0;interrupts=true;
 memory[0x13800000U].resize(1024);memory[0x13800000U][16/4]=4U;memory[0x13800000U][56/4]=15U;
}
static i9100_peripheral_native *parent()
{i9100_peripheral_native *c=0;assert(!i9100_peripheral_native_create(1,&c)&&c);return c;}
static void uart_ownership()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_bt_io io;
 unsigned before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,0,&io)==I9100_UART_ARGUMENT&&barriers==before&&!l.owned);
 assert(!i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)&&l.owned);
 assert(l.kind==I9100_NATIVE_UART&&l.self==(uintptr_t)&l&&io.cookie==&l);
 i9100_native_transport_lease copy=l;before=barriers;
 assert(i9100_peripheral_native_transport_close(&copy)==I9100_GPIO_OWNER&&barriers==before);
 assert(i9100_peripheral_native_uart_close(c)==I9100_UART_BUSY&&barriers==before);
 uint8_t buffer[8];unsigned got=99;
 assert(i9100_peripheral_native_uart_read(c,buffer,sizeof(buffer),&got)==I9100_UART_BUSY&&!got&&barriers==before);
 assert(i9100_peripheral_native_uart_read_owned(&l,buffer,sizeof(buffer),&got)==0&&!got);
 assert(i9100_peripheral_native_uart_read_owned(&l,(uint8_t *)&l,8,&got)==I9100_UART_ARGUMENT);
 assert(i9100_peripheral_native_transport_idle(c,I9100_NATIVE_UART)==I9100_GPIO_BUSY);
 assert(!io.lock(io.cookie));before=barriers;
 assert(i9100_peripheral_native_transport_close(&l)==I9100_UART_BUSY&&barriers==before&&l.owned);io.unlock(io.cookie);
 uint32_t generation=l.generation;l.generation--;
 assert(io.lock(io.cookie)&&i9100_peripheral_native_transport_close(&l)==I9100_UART_BUSY);l.generation=generation;
 i9100_bt_io old=io;assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);
 before=barriers;assert(old.lock(old.cookie)&&barriers==before);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_UART));
 i9100_native_transport_lease second={};assert(!i9100_peripheral_native_uart_open_owned(c,&second,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(second.generation>generation&&old.lock(old.cookie));
 assert(!i9100_peripheral_native_transport_close(&second));
 c->uart.generation=0xffffffffU;before=barriers;i9100_native_transport_lease exhausted={};
 assert(i9100_peripheral_native_uart_open_owned(c,&exhausted,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&barriers==before&&!exhausted.owned);
 assert(!i9100_peripheral_native_destroy(&c)&&!c&&!active_allocations);
}
static void retained_uart_close()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_bt_io io;
 assert(!i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 memory[0x13800000U][16/4]=0;
 assert(i9100_peripheral_native_transport_close(&l)==I9100_UART_TIMEOUT&&l.owned&&c->uart.closing);
 unsigned before=barriers;assert(io.lock(io.cookie)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 memory[0x13800000U][16/4]=4;
 assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void legacy_compatibility()
{
 setup();i9100_peripheral_native *c=parent();i9100_bt_io io;
 assert(!i9100_peripheral_native_uart_open(c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(!c->uart.owner&&!io.lock(io.cookie));unsigned sent=0;const uint8_t command[4]={1,3,12,0};
 assert(!io.write(io.cookie,command,sizeof(command),&sent)&&sent==sizeof(command));io.unlock(io.cookie);
 uint8_t bytes[8];unsigned received=99;
 assert(!i9100_peripheral_native_uart_read(c,bytes,sizeof(bytes),&received)&&!received);
 i9100_bt_io old=io;assert(!i9100_peripheral_native_uart_close(c));
 i9100_native_transport_lease lease={};
 assert(!i9100_peripheral_native_uart_open_owned(c,&lease,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(old.lock(old.cookie)); /* a former legacy borrower cannot access the new owner */
 assert(!i9100_peripheral_native_transport_close(&lease));
 i9100_sdio *card=0;i9100_f2_io f2;
 assert(i9100_peripheral_native_sdio_open(c,48000000U,1,&card,&f2)==I9100_SDIO_CARD&&!card&&!c->sdio.owner&&!c->sdio.live);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_SDIO));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void sdio_failure_ownership()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease l={};i9100_f2_io io;i9100_sdio *card=0;
 /* Version/capability absent: actual native open enters then safely unwinds,
  * no F1 chip assumed. Force only IP gate restore loss to retain the owner. */
 drop_sdio_release=1;
 assert(i9100_peripheral_native_sdio_open_owned(c,&l,48000000U,1,&card,&io)==I9100_SDIO_CARD);
 assert(l.owned&&card&&c->sdio.closing&&c->sdio.owner==&l);
 unsigned before=barriers;
 assert(i9100_peripheral_native_sdio_close(c)==I9100_SDIO_BUSY&&barriers==before);
 i9100_native_transport_lease other={};i9100_sdio *other_card=0;
 assert(i9100_peripheral_native_sdio_open_owned(c,&other,48000000U,1,&other_card,&io)==I9100_SDIO_BUSY&&!other.owned&&barriers==before);
 assert(owned_sdio_lock(&l)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
 drop_sdio_release=0;
 assert(!i9100_peripheral_native_transport_close(&l)&&!l.owned);
 assert(!i9100_peripheral_native_transport_idle(c,I9100_NATIVE_SDIO));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void allocations_and_aliases()
{
 setup();i9100_peripheral_native *c=0;assert(i9100_peripheral_native_create(0,&c)==I9100_GPIO_DISABLED&&!allocations&&!barriers);
 c=parent();i9100_native_transport_lease l={};i9100_bt_io io;unsigned before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&l,48000000U,115200U,I9100_UART_RAW_FRACTION,1,(i9100_bt_io *)&l)==I9100_UART_ARGUMENT&&barriers==before);
 assert(i9100_peripheral_native_uart_open_owned(c,(i9100_native_transport_lease *)c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_ARGUMENT&&barriers==before);
 assert(!i9100_peripheral_native_destroy(&c));unsigned count=allocations;
 for(unsigned n=1;n<=count;n++){
  setup();fail_alloc=n;
  assert(i9100_peripheral_native_create(1,&c)&&!c&&!active_allocations);
 }
 setup();c=parent();i9100_native_transport_lease fresh={};interrupts=false;before=barriers;
 assert(i9100_peripheral_native_uart_open_owned(c,&fresh,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&!fresh.owned&&barriers==before);
 interrupts=true;assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
static void board_reservation()
{
 setup();i9100_peripheral_native *c=parent();i9100_native_transport_lease uart={},sdio={},other={};i9100_bt_io io;
 unsigned before=barriers;
 assert(!i9100_peripheral_native_radio_bind(c,&uart,&sdio)&&barriers==before);
 assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c&&barriers==before);
 assert(i9100_peripheral_native_uart_open(c,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&barriers==before);
 assert(i9100_peripheral_native_uart_open_owned(c,&other,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io)==I9100_UART_BUSY&&!other.owned&&barriers==before);
 assert(i9100_peripheral_native_radio_bind(c,&uart,&sdio)==I9100_GPIO_BUSY&&barriers==before);
 assert(!i9100_peripheral_native_uart_open_owned(c,&uart,48000000U,115200U,I9100_UART_RAW_FRACTION,1,&io));
 assert(i9100_peripheral_native_radio_unbind(c,&uart,&sdio)==I9100_GPIO_BUSY);
 assert(!i9100_peripheral_native_transport_close(&uart));
 assert(i9100_peripheral_native_radio_unbind(c,&other,&sdio)==I9100_GPIO_OWNER);
 assert(!i9100_peripheral_native_radio_unbind(c,&uart,&sdio));
 assert(!i9100_peripheral_native_destroy(&c)&&!active_allocations);
}
int main()
{uart_ownership();retained_uart_close();legacy_compatibility();sdio_failure_ownership();allocations_and_aliases();board_reservation();puts("PASS actual native UART/SDIO board reservation/owner+epoch callbacks/legacy compatibility+exclusion/close-in-flight/failed-close IO gating/stale leases/alias/IRQ context/all parent allocations; MMIO stand-in only, no successful native F1 enumeration claim");return 0;}
