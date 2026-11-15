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
enum {MAP_GPIO0,MAP_GPIO1,MAP_GPZ,MAP_CMU,MAP_GIC,MAP_I2C1,MAP_I2C6,MAP_I2C7,MAP_COUNT};
struct native_i2c {
 struct i9100_peripheral_native *context;unsigned index,live;
 IOLock *outer,*controller;
 i9100_i2c_samsung bus;i9100_gpio_lease pins;i9100_clock_lease gate;
};
struct i9100_peripheral_native {
 IOMemoryDescriptor *memory[MAP_COUNT];IOMemoryMap *mapping[MAP_COUNT];
 volatile uint8_t *regs[MAP_COUNT];
 IOLock *gpio_lock,*clock_lock;IOSimpleLock *irq_lock;
 i9100_gpio_broker gpio;i9100_clock_gates clocks;i9100_irq_registry irqs;
 native_i2c buses[3];
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
static int i2c_transfer(void *cookie,const i9100_i2c_message *messages,unsigned count,unsigned *completed)
{native_i2c *b=(native_i2c *)cookie;return i9100_i2c_samsung_transfer(&b->bus,messages,count,completed);}
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
 const uint32_t addresses[MAP_COUNT]={0x11400000U,0x11000000U,0x03860000U,0x10030000U,0x10490000U,0x13870000U,0x138c0000U,0x138d0000U};
 for(unsigned n=0;n<MAP_COUNT;n++)if(!map(c,n,addresses[n],n==MAP_CMU?65536U:4096U)){dispose(c);return I9100_GPIO_BUSY;}
 i9100_gpio_io gpio={c,gpio_read,gpio_write,gpio_lock,gpio_unlock};
 i9100_clock_io clocks={c,clock_read,clock_write,clock_lock,clock_unlock};
 i9100_irq_io irqs={c,irq_read,irq_write,irq_enter,irq_leave};uint32_t allowed[8];i9100_u1_irq_whitelist(allowed);
 if(i9100_gpio_broker_init(&c->gpio,&gpio,i9100_u1_banks,I9100_BANK_COUNT)||
    i9100_clock_init(&c->clocks,&clocks,i9100_u1_gates,I9100_GATE_COUNT)||
    i9100_irq_registry_init(&c->irqs,&irqs,allowed)){dispose(c);return I9100_GPIO_ARGUMENT;}
 for(unsigned n=0;n<3;n++){
  native_i2c *b=&c->buses[n];b->context=c;b->index=n;b->outer=IOLockAlloc();b->controller=IOLockAlloc();
  if(!b->outer||!b->controller){dispose(c);return I9100_GPIO_BUSY;}
 }
 if(i9100_gpio_broker_enable(&c->gpio,1)||i9100_clock_enable(&c->clocks,1)||i9100_irq_registry_enable(&c->irqs,1))
  {dispose(c);return I9100_GPIO_BUSY;}
 *result=c;return 0;
}
int i9100_peripheral_native_destroy(i9100_peripheral_native **owner)
{
 if(!owner||!*owner)return I9100_GPIO_ARGUMENT;i9100_peripheral_native *c=*owner;
 for(unsigned n=0;n<3;n++)if(c->buses[n].live)return I9100_GPIO_BUSY;
 int e=i9100_irq_registry_enable(&c->irqs,0);if(e)return e;
 e=i9100_gpio_broker_enable(&c->gpio,0);if(!e)e=i9100_clock_enable(&c->clocks,0);
 if(e){(void)i9100_irq_registry_enable(&c->irqs,1);(void)i9100_gpio_broker_enable(&c->gpio,1);return e;}
 dispose(c);*owner=0;return 0;
}
struct i9100_gpio_broker *i9100_peripheral_native_gpio(i9100_peripheral_native *c){return c?&c->gpio:0;}
struct i9100_clock_gates *i9100_peripheral_native_clocks(i9100_peripheral_native *c){return c?&c->clocks:0;}
struct i9100_irq_registry *i9100_peripheral_native_irqs(i9100_peripheral_native *c){return c?&c->irqs:0;}
int i9100_peripheral_native_i2c_close(i9100_peripheral_native *c,unsigned index)
{
 if(!c||index>=3U)return I9100_I2C_ARGUMENT;native_i2c *b=&c->buses[index];
 if(!b->live)return I9100_I2C_ARGUMENT;if(window_lock(b))return I9100_I2C_BUSY;
 int e=0;
 if(b->bus.enabled)e=i9100_i2c_samsung_enable(&b->bus,0,1);
 if(!e&&b->pins.active)e=i9100_gpio_release(&b->pins,I9100_GPIO_RESTORE);
 if(!e&&b->gate.active)e=i9100_clock_release(&b->gate);
 if(!e){b->live=0;memset(&b->bus,0,sizeof(b->bus));}
 window_unlock(b);return e;
}
int i9100_peripheral_native_i2c_open(i9100_peripheral_native *c,unsigned index,uint32_t pclk,i9100_peripheral_i2c *output)
{
 if(!c||index>=3U||!output)return I9100_I2C_ARGUMENT;memset(output,0,sizeof(*output));
 native_i2c *b=&c->buses[index];if(window_lock(b))return I9100_I2C_BUSY;
 int e=0,started=0;
 const i9100_i2c_board_bus *profile=&i9100_u1_i2c[index];
 i9100_i2c_samsung_io io={b,i2c_read,i2c_write,i2c_lock,i2c_unlock,time_ns,pause_us};
 if(b->live){e=I9100_I2C_BUSY;goto done;}
 /* Polling IRQPEND requires IRQEN in the I2C block, but GIC remains masked.
  * Refuse a source already enabled by another driver; don't steal its IRQ. */
 if(read_mmio(c,MAP_GIC,0x100U+(profile->irq/32U)*4U)&(1U<<(profile->irq%32U)))
  {e=I9100_I2C_BUSY;goto done;}
 e=i9100_i2c_samsung_init(&b->bus,&io,pclk,100000);if(e)goto done;
 b->live=1;started=1;e=i9100_clock_acquire(&c->clocks,profile->gate,&b->gate);
 if(!e)e=i9100_gpio_claim(&c->gpio,profile->bank,(1U<<profile->sda)|(1U<<profile->scl),&b->pins);
 if(!e)e=i9100_gpio_pull(&b->pins,profile->sda,3);
 if(!e)e=i9100_gpio_pull(&b->pins,profile->scl,3);
 if(!e)e=i9100_gpio_function(&b->pins,profile->sda,profile->function);
 if(!e)e=i9100_gpio_function(&b->pins,profile->scl,profile->function);
 if(!e)e=i9100_i2c_samsung_enable(&b->bus,1,1);
done:
 window_unlock(b);
 if(e&&started){int close=i9100_peripheral_native_i2c_close(c,index);if(close){output->cookie=b;return e;}}
 if(!e){output->cookie=b;output->transfer=i2c_transfer;output->claim=window_lock;output->release=window_unlock;output->now_ns=time_ns;output->delay_us=pause_us;}
 return e;
}
