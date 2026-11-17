/* SPDX-License-Identifier: BSD-2-Clause; shared real native/controller fixture. */
#ifndef I9100_NATIVE_I2C_MODEL_H
#define I9100_NATIVE_I2C_MODEL_H
#include "i9100_native_parent_model.h"
#include "i9100_i2c_register_model.h"
namespace i2c_card = i9100_i2c_card_model;
static i9100_peripheral_native *i2c_parent;
static i9100_i2c_samsung_io original_i2c_io[3];
static unsigned wrapped_i2c_inits;
static unsigned register_index(void *cookie)
{
 native_i2c *b=(native_i2c *)cookie;
 assert(i2c_parent&&b->index<3&&b==&i2c_parent->buses[b->index]);
 return b->index;
}
static uint32_t card_i2c_read(void *cookie,unsigned off)
{
 unsigned index=register_index(cookie);assert(i2c_parent->buses[index].controller->held&&i2c_parent->buses[index].outer->held);
 return i2c_card::read(&i2c_card::hosts[index],off);
}
static void card_i2c_write(void *cookie,unsigned off,uint32_t value)
{
 unsigned index=register_index(cookie);assert(i2c_parent->buses[index].controller->held&&i2c_parent->buses[index].outer->held);
 i2c_card::write(&i2c_card::hosts[index],off,value);
}
static int card_i2c_lock(void *cookie)
{
 unsigned index=register_index(cookie);assert(!i2c_card::hosts[index].locked);
 int e=original_i2c_io[index].lock(cookie);if(!e)i2c_card::hosts[index].locked=1;return e;
}
static void card_i2c_unlock(void *cookie)
{
 unsigned index=register_index(cookie);assert(i2c_card::hosts[index].locked);
 i2c_card::hosts[index].locked=0;original_i2c_io[index].unlock(cookie);
}
extern "C" int __real_i9100_i2c_samsung_init(i9100_i2c_samsung *,const i9100_i2c_samsung_io *,uint32_t,uint32_t);
extern "C" int __wrap_i9100_i2c_samsung_init(i9100_i2c_samsung *b,const i9100_i2c_samsung_io *io,uint32_t pclk,uint32_t hz)
{
 unsigned index=register_index(io->cookie);assert(b==&i2c_parent->buses[index].bus);
 assert(i2c_parent->buses[index].outer->held&&!i2c_parent->buses[index].controller->held);
 assert(io->read32==i2c_read&&io->write32==i2c_write&&io->lock==i2c_lock&&io->unlock==i2c_unlock);
 assert(io->now_ns==time_ns&&io->delay_us==pause_us);original_i2c_io[index]=*io;wrapped_i2c_inits++;
 i9100_i2c_samsung_io boundary=*io;boundary.read32=card_i2c_read;boundary.write32=card_i2c_write;
 boundary.lock=card_i2c_lock;boundary.unlock=card_i2c_unlock;
 return __real_i9100_i2c_samsung_init(b,&boundary,pclk,hz);
}
static __attribute__((unused)) i9100_peripheral_native *fresh_i2c_parent()
{
 setup();i2c_card::reset();wrapped_i2c_inits=0;i2c_parent=parent();
 memory[0x10030000U][0xc950/4]=1U<<24;return i2c_parent;
}
static __attribute__((unused)) void i2c_restored(i9100_peripheral_native *c)
{
 for(unsigned n=0;n<3;n++){
  assert(!c->buses[n].live&&!c->buses[n].closing&&!c->buses[n].owner&&!c->buses[n].borrowers);
  assert(!c->buses[n].pins.active&&!c->buses[n].gate.active);
  assert(i2c_card::hosts[n].regs[0]==0x82&&i2c_card::hosts[n].regs[1]==0x80);
  assert(i2c_card::hosts[n].regs[2]==0x5a&&i2c_card::hosts[n].regs[4]==3&&!i2c_card::hosts[n].locked);
 }
 assert(memory[0x10030000U][0xc950/4]==(1U<<24));
 assert(!i9100_peripheral_native_destroy(&c)&&!c&&!active_allocations);i2c_parent=0;
}
#endif
