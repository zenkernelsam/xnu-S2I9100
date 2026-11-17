/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_power_haptic_model.h"
static void lifecycle_and_foreign_writes()
{
 HapticHardware hardware,mali;auto hw=hardware.io(),mh=mali.io();auto ops=hardware.ops();bool dead=false;auto *owner=new HapticOwner(&dead);
 assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));i9100_power_haptic_client client={},other={};
 assert(!i9100_power_haptic_open(&client,1)&&client.ready&&client.saved_ldo==0x28&&client.saved_config==0x83);
 assert(i9100_power_haptic_open(&other,1)==I9100_SHARED_BUSY&&!other.owned);
 i9100_power_haptic_client copied=client;uint8_t config=0;
 assert(i9100_power_haptic_read(&copied,&config)==I9100_SHARED_ARGUMENT);
 unsigned before=hardware.gpio.enters;assert(i9100_power_haptic_read(&client,(uint8_t *)&client)==I9100_SHARED_ARGUMENT&&before==hardware.gpio.enters);
 before=mali.gpio.enters;
 assert(!i9100_power_pmic_write(&mh,0x48,0)&&!i9100_power_haptic_config_write(&mh,0xc3)&&before==mali.gpio.enters);
 assert(i9100_power_pmic_write(&mh,0x53,5)&&mali.gpio.memory[0x53]==5);
 i9100_power_haptic_hold worker={},device={};assert(!i9100_power_haptic_hold_get(&client,&worker)&&!i9100_power_haptic_hold_get(&client,&device));
 auto copy=worker;assert(i9100_power_haptic_hold_put(&copy)==I9100_SHARED_ARGUMENT&&client.users==2);
 before=hardware.gpio.enters;assert(i9100_power_haptic_close(&client)==I9100_SHARED_BUSY&&before==hardware.gpio.enters);
 assert(!i9100_power_haptic_rail(&client,1)&&hardware.gpio.memory[0x48]==0xe8&&client.rail_owned);
 assert(!i9100_power_haptic_write(&client,0xc3)&&hardware.gpio.hapticMemory[2]==0xc3&&client.motor_owned);
 assert(i9100_power_haptic_write(&client,0xc3)==I9100_SHARED_BUSY);
 assert(i9100_power_haptic_write(&client,0xff)==I9100_SHARED_ARGUMENT&&hardware.gpio.hapticMemory[2]==0xc3);
 assert(!i9100_power_haptic_write(&client,0x83)&&!i9100_power_haptic_rail(&client,0));
 assert(hardware.gpio.memory[0x48]==0x28&&!client.motor_owned&&!client.rail_owned);
 assert(!i9100_power_haptic_hold_put(&worker)&&!i9100_power_haptic_hold_put(&device));
 assert(!i9100_power_haptic_close(&client)&&!client.owned);
 assert(i9100_power_pmic_write(&mh,0x48,0x28));
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);
}
static void borrowed_rail_and_revocation()
{
 HapticHardware hardware(true),replacement;auto hw=hardware.io(),newhw=replacement.io();auto ops=hardware.ops();bool dead=false,newdead=false;
 auto *owner=new HapticOwner(&dead),*next=new HapticOwner(&newdead);
 assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));i9100_power_haptic_client client={},another={};
 assert(!i9100_power_haptic_open(&client,1));i9100_power_haptic_hold worker={};assert(!i9100_power_haptic_hold_get(&client,&worker));
 assert(!i9100_power_haptic_rail(&client,1)&&!i9100_power_haptic_write(&client,0xc3));
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(!dead);
 assert(!i9100_power_shared_publish_haptic(next,&newhw,&ops));
 assert(i9100_power_haptic_open(&another,1)==I9100_SHARED_BUSY&&!another.owned);
 assert(i9100_power_haptic_hold_check(&worker)==I9100_SHARED_NOT_READY);
 assert(i9100_power_haptic_write(&client,0xc3)==I9100_SHARED_NOT_READY);
 assert(!i9100_power_haptic_write(&client,0x83)&&!i9100_power_haptic_rail(&client,0));
 assert(hardware.gpio.memory[0x48]==0xe8); /* original on rail is never forced off */
 assert(!i9100_power_haptic_hold_put(&worker)&&!i9100_power_haptic_close(&client)&&dead);
 assert(!i9100_power_haptic_open(&another,1)&&another.generation>client.generation);
 assert(!i9100_power_haptic_close(&another));assert(!i9100_power_shared_unpublish(next));next->release();assert(newdead);
}
static void faulted_writes_and_exact_cleanup()
{
 for(unsigned fault=0;fault<4;fault++){
  HapticHardware hardware;auto hw=hardware.io();auto ops=hardware.ops();bool dead=false;auto *owner=new HapticOwner(&dead);
  assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));i9100_power_haptic_client client={};assert(!i9100_power_haptic_open(&client,1));
  if(fault==0){hardware.gpio.nack=true;assert(i9100_power_haptic_rail(&client,1)==I9100_SHARED_IO&&client.rail_owned);hardware.gpio.nack=false;}
  if(fault==1){hardware.gpio.dropSlaveWrite=true;hardware.gpio.dropAddress=0x66;hardware.gpio.dropRegister=0x48;hardware.gpio.dropValue=0xe8;
   assert(i9100_power_haptic_rail(&client,1)==I9100_SHARED_IO&&client.rail_owned&&hardware.gpio.memory[0x48]==0x28);hardware.gpio.dropSlaveWrite=false;}
  if(fault>=2){assert(!i9100_power_haptic_rail(&client,1));
   if(fault==2){hardware.fail_after_config=true;}else{hardware.gpio.dropSlaveWrite=true;hardware.gpio.dropAddress=0x48;hardware.gpio.dropRegister=2;hardware.gpio.dropValue=0xc3;}
   assert(i9100_power_haptic_write(&client,0xc3)==I9100_SHARED_IO&&client.motor_owned);
   assert(hardware.gpio.hapticMemory[2]==(fault==2?0xc3:0x83));hardware.fail_after_config=false;hardware.gpio.dropSlaveWrite=false;
   assert(!i9100_power_haptic_write(&client,0x83));
  }
  assert(client.fault&&client.error==I9100_SHARED_IO);
  assert(i9100_power_haptic_close(&client)==I9100_SHARED_BUSY&&client.owned);
  assert(!i9100_power_haptic_rail(&client,0)&&!client.rail_owned);assert(!i9100_power_haptic_close(&client));
  assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);
 }
}
static void admission_and_selector_fault()
{
 HapticHardware hardware;auto hw=hardware.io();auto ops=hardware.ops();bool dead=false;auto *owner=new HapticOwner(&dead);
 assert(!i9100_power_shared_publish(owner,&hw));i9100_power_haptic_client client={};
 assert(i9100_power_haptic_open(&client,1)==I9100_SHARED_NOT_READY&&!client.owned);assert(!i9100_power_shared_unpublish(owner));
 assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));
 for(unsigned state=0;state<3;state++){
  hardware.gpio.hapticMemory[2]=state==0?0xc3:0x83;hardware.gpio.memory[0x48]=state==1?0x2c:state==2?0x68:0x28;
  assert(i9100_power_haptic_open(&client,1)==(state==0?I9100_SHARED_BUSY:I9100_SHARED_NOT_READY)&&!client.owned);
 }
 hardware.gpio.memory[0x48]=0x28;assert(!i9100_power_haptic_open(&client,1));
 hardware.gpio.memory[0x48]=0xe9;assert(i9100_power_haptic_rail(&client,1)==I9100_SHARED_IO&&client.rail_owned);
 assert(i9100_power_haptic_rail(&client,0)==I9100_SHARED_IO&&hardware.gpio.memory[0x48]==0xe9&&client.owned);
 hardware.gpio.memory[0x48]=0x28;assert(!i9100_power_haptic_rail(&client,0)&&!i9100_power_haptic_close(&client));
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);
}
static void revoked_in_flight_enable()
{
 HapticHardware hardware;auto hw=hardware.io();auto ops=hardware.ops();bool dead=false;auto *owner=new HapticOwner(&dead);
 assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));i9100_power_haptic_client client={};
 assert(!i9100_power_haptic_open(&client,1)&&!i9100_power_haptic_rail(&client,1));
 hardware.pause=true;hardware.entered=false;int result=0;
 std::thread writing([&]{result=i9100_power_haptic_write(&client,0xc3);});wait_entered(hardware);
 assert(i9100_power_haptic_close(&client)==I9100_SHARED_BUSY);
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(!dead);
 hardware.pause=false;writing.join();assert(result==I9100_SHARED_NOT_READY&&hardware.gpio.hapticMemory[2]==0x83);
 assert(!i9100_power_haptic_write(&client,0x83)&&!i9100_power_haptic_rail(&client,0));
 assert(!i9100_power_haptic_close(&client)&&dead);
}
static void concurrent_authorization()
{
 HapticHardware hardware,mali;auto hw=hardware.io(),mh=mali.io();auto ops=hardware.ops();bool dead=false;auto *owner=new HapticOwner(&dead);
 assert(!i9100_power_shared_publish_haptic(owner,&hw,&ops));i9100_power_haptic_client client={};
 assert(!i9100_power_haptic_open(&client,1)&&!i9100_power_haptic_rail(&client,1));
 std::thread authorized([&]{for(unsigned n=0;n<24;n++){assert(!i9100_power_haptic_write(&client,0xc3));assert(!i9100_power_haptic_write(&client,0x83));}});
 std::thread foreign([&]{for(unsigned n=0;n<24;n++){assert(!i9100_power_pmic_write(&mh,0x48,0));assert(!i9100_power_haptic_config_write(&mh,0xc3));}});
 authorized.join();foreign.join();assert(!mali.gpio.enters&&hardware.gpio.hapticMemory[2]==0x83&&!client.fault);
 assert(!i9100_power_haptic_rail(&client,0)&&!i9100_power_haptic_close(&client));
 assert(!i9100_power_shared_unpublish(owner));owner->release();assert(dead);
}
int main()
{
 assert(!i9100_power_shared_prepare(1));lifecycle_and_foreign_writes();borrowed_rail_and_revocation();faulted_writes_and_exact_cleanup();admission_and_selector_fault();revoked_in_flight_enable();concurrent_authorization();
 assert(haptic_live==3U);
 puts("PASS real shared haptic owner/raw GPIO wire: slave48 CONFIG2 + slave66 LDO14,2.8V/normal admission,foreign writes denied,device+worker holds,borrowed-on rail preserved,revoked/delivered/ACK-without-write faults,exact cleanup and retained provider; no PWM/motor/electrical proof");return 0;
}
