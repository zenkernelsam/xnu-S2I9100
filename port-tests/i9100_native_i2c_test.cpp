/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_native_i2c_model.h"
static int owned_open(i9100_peripheral_native *c,i9100_native_transport_lease *lease,unsigned index,i9100_peripheral_i2c *io)
{return i9100_peripheral_native_i2c_open_owned(c,lease,index,100000000U,io);}
static void transactions_and_holds()
{
 i9100_peripheral_native *c=fresh_i2c_parent();
 for(unsigned index=0;index<3;index++){
  i9100_native_transport_lease lease={};i9100_peripheral_i2c io;assert(!owned_open(c,&lease,index,&io));
  assert(lease.kind==I9100_NATIVE_I2C1+index&&lease.owned&&lease.self==(uintptr_t)&lease);
  assert(c->buses[index].owner==&lease&&c->buses[index].bus.enabled&&!c->buses[index].bus.fault);
  assert(io.cookie==&lease&&io.claim==owned_i2c_claim&&io.release==owned_i2c_release&&io.now_ns==time_ns&&io.delay_us==pause_us);
  assert(c->buses[index].bus.bus_hz<=100000U);
  uint8_t addr=index==0?0x19:index==1?0x3a:0x0c,reg=0x20,data[3]={};
  for(unsigned n=0;n<3;n++)i2c_card::hosts[index].data[addr][0x20+n]=(uint8_t)(41+n);
  i9100_i2c_message messages[2]={{addr,0,1,&reg},{addr,1,3,data}};unsigned completed=99;
  assert(!io.claim(io.cookie));assert(!io.transfer(io.cookie,messages,2,&completed)&&completed==2U);io.release(io.cookie);
  assert(!memcmp(data,"\x29\x2a\x2b",3));
  assert(i2c_card::hosts[index].starts==2&&i2c_card::hosts[index].stops==1);
  assert(i2c_card::hosts[index].acked_reads==2&&i2c_card::hosts[index].nacked_reads==1);
  i9100_native_transport_hold device={},worker={};
  assert(!i9100_peripheral_native_transport_hold_get(&lease,&device)&&!i9100_peripheral_native_transport_hold_get(&lease,&worker));
  unsigned writes=i2c_card::hosts[index].writes;
  assert(i9100_peripheral_native_transport_close(&lease)==I9100_I2C_BUSY&&!c->buses[index].closing);
  assert(i9100_peripheral_native_i2c_close(c,index)==I9100_I2C_BUSY&&writes==i2c_card::hosts[index].writes);
  i9100_native_transport_lease copy=lease;i9100_peripheral_i2c copied=io;copied.cookie=&copy;
  assert(copied.claim(copied.cookie)&&i9100_peripheral_native_transport_close(&copy)==I9100_GPIO_OWNER);
  assert(copied.transfer(copied.cookie,messages,2,&completed)==I9100_I2C_ARGUMENT&&writes==i2c_card::hosts[index].writes);
  assert(!io.claim(io.cookie));assert(i9100_peripheral_native_transport_hold_put(&worker)==I9100_GPIO_BUSY);io.release(io.cookie);
  assert(!i9100_peripheral_native_transport_hold_put(&worker)&&!i9100_peripheral_native_transport_hold_put(&device));
  completed=77;unsigned before=barriers;
  assert(io.transfer(io.cookie,messages,2,(unsigned *)&lease)==I9100_I2C_ARGUMENT&&before==barriers&&lease.owned);
  assert(!i9100_peripheral_native_transport_close(&lease)&&!lease.owned&&io.claim(io.cookie));
  i9100_native_transport_lease next={};i9100_peripheral_i2c fresh;
  assert(!owned_open(c,&next,index,&fresh)&&next.generation>lease.generation&&io.claim(io.cookie));
  assert(!i9100_peripheral_native_transport_close(&next));
 }
 assert(wrapped_i2c_inits==6U);i2c_restored(c);
}
static void legacy_exclusion()
{
 i9100_peripheral_native *c=fresh_i2c_parent();i9100_peripheral_i2c old;
 assert(!i9100_peripheral_native_i2c_open(c,0,100000000U,&old));assert(!old.claim(old.cookie));old.release(old.cookie);
 assert(!i9100_peripheral_native_i2c_close(c,0)&&old.claim(old.cookie));
 i9100_native_transport_lease owner={};i9100_peripheral_i2c io;
 assert(!owned_open(c,&owner,0,&io)&&old.claim(old.cookie));
 uint8_t reg=0x20;unsigned done=99;i9100_i2c_message message={0x19,0,1,&reg};unsigned writes=i2c_card::hosts[0].writes;
 assert(old.transfer(old.cookie,&message,1,&done)==I9100_I2C_BUSY&&!done&&writes==i2c_card::hosts[0].writes);
 assert(!i9100_peripheral_native_transport_close(&owner));i2c_restored(c);
}
static void retained_faults()
{
 for(unsigned fault=0;fault<5;fault++){
  i9100_peripheral_native *c=fresh_i2c_parent();i9100_native_transport_lease lease={};i9100_peripheral_i2c io;
  i2c_card::host &h=i2c_card::hosts[0];
  if(fault==0){h.fail_write=1;h.drop_enabled=1;h.drop_register=16;h.drop_value=3;
   assert(owned_open(c,&lease,0,&io)==I9100_I2C_IO&&lease.owned&&io.cookie==&lease&&!io.transfer);}
  else{
   assert(!owned_open(c,&lease,0,&io));
   if(fault==1){h.drop_enabled=1;h.drop_register=16;h.drop_value=3;}
   if(fault==2)drop_i2c_release=1U<<7;
   if(fault>=3){
    i9100_native_transport_hold holder={};assert(!i9100_peripheral_native_transport_hold_get(&lease,&holder));
    h.stalled=fault==3;h.arbitration=fault==4;freeze_model_time=fault==3;
    uint8_t reg=0x20;i9100_i2c_message msg={0x19,0,1,&reg};unsigned done=99;
    assert(!io.claim(io.cookie));
    assert(io.transfer(io.cookie,&msg,1,&done)==(fault==3?I9100_I2C_TIMEOUT:I9100_I2C_ARBITRATION));io.release(io.cookie);
    assert(c->buses[0].bus.fault&&i9100_peripheral_native_transport_hold_check(&holder)==I9100_GPIO_IO);
    assert(i9100_peripheral_native_transport_close(&lease)==I9100_I2C_BUSY&&!c->buses[0].closing);
    assert(!i9100_peripheral_native_transport_hold_put(&holder));
    if(fault==3){h.stalled=0;freeze_model_time=false;assert(!i9100_peripheral_native_transport_close(&lease));i2c_restored(c);continue;}
    assert(!h.stops); /* actual arbitration path never generates a STOP */
   }
   assert(i9100_peripheral_native_transport_close(&lease)==(fault==2?I9100_CLOCK_IO:fault==4?I9100_I2C_BUS:I9100_I2C_IO));
  }
  assert(lease.owned&&c->buses[0].closing&&c->buses[0].live);
  assert(i9100_peripheral_native_destroy(&c)==I9100_GPIO_BUSY&&c);
  unsigned writes=h.writes;assert(owned_i2c_claim(&lease)&&h.writes==writes);
  i9100_native_transport_lease another={};i9100_peripheral_i2c unused;
  assert(owned_open(c,&another,0,&unused)==I9100_I2C_BUSY&&!another.owned);
  if(fault==2)assert(!c->buses[0].bus.enabled&&!c->buses[0].pins.active&&c->buses[0].gate.active);
  else assert(c->buses[0].bus.enabled&&c->buses[0].pins.active&&c->buses[0].gate.active);
  h.fail_write=h.drop_enabled=h.arbitration=0;drop_i2c_release=0;
  if(fault==4){h.active=0;h.regs[1]&=~40U;} /* external stand-in bus recovery only */
  assert(!i9100_peripheral_native_transport_close(&lease)&&!lease.owned);i2c_restored(c);
 }
}
static void early_guards()
{
 i9100_peripheral_native *c=fresh_i2c_parent();i9100_native_transport_lease lease={};i9100_peripheral_i2c io;
 unsigned before=barriers;
 assert(i9100_peripheral_native_i2c_open_owned(c,&lease,3,100000000U,&io)==I9100_I2C_ARGUMENT&&!lease.owned&&before==barriers);
 assert(i9100_peripheral_native_i2c_open_owned(c,&lease,0,1U,&io)==I9100_I2C_ARGUMENT&&!lease.owned&&!c->buses[0].live);
 c->buses[0].generation=0xffffffffU;before=barriers;
 assert(owned_open(c,&lease,0,&io)==I9100_I2C_BUSY&&!lease.owned&&before==barriers);c->buses[0].generation=0;
 memory[0x10490000U][0x108/4]=1U<<(91%32U);before=wrapped_i2c_inits;
 assert(owned_open(c,&lease,0,&io)==I9100_I2C_BUSY&&!lease.owned&&before==wrapped_i2c_inits);memory[0x10490000U][0x108/4]=0;
 interrupts=false;before=barriers;assert(owned_open(c,&lease,0,&io)==I9100_I2C_BUSY&&!lease.owned&&before==barriers);interrupts=true;
 i2c_restored(c);
}
static void explicit_controller_recovery()
{
 i9100_peripheral_native *c=fresh_i2c_parent();i9100_native_transport_lease lease={};i9100_peripheral_i2c io;
 assert(!owned_open(c,&lease,0,&io));
 assert(i9100_peripheral_native_i2c_recover_owned(&lease)==I9100_I2C_ARGUMENT);
 i9100_native_transport_hold device={};assert(!i9100_peripheral_native_transport_hold_get(&lease,&device));
 i2c_card::host &h=i2c_card::hosts[0];h.stalled=1;freeze_model_time=true;
 uint8_t reg=0x20;i9100_i2c_message msg={0x19,0,1,&reg};unsigned done=0;
 assert(!io.claim(io.cookie));assert(io.transfer(io.cookie,&msg,1,&done)==I9100_I2C_TIMEOUT);io.release(io.cookie);
 h.stalled=0;freeze_model_time=false;unsigned writes=h.writes;
 assert(i9100_peripheral_native_i2c_recover_owned(&lease)==I9100_I2C_BUS&&h.writes==writes&&c->buses[0].bus.fault);
 const i9100_i2c_board_bus &p=i9100_u1_i2c[0];const i9100_gpio_bank_config &bank=i9100_u1_banks[p.bank];
 memory[i9100_gpio_physical[bank.region]][(bank.offset+4U)/4]|=(1U<<p.sda)|(1U<<p.scl);
 uint32_t generation=lease.generation;
 assert(!i9100_peripheral_native_i2c_recover_owned(&lease)&&!c->buses[0].bus.fault&&lease.generation==generation);
 assert(c->buses[0].borrowers==1&&device.owned&&!i9100_peripheral_native_transport_hold_check(&device));
 assert(!io.claim(io.cookie));assert(!io.transfer(io.cookie,&msg,1,&done)&&done==1);io.release(io.cookie);
 assert(!i9100_peripheral_native_transport_hold_put(&device));
 assert(!i9100_peripheral_native_transport_close(&lease));i2c_restored(c);
}
int main()
{
 transactions_and_holds();legacy_exclusion();retained_faults();early_guards();explicit_controller_recovery();
 puts("PASS real native I2C1/6/7 + real Samsung controller/repeated START/slave transfers, original mutex/time callbacks, per-device/worker holds, stale/copy/legacy exclusion, partial enable+restore retention, timeout/arbitration and GPIO-clock lifecycle; registers/slaves/IOKit are stand-ins, no phone proof");
 return 0;
}
