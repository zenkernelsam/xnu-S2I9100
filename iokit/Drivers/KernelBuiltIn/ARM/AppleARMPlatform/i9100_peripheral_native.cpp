/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_peripheral_native.h"
#include "i9100_i2c_samsung.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <machine/machine_routines.h>
#include <string.h>
#undef absolutetime_to_nanoseconds /* C++ AbsoluteTime wrapper is a struct; our callback uses raw uint64. */
extern "C" {
#include <mach/mach_time.h>
void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
}
enum {MAP_GPIO0,MAP_GPIO1,MAP_GPZ,MAP_CMU,MAP_GIC,MAP_I2C1,MAP_I2C6,MAP_I2C7,MAP_UART0,MAP_SDIO3,MAP_COUNT};
struct native_i2c {
 struct i9100_peripheral_native *context;unsigned index,live;
 i9100_native_transport_lease *owner;uint32_t generation,closing,borrowers;
 IOLock *outer,*controller;
 i9100_i2c_samsung bus;i9100_gpio_lease pins;i9100_clock_lease gate;
};
struct native_uart {
 struct i9100_peripheral_native *context;unsigned live;
 i9100_native_transport_lease *owner,*allowed_owner;uint32_t generation,closing,borrowers;
 IOLock *outer,*controller;i9100_uart_bluetooth port;i9100_gpio_lease pins;i9100_clock_lease gate;
};
struct native_sdio {
 struct i9100_peripheral_native *context;unsigned live;
 i9100_native_transport_lease *owner,*allowed_owner;uint32_t generation,closing,borrowers;
 IOLock *outer,*controller;i9100_sdio card;i9100_gpio_lease pins;
 i9100_clock_lease ip_gate,source_gate;
};
struct i9100_peripheral_native {
 IOMemoryDescriptor *memory[MAP_COUNT];IOMemoryMap *mapping[MAP_COUNT];
 volatile uint8_t *regs[MAP_COUNT];
 IOLock *gpio_lock,*clock_lock;IOSimpleLock *irq_lock;
 i9100_gpio_broker gpio;i9100_clock_gates clocks;i9100_irq_registry irqs;
 i9100_irq_cascade eint;
 native_i2c buses[3];
 native_uart uart;
 native_sdio sdio;
};
static int thread_lock(IOLock *lock)
{if(!ml_get_interrupts_enabled())return -1;return IOLockTryLock(lock)?0:-1;}
static uint32_t read_mmio(i9100_peripheral_native *c,unsigned map,unsigned offset)
{OSSynchronizeIO();uint32_t value=*(volatile uint32_t *)(c->regs[map]+offset);OSSynchronizeIO();return value;}
static void write_mmio(i9100_peripheral_native *c,unsigned map,unsigned offset,uint32_t value)
{OSSynchronizeIO();*(volatile uint32_t *)(c->regs[map]+offset)=value;OSSynchronizeIO();}
static uint32_t gpio_read(void *cookie,unsigned region,unsigned off)
{return read_mmio((i9100_peripheral_native *)cookie,region,off);}
static void gpio_write(void *cookie,unsigned region,unsigned off,uint32_t value)
{write_mmio((i9100_peripheral_native *)cookie,region,off,value);}
static int gpio_lock(void *cookie){return thread_lock(((i9100_peripheral_native *)cookie)->gpio_lock);}
static void gpio_unlock(void *cookie){IOLockUnlock(((i9100_peripheral_native *)cookie)->gpio_lock);}
static uint32_t clock_read(void *cookie,unsigned off){return read_mmio((i9100_peripheral_native *)cookie,MAP_CMU,off);}
static void clock_write(void *cookie,unsigned off,uint32_t value){write_mmio((i9100_peripheral_native *)cookie,MAP_CMU,off,value);}
static int clock_lock(void *cookie){return thread_lock(((i9100_peripheral_native *)cookie)->clock_lock);}
static void clock_unlock(void *cookie){IOLockUnlock(((i9100_peripheral_native *)cookie)->clock_lock);}
static uint32_t irq_read(void *cookie,unsigned off){return read_mmio((i9100_peripheral_native *)cookie,MAP_GIC,off);}
static void irq_write(void *cookie,unsigned off,uint32_t value){write_mmio((i9100_peripheral_native *)cookie,MAP_GIC,off,value);}
static uint32_t irq_enter(void *cookie)
{return (uint32_t)IOSimpleLockLockDisableInterrupt(((i9100_peripheral_native *)cookie)->irq_lock);}
static void irq_leave(void *cookie,uint32_t saved)
{IOSimpleLockUnlockEnableInterrupt(((i9100_peripheral_native *)cookie)->irq_lock,(IOInterruptState)saved);}
static uint32_t eint_read(void *cookie,unsigned region,unsigned off)
{return region?0:read_mmio((i9100_peripheral_native *)cookie,MAP_GPIO1,off);}
static void eint_write(void *cookie,unsigned region,unsigned off,uint32_t value)
{if(!region)write_mmio((i9100_peripheral_native *)cookie,MAP_GPIO1,off,value);}
static uint64_t time_ns(void *){uint64_t result=0;absolutetime_to_nanoseconds(mach_absolute_time(),&result);return result;}
static int pause_us(void *,uint32_t microseconds)
{if(!ml_get_interrupts_enabled()||microseconds>100000U)return -1;IODelay(microseconds);return 0;}
static uint32_t i2c_read(void *cookie,unsigned off)
{native_i2c *b=(native_i2c *)cookie;return read_mmio(b->context,MAP_I2C1+b->index,off);}
static void i2c_write(void *cookie,unsigned off,uint32_t value)
{native_i2c *b=(native_i2c *)cookie;write_mmio(b->context,MAP_I2C1+b->index,off,value);}
static int i2c_lock(void *cookie){return thread_lock(((native_i2c *)cookie)->controller);}
static void i2c_unlock(void *cookie){IOLockUnlock(((native_i2c *)cookie)->controller);}
static int window_lock(void *cookie){return thread_lock(((native_i2c *)cookie)->outer);}
static void window_unlock(void *cookie){IOLockUnlock(((native_i2c *)cookie)->outer);}
static int legacy_i2c_claim(void *cookie)
{native_i2c *b=(native_i2c *)cookie;int e=window_lock(b);
 if(!e&&(!b->live||b->owner||b->closing)){window_unlock(b);e=-1;}return e;}
static int i2c_transfer(void *cookie,const i9100_i2c_message *messages,unsigned count,unsigned *completed)
{native_i2c *b=(native_i2c *)cookie;
 if(!b->live||b->owner||b->closing){if(completed)*completed=0;return I9100_I2C_BUSY;}
 return i9100_i2c_samsung_transfer(&b->bus,messages,count,completed);}
static uint32_t uart_read32(void *cookie,unsigned off)
{native_uart *u=(native_uart *)cookie;return read_mmio(u->context,MAP_UART0,off);}
static uint8_t uart_read8(void *cookie,unsigned off)
{native_uart *u=(native_uart *)cookie;OSSynchronizeIO();uint8_t value=u->context->regs[MAP_UART0][off];OSSynchronizeIO();return value;}
static void uart_write32(void *cookie,unsigned off,uint32_t value)
{native_uart *u=(native_uart *)cookie;write_mmio(u->context,MAP_UART0,off,value);}
static void uart_write8(void *cookie,unsigned off,uint8_t value)
{native_uart *u=(native_uart *)cookie;OSSynchronizeIO();u->context->regs[MAP_UART0][off]=value;OSSynchronizeIO();}
static int uart_lock(void *cookie){return thread_lock(((native_uart *)cookie)->controller);}
static void uart_unlock(void *cookie){IOLockUnlock(((native_uart *)cookie)->controller);}
static int uart_control_lock(native_uart *u){return thread_lock(u->outer);}
static int h4_lock(void *cookie)
{native_uart *u=(native_uart *)cookie;int e=uart_control_lock(u);if(!e&&(u->owner||u->allowed_owner||u->closing)){IOLockUnlock(u->outer);e=-1;}return e;}
static void h4_unlock(void *cookie){IOLockUnlock(((native_uart *)cookie)->outer);}
static int h4_write(void *cookie,const uint8_t *bytes,unsigned count,unsigned *sent)
{return i9100_uart_bt_write(&((native_uart *)cookie)->port,bytes,count,sent);}
static int h4_quiesce(void *cookie){return i9100_uart_bt_stop(&((native_uart *)cookie)->port);}
static uint32_t sdio_read32(void *cookie,unsigned off)
{native_sdio *s=(native_sdio *)cookie;return read_mmio(s->context,MAP_SDIO3,off);}
static uint16_t sdio_read16(void *cookie,unsigned off)
{native_sdio *s=(native_sdio *)cookie;OSSynchronizeIO();uint16_t v=*(volatile uint16_t *)(s->context->regs[MAP_SDIO3]+off);OSSynchronizeIO();return v;}
static uint8_t sdio_read8(void *cookie,unsigned off)
{native_sdio *s=(native_sdio *)cookie;OSSynchronizeIO();uint8_t v=s->context->regs[MAP_SDIO3][off];OSSynchronizeIO();return v;}
static void sdio_write32(void *cookie,unsigned off,uint32_t value)
{native_sdio *s=(native_sdio *)cookie;write_mmio(s->context,MAP_SDIO3,off,value);}
static void sdio_write16(void *cookie,unsigned off,uint16_t value)
{native_sdio *s=(native_sdio *)cookie;OSSynchronizeIO();*(volatile uint16_t *)(s->context->regs[MAP_SDIO3]+off)=value;OSSynchronizeIO();}
static void sdio_write8(void *cookie,unsigned off,uint8_t value)
{native_sdio *s=(native_sdio *)cookie;OSSynchronizeIO();s->context->regs[MAP_SDIO3][off]=value;OSSynchronizeIO();}
static int sdio_lock(void *cookie){return thread_lock(((native_sdio *)cookie)->controller);}
static void sdio_unlock(void *cookie){IOLockUnlock(((native_sdio *)cookie)->controller);}
static int sdio_control_lock(native_sdio *s){return thread_lock(s->outer);}
static int packet_lock(void *cookie)
{native_sdio *s=(native_sdio *)cookie;int e=sdio_control_lock(s);if(!e&&(s->owner||s->allowed_owner||s->closing)){IOLockUnlock(s->outer);e=-1;}return e;}
static void packet_unlock(void *cookie){IOLockUnlock(((native_sdio *)cookie)->outer);}
static bool lease_valid(i9100_native_transport_lease *l,unsigned kind)
{return l&&l->owned&&l->self==(uintptr_t)l&&l->context&&l->kind==kind;}
static bool i2c_kind(unsigned kind){return kind>=I9100_NATIVE_I2C1&&kind<=I9100_NATIVE_I2C7;}
static int owned_lock(void *cookie,unsigned kind)
{
 i9100_native_transport_lease *l=(i9100_native_transport_lease *)cookie;if(!lease_valid(l,kind))return -1;
 if(kind==I9100_NATIVE_UART){
  native_uart *u=&l->context->uart;int e=uart_control_lock(u);
  if(!e&&(!u->live||u->closing||u->owner!=l||u->generation!=l->generation)){IOLockUnlock(u->outer);e=-1;}return e;
 }
 if(i2c_kind(kind)){
  native_i2c *b=&l->context->buses[kind-I9100_NATIVE_I2C1];int e=window_lock(b);
  if(!e&&(!b->live||b->closing||b->owner!=l||b->generation!=l->generation)){window_unlock(b);e=-1;}return e;
 }
 if(kind!=I9100_NATIVE_SDIO)return -1;
 native_sdio *s=&l->context->sdio;int e=sdio_control_lock(s);
 if(!e&&(!s->live||s->closing||s->owner!=l||s->generation!=l->generation)){IOLockUnlock(s->outer);e=-1;}return e;
}
static int owned_uart_lock(void *cookie){return owned_lock(cookie,I9100_NATIVE_UART);}
static void owned_uart_unlock(void *cookie){IOLockUnlock(((i9100_native_transport_lease *)cookie)->context->uart.outer);}
static int owned_uart_write(void *cookie,const uint8_t *bytes,unsigned n,unsigned *sent)
{i9100_native_transport_lease *l=(i9100_native_transport_lease *)cookie;return i9100_uart_bt_write(&l->context->uart.port,bytes,n,sent);}
static int owned_uart_quiesce(void *cookie)
{return i9100_uart_bt_stop(&((i9100_native_transport_lease *)cookie)->context->uart.port);}
static int owned_sdio_lock(void *cookie){return owned_lock(cookie,I9100_NATIVE_SDIO);}
static void owned_sdio_unlock(void *cookie){IOLockUnlock(((i9100_native_transport_lease *)cookie)->context->sdio.outer);}
static bool new_lease(i9100_native_transport_lease *l)
{return l&&!l->owned&&(!l->self||l->self==(uintptr_t)l);}
static bool ranges_overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn)
{return a>~(uintptr_t)0-an||b>~(uintptr_t)0-bn||(a+an>b&&b+bn>a);}
static bool lease_outputs_valid(i9100_peripheral_native *c,i9100_native_transport_lease *l,const void *out,size_t size)
{return c&&l&&out&&!ranges_overlap((uintptr_t)l,sizeof(*l),(uintptr_t)c,sizeof(*c))&&
 !ranges_overlap((uintptr_t)l,sizeof(*l),(uintptr_t)out,size)&&
 !ranges_overlap((uintptr_t)c,sizeof(*c),(uintptr_t)out,size);}
static int owned_i2c_claim(void *cookie)
{i9100_native_transport_lease *l=(i9100_native_transport_lease *)cookie;
 return l&&i2c_kind(l->kind)?owned_lock(l,l->kind):-1;}
static void owned_i2c_release(void *cookie)
{i9100_native_transport_lease *l=(i9100_native_transport_lease *)cookie;
 window_unlock(&l->context->buses[l->kind-I9100_NATIVE_I2C1]);}
static int owned_i2c_transfer(void *cookie,const i9100_i2c_message *messages,unsigned count,unsigned *completed)
{
 i9100_native_transport_lease *l=(i9100_native_transport_lease *)cookie;
 if(!l||!completed||!i2c_kind(l->kind)||!lease_valid(l,l->kind))return I9100_I2C_ARGUMENT;
 if(!lease_outputs_valid(l->context,l,completed,sizeof(*completed)))return I9100_I2C_ARGUMENT;
 if(!messages||!count||count>8U||!lease_outputs_valid(l->context,l,messages,count*sizeof(*messages))||
    ranges_overlap((uintptr_t)messages,count*sizeof(*messages),(uintptr_t)completed,sizeof(*completed)))return I9100_I2C_ARGUMENT;
 for(unsigned n=0;n<count;n++)if(!messages[n].bytes||!messages[n].length||
    !lease_outputs_valid(l->context,l,messages[n].bytes,messages[n].length)||
    ranges_overlap((uintptr_t)messages[n].bytes,messages[n].length,(uintptr_t)completed,sizeof(*completed)))return I9100_I2C_ARGUMENT;
 *completed=0;native_i2c *b=&l->context->buses[l->kind-I9100_NATIVE_I2C1];
 if(!b->live||b->closing||b->owner!=l||b->generation!=l->generation)return I9100_I2C_BUSY;
 return i9100_i2c_samsung_transfer(&b->bus,messages,count,completed);
}
static void bind_lease(i9100_native_transport_lease *l,i9100_peripheral_native *c,unsigned kind,uint32_t generation)
{if(l){memset(l,0,sizeof(*l));l->context=c;l->self=(uintptr_t)l;l->kind=kind;l->generation=generation;l->owned=1;}}
static bool map(i9100_peripheral_native *c,unsigned index,uint32_t physical,uint32_t bytes)
{
 c->memory[index]=IOMemoryDescriptor::withPhysicalAddress(physical,bytes,kIODirectionInOut);
 c->mapping[index]=c->memory[index]?c->memory[index]->map(kIOMapInhibitCache):0;
 c->regs[index]=c->mapping[index]?(volatile uint8_t *)c->mapping[index]->getAddress():0;return c->regs[index]!=0;
}
static void dispose(i9100_peripheral_native *c)
{
 for(unsigned n=0;n<MAP_COUNT;n++){if(c->mapping[n])c->mapping[n]->release();if(c->memory[n])c->memory[n]->release();}
 for(unsigned n=0;n<3;n++){if(c->buses[n].outer)IOLockFree(c->buses[n].outer);if(c->buses[n].controller)IOLockFree(c->buses[n].controller);}
 if(c->uart.outer)IOLockFree(c->uart.outer);if(c->uart.controller)IOLockFree(c->uart.controller);
 if(c->sdio.outer)IOLockFree(c->sdio.outer);if(c->sdio.controller)IOLockFree(c->sdio.controller);
 if(c->irq_lock)IOSimpleLockFree(c->irq_lock);if(c->gpio_lock)IOLockFree(c->gpio_lock);if(c->clock_lock)IOLockFree(c->clock_lock);
 IOFree(c,sizeof(*c));
}
int i9100_peripheral_native_create(int enabled,i9100_peripheral_native **result)
{
 if(!result)return I9100_GPIO_ARGUMENT;*result=0;if(enabled!=1)return I9100_GPIO_DISABLED;
 if(!ml_get_interrupts_enabled()||i9100_u1_profile_validate())return I9100_GPIO_ARGUMENT;
 i9100_peripheral_native *c=(i9100_peripheral_native *)IOMalloc(sizeof(*c));if(!c)return I9100_GPIO_BUSY;
 memset(c,0,sizeof(*c));c->gpio_lock=IOLockAlloc();c->clock_lock=IOLockAlloc();c->irq_lock=IOSimpleLockAlloc();
 if(!c->gpio_lock||!c->clock_lock||!c->irq_lock){dispose(c);return I9100_GPIO_BUSY;}
 const uint32_t addresses[MAP_COUNT]={0x11400000U,0x11000000U,0x03860000U,0x10030000U,0x10490000U,0x13870000U,0x138c0000U,0x138d0000U,0x13800000U,0x12540000U};
 for(unsigned n=0;n<MAP_COUNT;n++)if(!map(c,n,addresses[n],n==MAP_CMU?65536U:4096U)){dispose(c);return I9100_GPIO_BUSY;}
 i9100_gpio_io gpio={c,gpio_read,gpio_write,gpio_lock,gpio_unlock};
 i9100_clock_io clocks={c,clock_read,clock_write,clock_lock,clock_unlock};
 i9100_irq_io irqs={c,irq_read,irq_write,irq_enter,irq_leave};uint32_t allowed[8];i9100_u1_irq_whitelist(allowed);
 i9100_cascade_io eint={c,eint_read,eint_write,irq_enter,irq_leave};
 if(i9100_gpio_broker_init(&c->gpio,&gpio,i9100_u1_banks,I9100_BANK_COUNT)||
    i9100_clock_init(&c->clocks,&clocks,i9100_u1_gates,I9100_GATE_COUNT)||
    i9100_irq_registry_init(&c->irqs,&irqs,allowed)||
    i9100_cascade_init(&c->eint,&eint,I9100_CASCADE_EINT,0,(1U<<21)|(1U<<22))){dispose(c);return I9100_GPIO_ARGUMENT;}
 for(unsigned n=0;n<3;n++){
  native_i2c *b=&c->buses[n];b->context=c;b->index=n;b->outer=IOLockAlloc();b->controller=IOLockAlloc();
  if(!b->outer||!b->controller){dispose(c);return I9100_GPIO_BUSY;}
 }
 c->uart.context=c;c->uart.outer=IOLockAlloc();c->uart.controller=IOLockAlloc();
 if(!c->uart.outer||!c->uart.controller){dispose(c);return I9100_GPIO_BUSY;}
 c->sdio.context=c;c->sdio.outer=IOLockAlloc();c->sdio.controller=IOLockAlloc();
 if(!c->sdio.outer||!c->sdio.controller){dispose(c);return I9100_GPIO_BUSY;}
 if(i9100_gpio_broker_enable(&c->gpio,1)||i9100_clock_enable(&c->clocks,1)||i9100_irq_registry_enable(&c->irqs,1)||i9100_cascade_enable(&c->eint,1))
  {dispose(c);return I9100_GPIO_BUSY;}
 *result=c;return 0;
}
int i9100_peripheral_native_destroy(i9100_peripheral_native **owner)
{
 if(!owner||!*owner)return I9100_GPIO_ARGUMENT;i9100_peripheral_native *c=*owner;
 if(c->uart.live||c->uart.allowed_owner)return I9100_GPIO_BUSY;
 if(c->sdio.live||c->sdio.allowed_owner)return I9100_GPIO_BUSY;
 for(unsigned n=0;n<3;n++)if(c->buses[n].live)return I9100_GPIO_BUSY;
 int e=i9100_cascade_enable(&c->eint,0);if(e)return e;
 e=i9100_irq_registry_enable(&c->irqs,0);if(e){(void)i9100_cascade_enable(&c->eint,1);return e;}
 e=i9100_gpio_broker_enable(&c->gpio,0);if(!e)e=i9100_clock_enable(&c->clocks,0);
 if(e){(void)i9100_cascade_enable(&c->eint,1);(void)i9100_irq_registry_enable(&c->irqs,1);(void)i9100_gpio_broker_enable(&c->gpio,1);return e;}
 dispose(c);*owner=0;return 0;
}
struct i9100_gpio_broker *i9100_peripheral_native_gpio(i9100_peripheral_native *c){return c?&c->gpio:0;}
struct i9100_clock_gates *i9100_peripheral_native_clocks(i9100_peripheral_native *c){return c?&c->clocks:0;}
struct i9100_irq_registry *i9100_peripheral_native_irqs(i9100_peripheral_native *c){return c?&c->irqs:0;}
struct i9100_irq_cascade *i9100_peripheral_native_eint(i9100_peripheral_native *c){return c?&c->eint:0;}
static int i2c_close_for(i9100_peripheral_native *c,unsigned index,i9100_native_transport_lease *owner)
{
 if(!c||index>=3U)return I9100_I2C_ARGUMENT;native_i2c *b=&c->buses[index];
 if(!b->live)return I9100_I2C_ARGUMENT;if(window_lock(b))return I9100_I2C_BUSY;
 if(b->owner!=owner||(owner&&b->generation!=owner->generation)||b->borrowers){window_unlock(b);return I9100_I2C_BUSY;}
 b->closing=1;
 int e=0;
 if(b->bus.enabled)e=i9100_i2c_samsung_enable(&b->bus,0,1);
 if(!e&&b->pins.active)e=i9100_gpio_release(&b->pins,I9100_GPIO_RESTORE);
 if(!e&&b->gate.active)e=i9100_clock_release(&b->gate);
 if(!e){b->live=b->closing=0;b->owner=0;if(owner)owner->owned=0;memset(&b->bus,0,sizeof(b->bus));}
 window_unlock(b);return e;
}
int i9100_peripheral_native_i2c_close(i9100_peripheral_native *c,unsigned index)
{return i2c_close_for(c,index,0);}
static int i2c_open_for(i9100_peripheral_native *c,i9100_native_transport_lease *owner,unsigned index,uint32_t pclk,i9100_peripheral_i2c *output)
{
 if(!c||index>=3U||!output)return I9100_I2C_ARGUMENT;memset(output,0,sizeof(*output));
 native_i2c *b=&c->buses[index];if(window_lock(b))return I9100_I2C_BUSY;
 int e=0,started=0;
 const i9100_i2c_board_bus *profile=&i9100_u1_i2c[index];
 i9100_i2c_samsung_io io={b,i2c_read,i2c_write,i2c_lock,i2c_unlock,time_ns,pause_us};
 if(b->live||b->owner||b->generation==0xffffffffU){e=I9100_I2C_BUSY;goto done;}
 /* Polling IRQPEND requires IRQEN in the I2C block, but GIC remains masked.
  * Refuse a source already enabled by another driver; don't steal its IRQ. */
 if(read_mmio(c,MAP_GIC,0x100U+(profile->irq/32U)*4U)&(1U<<(profile->irq%32U)))
  {e=I9100_I2C_BUSY;goto done;}
 e=i9100_i2c_samsung_init(&b->bus,&io,pclk,100000);if(e)goto done;
 b->generation++;b->owner=owner;b->closing=0;bind_lease(owner,c,I9100_NATIVE_I2C1+index,b->generation);
 b->live=1;started=1;e=i9100_clock_acquire(&c->clocks,profile->gate,&b->gate);
 if(!e)e=i9100_gpio_claim(&c->gpio,profile->bank,(1U<<profile->sda)|(1U<<profile->scl),&b->pins);
 if(!e)e=i9100_gpio_pull(&b->pins,profile->sda,3);
 if(!e)e=i9100_gpio_pull(&b->pins,profile->scl,3);
 if(!e)e=i9100_gpio_function(&b->pins,profile->sda,profile->function);
 if(!e)e=i9100_gpio_function(&b->pins,profile->scl,profile->function);
 if(!e)e=i9100_i2c_samsung_enable(&b->bus,1,1);
done:
 window_unlock(b);
 if(e&&started){int close=i2c_close_for(c,index,owner);if(close){output->cookie=owner?(void *)owner:(void *)b;return e;}}
 if(!e){output->cookie=owner?(void *)owner:(void *)b;output->transfer=owner?owned_i2c_transfer:i2c_transfer;
  output->claim=owner?owned_i2c_claim:legacy_i2c_claim;output->release=owner?owned_i2c_release:window_unlock;
  output->now_ns=time_ns;output->delay_us=pause_us;}
 return e;
}
int i9100_peripheral_native_i2c_open(i9100_peripheral_native *c,unsigned index,uint32_t pclk,i9100_peripheral_i2c *output)
{return i2c_open_for(c,0,index,pclk,output);}
int i9100_peripheral_native_i2c_open_owned(i9100_peripheral_native *c,i9100_native_transport_lease *owner,unsigned index,uint32_t pclk,i9100_peripheral_i2c *output)
{return new_lease(owner)&&lease_outputs_valid(c,owner,output,sizeof(*output))?
 i2c_open_for(c,owner,index,pclk,output):I9100_I2C_ARGUMENT;}
int i9100_peripheral_native_i2c_recover_owned(i9100_native_transport_lease *owner)
{
 if(!owner||!i2c_kind(owner->kind)||!lease_valid(owner,owner->kind))return I9100_I2C_ARGUMENT;
 if(owned_i2c_claim(owner))return I9100_I2C_BUSY;
 native_i2c *b=&owner->context->buses[owner->kind-I9100_NATIVE_I2C1];
 const i9100_i2c_board_bus *profile=&i9100_u1_i2c[b->index];
 int e=0;unsigned sda=0,scl=0;
 if(!b->bus.fault&&b->bus.enabled)e=I9100_I2C_ARGUMENT;
 else if(!b->pins.active||!b->gate.active||
         (read_mmio(owner->context,MAP_GIC,0x100U+(profile->irq/32U)*4U)&(1U<<(profile->irq%32U))))e=I9100_I2C_BUSY;
 if(!e)e=i9100_gpio_level(&b->pins,profile->sda,&sda);
 if(!e)e=i9100_gpio_level(&b->pins,profile->scl,&scl);
 if(!e&&(!sda||!scl))e=I9100_I2C_BUS;
 if(!e&&b->bus.enabled)e=i9100_i2c_samsung_enable(&b->bus,0,1);
 if(!e)e=i9100_i2c_samsung_enable(&b->bus,1,1);
 if(e&&e!=I9100_I2C_ARGUMENT)b->bus.fault=1;
 owned_i2c_release(owner);return e;
}
static int uart_close_for(i9100_peripheral_native *c,i9100_native_transport_lease *owner)
{
 if(!c)return I9100_UART_ARGUMENT;native_uart *u=&c->uart;
 if(uart_control_lock(u))return I9100_UART_BUSY;
 if(!u->live){h4_unlock(u);return I9100_UART_DISABLED;}
 if(u->owner!=owner||(owner&&u->generation!=owner->generation)){h4_unlock(u);return I9100_UART_BUSY;}
 if(u->borrowers){h4_unlock(u);return I9100_UART_BUSY;}
 u->closing=1;
 int e=0;if(u->port.prepared)e=i9100_uart_bt_stop(&u->port);
 if(!e&&u->pins.active)e=i9100_gpio_release(&u->pins,I9100_GPIO_RESTORE);
 if(!e&&u->gate.active)e=i9100_clock_release(&u->gate);
 if(!e){u->live=u->closing=0;u->owner=0;if(owner)owner->owned=0;memset(&u->port,0,sizeof(u->port));}
 h4_unlock(u);return e;
}
int i9100_peripheral_native_uart_close(i9100_peripheral_native *c){return uart_close_for(c,0);}
static int uart_open_for(i9100_peripheral_native *c,i9100_native_transport_lease *owner,uint32_t clock,uint32_t baud,enum i9100_uart_fraction fraction,int verified,i9100_bt_io *output)
{
 if(!c||!output||verified!=1)return I9100_UART_ARGUMENT;memset(output,0,sizeof(*output));
 native_uart *u=&c->uart;if(uart_control_lock(u))return I9100_UART_BUSY;
 int e=0,started=0;
 i9100_uart_io io={u,uart_read32,uart_read8,uart_write32,uart_write8,uart_lock,uart_unlock,time_ns,pause_us};
 if(u->live||u->owner||(u->allowed_owner&&u->allowed_owner!=owner)||u->generation==0xffffffffU){e=I9100_UART_BUSY;goto done;}
 e=i9100_uart_bt_init(&u->port,&io);if(e)goto done;
 /* Validate the production baud/fraction calculations before any clock/pin
  * acquisition. The disabled configure path performs no UART IO. */
 e=i9100_uart_bt_configure(&u->port,clock,baud,fraction,verified);
 if(e!=I9100_UART_DISABLED){memset(&u->port,0,sizeof(u->port));goto done;}e=0;
 if(read_mmio(c,MAP_GIC,0x108U)&(1U<<20)){e=I9100_UART_BUSY;memset(&u->port,0,sizeof(u->port));goto done;}
 u->generation++;u->owner=owner;u->closing=0;bind_lease(owner,c,I9100_NATIVE_UART,u->generation);
 u->live=1;started=1;e=i9100_clock_acquire(&c->clocks,I9100_GATE_UART0,&u->gate);
 if(!e)e=i9100_gpio_claim(&c->gpio,I9100_GPA0,15U,&u->pins);
 for(unsigned n=0;n<4U&&!e;n++){e=i9100_gpio_pull(&u->pins,n,0);if(!e)e=i9100_gpio_function(&u->pins,n,2);}
 if(!e)e=i9100_uart_bt_enable(&u->port,1);
 if(!e)e=i9100_uart_bt_configure(&u->port,clock,baud,fraction,verified);
done:
 h4_unlock(u);
 if(e&&started){int close=uart_close_for(c,owner);if(close){output->cookie=owner?(void *)owner:(void *)u;return e;}}
 if(!e){output->cookie=owner?(void *)owner:(void *)u;output->lock=owner?owned_uart_lock:h4_lock;
  output->unlock=owner?owned_uart_unlock:h4_unlock;output->write=owner?owned_uart_write:h4_write;
  output->quiesce=owner?owned_uart_quiesce:h4_quiesce;output->now_ns=time_ns;}
 return e;
}
int i9100_peripheral_native_uart_open(i9100_peripheral_native *c,uint32_t clock,uint32_t baud,enum i9100_uart_fraction fraction,int verified,i9100_bt_io *output)
{return uart_open_for(c,0,clock,baud,fraction,verified,output);}
int i9100_peripheral_native_uart_open_owned(i9100_peripheral_native *c,i9100_native_transport_lease *owner,uint32_t clock,uint32_t baud,enum i9100_uart_fraction fraction,int verified,i9100_bt_io *output)
{return new_lease(owner)&&lease_outputs_valid(c,owner,output,sizeof(*output))?
 uart_open_for(c,owner,clock,baud,fraction,verified,output):I9100_UART_ARGUMENT;}
int i9100_peripheral_native_uart_read(i9100_peripheral_native *c,uint8_t *bytes,unsigned capacity,unsigned *received)
{
 if(received)*received=0;if(!c)return I9100_UART_DISABLED;
 native_uart *u=&c->uart;if(h4_lock(u))return I9100_UART_BUSY;
 if(!u->live){h4_unlock(u);return I9100_UART_DISABLED;}
 int e=i9100_uart_bt_read(&u->port,bytes,capacity,received);h4_unlock(u);return e;
}
int i9100_peripheral_native_uart_read_owned(i9100_native_transport_lease *owner,uint8_t *bytes,unsigned capacity,unsigned *received)
{if(!lease_valid(owner,I9100_NATIVE_UART)||!bytes||!capacity||capacity>4096U||!received||
 !lease_outputs_valid(owner->context,owner,bytes,capacity)||!lease_outputs_valid(owner->context,owner,received,sizeof(*received))||
 ranges_overlap((uintptr_t)bytes,capacity,(uintptr_t)received,sizeof(*received)))return I9100_UART_ARGUMENT;
 *received=0;if(owned_uart_lock(owner))return I9100_UART_BUSY;
 int e=i9100_uart_bt_read(&owner->context->uart.port,bytes,capacity,received);owned_uart_unlock(owner);return e;}
static int sdio_close_for(i9100_peripheral_native *c,i9100_native_transport_lease *owner)
{
 if(!c)return I9100_SDIO_ARGUMENT;native_sdio *s=&c->sdio;
 if(sdio_control_lock(s))return I9100_SDIO_BUSY;
 if(!s->live){packet_unlock(s);return I9100_SDIO_DISABLED;}
 if(s->owner!=owner||(owner&&s->generation!=owner->generation)){packet_unlock(s);return I9100_SDIO_BUSY;}
 if(s->borrowers){packet_unlock(s);return I9100_SDIO_BUSY;}
 s->closing=1;
 int e=0;if(s->card.active)e=i9100_sdio_enable(&s->card,0,1);
 if(!e&&s->card.host_owned)e=i9100_sdio_release_host(&s->card);
 if(!e&&s->pins.active)e=i9100_gpio_release(&s->pins,I9100_GPIO_RESTORE);
 if(!e&&s->source_gate.active)e=i9100_clock_release(&s->source_gate);
 if(!e&&s->ip_gate.active)e=i9100_clock_release(&s->ip_gate);
 if(!e){s->live=s->closing=0;s->owner=0;if(owner)owner->owned=0;memset(&s->card,0,sizeof(s->card));}
 packet_unlock(s);return e;
}
int i9100_peripheral_native_sdio_close(i9100_peripheral_native *c){return sdio_close_for(c,0);}
static int sdio_open_for(i9100_peripheral_native *c,i9100_native_transport_lease *owner,uint32_t clock,int verified,i9100_sdio **card,i9100_f2_io *output)
{
 if(card)*card=0;
 if(!c||!card||!output||verified!=1)return I9100_SDIO_ARGUMENT;memset(output,0,sizeof(*output));
 native_sdio *s=&c->sdio;if(sdio_control_lock(s))return I9100_SDIO_BUSY;
 int e=0,started=0;
 i9100_sdio_io io={s,sdio_read8,sdio_read16,sdio_read32,sdio_write8,sdio_write16,sdio_write32,sdio_lock,sdio_unlock,time_ns,pause_us};
 if(s->live||s->owner||(s->allowed_owner&&s->allowed_owner!=owner)||s->generation==0xffffffffU){e=I9100_SDIO_BUSY;goto done;}
 if(read_mmio(c,MAP_GIC,0x10cU)&(1U<<12)){e=I9100_SDIO_BUSY;goto done;}
 e=i9100_sdio_init(&s->card,&io,clock);if(e)goto done;
 s->generation++;s->owner=owner;s->closing=0;bind_lease(owner,c,I9100_NATIVE_SDIO,s->generation);
 s->live=1;started=1;e=i9100_clock_acquire(&c->clocks,I9100_GATE_HSMMC3,&s->ip_gate);
 if(!e)e=i9100_clock_acquire(&c->clocks,I9100_GATE_SCLK_MMC3,&s->source_gate);
 if(!e)e=i9100_gpio_claim(&c->gpio,I9100_GPK3,0x7bU,&s->pins);
 for(unsigned pin=0;pin<7U&&!e;pin++)if(pin!=2U){
  e=i9100_gpio_pull(&s->pins,pin,0);if(!e)e=i9100_gpio_drive(&s->pins,pin,2);if(!e)e=i9100_gpio_function(&s->pins,pin,2);
 }
 if(!e)e=i9100_sdio_enable(&s->card,1,verified);
 if(!e)e=i9100_sdio_function(&s->card,1,1); /* F2 remains off until authenticated firmware runs. */
done:
 packet_unlock(s);
 if(e&&started){int close=sdio_close_for(c,owner);if(close){*card=&s->card;return e;}}
 if(!e){*card=&s->card;output->cookie=owner?(void *)owner:(void *)s;output->lock=owner?owned_sdio_lock:packet_lock;
  output->unlock=owner?owned_sdio_unlock:packet_unlock;output->now_ns=time_ns;output->delay_us=pause_us;}
 return e;
}
int i9100_peripheral_native_sdio_open(i9100_peripheral_native *c,uint32_t clock,int verified,i9100_sdio **card,i9100_f2_io *output)
{return sdio_open_for(c,0,clock,verified,card,output);}
int i9100_peripheral_native_sdio_open_owned(i9100_peripheral_native *c,i9100_native_transport_lease *owner,uint32_t clock,int verified,i9100_sdio **card,i9100_f2_io *output)
{return new_lease(owner)&&card&&lease_outputs_valid(c,owner,output,sizeof(*output))&&
 lease_outputs_valid(c,owner,card,sizeof(*card))&&!ranges_overlap((uintptr_t)card,sizeof(*card),(uintptr_t)output,sizeof(*output))?
 sdio_open_for(c,owner,clock,verified,card,output):I9100_SDIO_ARGUMENT;}
int i9100_peripheral_native_transport_close(i9100_native_transport_lease *owner)
{
 if(!owner||!owner->owned||owner->self!=(uintptr_t)owner||!owner->context)return I9100_GPIO_OWNER;
 if(i2c_kind(owner->kind))return i2c_close_for(owner->context,owner->kind-I9100_NATIVE_I2C1,owner);
 return owner->kind==I9100_NATIVE_UART?uart_close_for(owner->context,owner):owner->kind==I9100_NATIVE_SDIO?
        sdio_close_for(owner->context,owner):I9100_GPIO_ARGUMENT;
}
static int transport_hold(i9100_native_transport_hold *hold,i9100_native_transport_lease *lease,unsigned operation)
{
 if(!hold||!lease||!lease_valid(lease,lease->kind)||
    (lease->kind!=I9100_NATIVE_UART&&lease->kind!=I9100_NATIVE_SDIO&&!i2c_kind(lease->kind))||
    ranges_overlap((uintptr_t)hold,sizeof(*hold),(uintptr_t)lease,sizeof(*lease))||
    ranges_overlap((uintptr_t)hold,sizeof(*hold),(uintptr_t)lease->context,sizeof(*lease->context)))return I9100_GPIO_ARGUMENT;
 if(operation==0U){if(hold->owned||(hold->self&&hold->self!=(uintptr_t)hold))return I9100_GPIO_OWNER;}
 else if(!hold->owned||hold->self!=(uintptr_t)hold||hold->lease!=lease||hold->kind!=lease->kind||hold->generation!=lease->generation)
  return I9100_GPIO_OWNER;
 i9100_peripheral_native *c=lease->context;native_i2c *bus=i2c_kind(lease->kind)?&c->buses[lease->kind-I9100_NATIVE_I2C1]:0;
 IOLock *lock=bus?bus->outer:(lease->kind==I9100_NATIVE_UART?c->uart.outer:c->sdio.outer);
 if(thread_lock(lock))return I9100_GPIO_BUSY;
 bool uart=lease->kind==I9100_NATIVE_UART;
 uint32_t *count=bus?&bus->borrowers:(uart?&c->uart.borrowers:&c->sdio.borrowers);
 bool matches=bus?(bus->live&&bus->owner==lease&&bus->generation==lease->generation):uart?(c->uart.live&&c->uart.owner==lease&&c->uart.generation==lease->generation):
  (c->sdio.live&&c->sdio.owner==lease&&c->sdio.generation==lease->generation);
 bool ready=bus?(!bus->closing&&bus->bus.enabled&&bus->bus.exclusive&&!bus->bus.fault):uart?(!c->uart.closing&&c->uart.port.prepared&&!c->uart.port.fault):
  (!c->sdio.closing&&c->sdio.card.active&&!c->sdio.card.fault);
 int e=0;
 if(!matches)e=I9100_GPIO_OWNER;
 else if(operation==0U){
  if(!ready||*count==0xffffffffU)e=I9100_GPIO_BUSY;
  else{(*count)++;memset(hold,0,sizeof(*hold));hold->lease=lease;hold->self=(uintptr_t)hold;
   hold->kind=lease->kind;hold->generation=lease->generation;hold->owned=1;}
 }else if(!*count)e=I9100_GPIO_OWNER;
 else if(operation==1U){if(!ready)e=I9100_GPIO_IO;}
 else{(*count)--;hold->owned=0;}
 IOLockUnlock(lock);return e;
}
int i9100_peripheral_native_transport_hold_get(i9100_native_transport_lease *lease,i9100_native_transport_hold *hold)
{return transport_hold(hold,lease,0);}
int i9100_peripheral_native_transport_hold_check(i9100_native_transport_hold *hold)
{return hold?transport_hold(hold,hold->lease,1):I9100_GPIO_ARGUMENT;}
int i9100_peripheral_native_transport_hold_put(i9100_native_transport_hold *hold)
{return hold?transport_hold(hold,hold->lease,2):I9100_GPIO_ARGUMENT;}
int i9100_peripheral_native_transport_idle(i9100_peripheral_native *c,enum i9100_native_transport_kind kind)
{
 if(!c)return I9100_GPIO_ARGUMENT;
 if(kind==I9100_NATIVE_UART){native_uart *u=&c->uart;if(uart_control_lock(u))return I9100_GPIO_BUSY;
  int e=u->live||u->owner?I9100_GPIO_BUSY:0;h4_unlock(u);return e;}
 if(kind==I9100_NATIVE_SDIO){native_sdio *s=&c->sdio;if(sdio_control_lock(s))return I9100_GPIO_BUSY;
  int e=s->live||s->owner?I9100_GPIO_BUSY:0;packet_unlock(s);return e;}
 if(i2c_kind(kind)){native_i2c *b=&c->buses[kind-I9100_NATIVE_I2C1];if(window_lock(b))return I9100_GPIO_BUSY;
  int e=b->live||b->owner?I9100_GPIO_BUSY:0;window_unlock(b);return e;}
 return I9100_GPIO_ARGUMENT;
}
int i9100_peripheral_native_radio_bind(i9100_peripheral_native *c,i9100_native_transport_lease *uart_owner,i9100_native_transport_lease *sdio_owner)
{
 if(!new_lease(uart_owner)||!new_lease(sdio_owner)||!lease_outputs_valid(c,uart_owner,sdio_owner,sizeof(*sdio_owner)))return I9100_GPIO_ARGUMENT;
 if(uart_control_lock(&c->uart))return I9100_GPIO_BUSY;
 if(sdio_control_lock(&c->sdio)){h4_unlock(&c->uart);return I9100_GPIO_BUSY;}
 int e=c->uart.live||c->sdio.live||c->uart.owner||c->sdio.owner||c->uart.allowed_owner||c->sdio.allowed_owner?I9100_GPIO_BUSY:0;
 if(!e){c->uart.allowed_owner=uart_owner;c->sdio.allowed_owner=sdio_owner;}
 packet_unlock(&c->sdio);h4_unlock(&c->uart);return e;
}
int i9100_peripheral_native_radio_unbind(i9100_peripheral_native *c,i9100_native_transport_lease *uart_owner,i9100_native_transport_lease *sdio_owner)
{
 if(!c||!uart_owner||!sdio_owner)return I9100_GPIO_ARGUMENT;
 if(uart_control_lock(&c->uart))return I9100_GPIO_BUSY;
 if(sdio_control_lock(&c->sdio)){h4_unlock(&c->uart);return I9100_GPIO_BUSY;}
 int e=c->uart.allowed_owner!=uart_owner||c->sdio.allowed_owner!=sdio_owner?I9100_GPIO_OWNER:
       c->uart.live||c->sdio.live||c->uart.owner||c->sdio.owner?I9100_GPIO_BUSY:0;
 if(!e){c->uart.allowed_owner=0;c->sdio.allowed_owner=0;}
 packet_unlock(&c->sdio);h4_unlock(&c->uart);return e;
}
