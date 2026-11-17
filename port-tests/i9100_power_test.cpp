#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "i9100_power.h"

struct BatteryModel {
 uint8_t pmic[256];uint16_t fuel[256];unsigned adc6=937,adc7=989;
 bool plugged=true,sub=false,adcOK=true;unsigned ops=0,failAt=0,subWrites=0;
 uint64_t time=1000000000ULL;
 BatteryModel(){
  memset(pmic,0,sizeof(pmic));memset(fuel,0,sizeof(fuel));
  pmic[0]=0x77;pmic[0x10]=0x1a;pmic[0x50]=0x8f;pmic[0x51]=0x12;
  pmic[0x52]=0xb3;pmic[0x53]=0xa9;pmic[0x54]=0xd2;pmic[0x55]=0x91;
  fuel[0x21]=0x0092;fuel[9]=48000;fuel[0xff]=50*256;
 }
 bool operation(){return ++ops!=failAt;}
 static int read(void *c,unsigned reg,uint8_t *v){auto m=(BatteryModel *)c;if(!m->operation())return 0;*v=m->pmic[reg];return 1;}
 static int write(void *c,unsigned reg,uint8_t v){
  auto m=(BatteryModel *)c;assert((reg>=0x50 && reg<=0x55) || reg==0x5a);
  if(!m->operation())return 0;m->pmic[reg]=v;return 1;
 }
 static int gauge(void *c,unsigned reg,uint16_t *v){auto m=(BatteryModel *)c;if(!m->operation())return 0;*v=m->fuel[reg];return 1;}
 static int adc(void *c,unsigned channel,unsigned *v){
  auto m=(BatteryModel *)c;assert(channel==6 || channel==7);
  if(!m->operation() || !m->adcOK)return 0;*v=channel==6?m->adc6:m->adc7;return 1;
 }
 static int status(void *c,unsigned *connected,unsigned *charging){
  auto m=(BatteryModel *)c;if(!m->operation())return 0;
  *connected=m->plugged;*charging=m->sub && m->plugged;return 1;
 }
 static int enable(void *c,unsigned en){auto m=(BatteryModel *)c;assert(en<2);m->sub=en;m->subWrites++;return m->operation();}
 static uint64_t now(void *c){return ((BatteryModel *)c)->time;}
 i9100_power_io io(){i9100_power_io io={};io.cookie=this;io.pmic_read=read;io.pmic_write=write;
  io.fuel_read=gauge;io.adc_read=adc;io.sub_read=status;io.sub_enable=enable;io.now_ns=now;return io;}
};
static void policy_checks()
{
 for(unsigned rev=0;rev<16;rev++){
  BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();
  assert(i9100_power_init(&s,&io,rev)==0 && s.use_sub==(rev>=8));
  assert((m.pmic[0x51]&0xc0)==0 && !m.sub);
  if(rev<8){
   assert(m.pmic[0x52]==0xb0 && m.pmic[0x53]==0xa5 && m.pmic[0x54]==0xd5);
   assert(m.pmic[0x50]==0xaf && m.pmic[0x55]==0x91 && (m.pmic[0x5a]&0x40));
  }else assert(m.pmic[0x52]==0xb3 && m.pmic[0x53]==0xa9); // Do not tune unused charger.
  int rc=i9100_power_poll(&s);
  if(rc || !s.valid || !s.present || !s.connected || !s.charging || !s.enabled)
   fprintf(stderr,"revision=%u rc=%d valid=%u present=%u cable=%u charging=%u enable=%u reason=%d temp=%d mv=%u\n",rev,rc,s.valid,s.present,s.connected,s.charging,s.enabled,s.reason,s.temperature_deci_c,s.millivolts);
  assert(rc==0 && s.valid && s.present && s.connected && s.charging && s.enabled);
  assert(s.percent==50 && s.millivolts==3750 && s.temperature_deci_c==250);
  if(rev>=8)assert(m.sub && !(m.pmic[0x51]&0xc0));
  else assert(!m.sub && !m.subWrites && (m.pmic[0x51]&0xc0)==0xc0);
  m.adcOK=false;assert(i9100_power_poll(&s)<0 && !s.valid && !s.enabled && !m.sub && !(m.pmic[0x51]&0xc0));
 }
 for(unsigned rev: {7U,8U}){
  BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();assert(!i9100_power_init(&s,&io,rev));
  assert(!i9100_power_poll(&s));
  (rev<8?m.adc6:m.adc7)=rev<8?544:603;
  assert(!i9100_power_poll(&s) && s.reason==I9100_POWER_TEMPERATURE && !s.enabled);
  (rev<8?m.adc6:m.adc7)=rev<8?570:630; // 43C: hysteresis holds the stop.
  assert(!i9100_power_poll(&s) && !s.enabled);
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  assert(!i9100_power_poll(&s) && s.enabled);
  m.fuel[9]=53760;assert(!i9100_power_poll(&s) && s.full && !s.enabled && s.reason==I9100_POWER_FULL);
  m.fuel[9]=52992;m.fuel[0xff]=94*256; // 4.14V is above the conservative recharge threshold.
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count);
  m.fuel[9]=48000;(rev<8?m.adc6:m.adc7)=rev<8?570:630;
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count); // 43C cannot start a new cycle.
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  m.fuel[9]=48000;assert(!i9100_power_poll(&s) && !s.enabled && s.recharge_low_count==1);
  (rev<8?m.adc6:m.adc7)=rev<8?544:603;
  assert(!i9100_power_poll(&s) && !s.enabled && !s.recharge_low_count); // Hot sample breaks the consecutive count.
  (rev<8?m.adc6:m.adc7)=rev<8?937:989;
  assert(!i9100_power_poll(&s) && !s.enabled && s.recharge_low_count==1);
  assert(!i9100_power_poll(&s) && s.enabled && !s.full && s.recharging); // Recharge without USB detach.
  uint64_t started=s.charge_start;m.time+=600ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.enabled && s.charge_start==started && s.charge_refresh==m.time);
  m.time=started+5400ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled);
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled); // Never auto-clear timeout.
  m.plugged=false;m.pmic[0x10]=0;assert(!i9100_power_poll(&s) && !s.full && !s.connected);
  m.plugged=true;m.pmic[0x10]=0x1a;assert(!i9100_power_poll(&s) && s.enabled);
  m.time+=18000ULL*1000000000ULL;
  assert(!i9100_power_poll(&s) && s.timeout && !s.enabled && s.reason==I9100_POWER_TIMEOUT);
 }
 BatteryModel m;i9100_power_state s;i9100_power_io io=m.io();
 assert(i9100_power_init(&s,&io,0xffffffffU)==I9100_POWER_REVISION && !m.ops && !m.subWrites);
 unsigned faultChecks=0;
 for(unsigned rev: {7U,8U}){
  BatteryModel baseline;i9100_power_state test;auto baseIO=baseline.io();assert(!i9100_power_init(&test,&baseIO,rev));
  baseline.ops=0;assert(!i9100_power_poll(&test));unsigned steps=baseline.ops;
  for(unsigned fail=1;fail<=steps;fail++){
   BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&test,&faultIO,rev));
   fault.ops=0;fault.failAt=fail;assert(i9100_power_poll(&test)<0);
   assert(!test.valid && !test.enabled && !fault.sub && !(fault.pmic[0x51]&0xc0));faultChecks++;
  }
 }
 for(unsigned invalid: {0U,65535U}){
  BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&s,&faultIO,8));
  fault.fuel[0x21]=invalid;assert(i9100_power_poll(&s)<0 && !s.enabled);
 }
 for(unsigned which=0;which<6;which++){
  BatteryModel fault;auto faultIO=fault.io();assert(!i9100_power_init(&s,&faultIO,8));
  if(which==0)fault.pmic[0x10]|=4;
  if(which==1)fault.fuel[0]|=8;
  if(which==2)fault.fuel[9]=56000;
  if(which==3)fault.fuel[0xff]=0xffff;
  if(which==4)fault.adc7=0;
  if(which==5)fault.adc7=4095;
  (void)i9100_power_poll(&s);assert(!s.enabled && !s.charging && !fault.sub);
  if(which<2)assert(s.valid && !s.present && s.reason==I9100_POWER_NO_BATTERY);
  else assert(!s.valid && s.reason==I9100_POWER_IO);
 }
 int temp;assert(!i9100_power_temperature(6,0,&temp) && !i9100_power_temperature(7,4095,&temp));
 assert(i9100_power_temperature(6,937,&temp) && temp==250);
 assert(i9100_power_temperature(7,989,&temp) && temp==250);
 printf("Charging policy: 16 revisions, full-stop recharge, 90min maintenance bound, refresh preserving deadline, timeout latch, voltage/thermal guards and %u injected I/O failures passed\n",faultChecks);
}

/* Byte-level I2C peripheral driven by GPIO edges. Each instance models only
 * one known bus/address; touching another bank fails the test. */
struct PowerGPIO {
 enum Phase {IDLE,RX,SLAVE_ACK,TX,MASTER_ACK};
 uint32_t regs[4][16384];uint8_t memory[256],hapticMemory[256];
 bool withHaptic=false,dropSlaveWrite=false;unsigned currentAddress=0,dropAddress=0,dropRegister=0,dropValue=0;
 unsigned region,base,sda,scl,address;
 Phase phase=IDLE,next=IDLE;bool oldClock=true,oldData=true,slaveLow=false,expectAddress=true;
 bool readMode=false,nack=false,stuckClock=false,stuckData=false,masterACK=false,adcStuck=false;
 unsigned bits=0,ackPhase=0,rxByte=0,registerBytes=0,pointer=0,txByte=0,starts=0,stops=0,waits=0;
 unsigned enters=0,leaves=0,guard=0,adcSamples=0;std::vector<uint8_t> received;
 explicit PowerGPIO(bool fuel,bool haptic=false):withHaptic(haptic),region(fuel?1:0),base(fuel?0x1a0:0x40),sda(fuel?0:6),scl(fuel?1:7),address(fuel?0x36:0x66){
  memset(regs,0,sizeof(regs));memset(memory,0,sizeof(memory));memset(hapticMemory,0,sizeof(hapticMemory));currentAddress=address;
  regs[region][base/4]=0;regs[region][(base+4)/4]=0xa5a50000;
  regs[region][(base+8)/4]=0x55660000;
 }
 bool master(unsigned pin)const{
  unsigned function=(regs[region][base/4]>>(pin*4))&15U;
  if(function!=1)return true;
  assert(!(regs[region][(base+4)/4]&(1U<<pin)));return false;
 }
 bool clock()const{return master(scl) && !stuckClock;}
 bool data()const{return master(sda) && !slaveLow && !stuckData;}
 uint8_t *selectedMemory(){return currentAddress==0x48U?hapticMemory:memory;}
 void beginTX(){phase=TX;bits=0;txByte=selectedMemory()[pointer&255];slaveLow=!(txByte&0x80);}
 void acceptedByte(){
  received.push_back((uint8_t)rxByte);next=RX;
  if(expectAddress){expectAddress=false;assert((rxByte>>1)==address||(withHaptic&&(rxByte>>1)==0x48U));currentAddress=rxByte>>1;
   readMode=rxByte&1;if(!readMode)registerBytes=0;if(readMode)next=TX;}
  else if(!readMode){if(!registerBytes){pointer=rxByte;registerBytes=1;}else{
   unsigned reg=pointer++&255U;if(!(dropSlaveWrite&&currentAddress==dropAddress&&reg==dropRegister&&rxByte==dropValue))selectedMemory()[reg]=(uint8_t)rxByte;}}
  phase=SLAVE_ACK;ackPhase=0;
 }
 void step(){
  bool clk=clock(),dat=data();
  if(oldClock && clk && oldData!=dat){
   if(!dat){phase=RX;bits=0;rxByte=0;expectAddress=true;slaveLow=false;starts++;}
   else{phase=IDLE;slaveLow=false;stops++;}
  }else if(!oldClock && clk){
   if(phase==RX){rxByte=(rxByte<<1)|dat;if(++bits==8)acceptedByte();}
   else if(phase==TX){if(++bits==8){phase=MASTER_ACK;ackPhase=0;}}
   else if(phase==MASTER_ACK && ackPhase==1)masterACK=!dat;
  }else if(oldClock && !clk){
   if(phase==SLAVE_ACK){
    if(!ackPhase){slaveLow=!nack;ackPhase=1;}
    else{slaveLow=false;if(nack)phase=IDLE;else if(next==TX)beginTX();else{phase=RX;bits=0;rxByte=0;}}
   }else if(phase==TX)slaveLow=!(txByte&(0x80U>>bits));
   else if(phase==MASTER_ACK){if(!ackPhase){slaveLow=false;ackPhase=1;}else if(masterACK){pointer++;beginTX();}else{phase=IDLE;slaveLow=false;}}
  }
  oldClock=clock();oldData=data();
 }
 static uint32_t read(void *cookie,unsigned region,unsigned off){
  auto m=(PowerGPIO *)cookie;assert(region<4 && off<65536 && !(off&3));
  uint32_t value=m->regs[region][off/4];
  if(region==m->region && off==m->base+4){unsigned mask=(1U<<m->sda)|(1U<<m->scl);value=(value&~mask)|(m->clock()?(1U<<m->scl):0)|(m->data()?(1U<<m->sda):0);}
  return value;
 }
 static void write(void *cookie,unsigned region,unsigned off,uint32_t value){
  auto m=(PowerGPIO *)cookie;assert(region<4 && off<65536 && !(off&3));
  assert((region==m->region && (off==m->base || off==m->base+4 || off==m->base+8)) ||
         (region==1 && (off==0x100 || off==0x104 || off==0x108)) ||
         (region==2 && off==0xc950) || (region==3 && (off==0 || off==0x18 || off==0x1c)));
  m->regs[region][off/4]=value;
  if(region==m->region && (off==m->base || off==m->base+4))m->step();
  if(region==3 && off==0 && (value&1U) && !m->adcStuck){
   assert((value&(1U<<16)) && (value&(1U<<14)));m->adcSamples++;
   m->regs[3][0]=(value&~1U)|(1U<<15);m->regs[3][3]=989;
  }
 }
 static void delay(void *c,unsigned){((PowerGPIO *)c)->waits++;}
 static void enter(void *c){auto m=(PowerGPIO *)c;assert(!m->guard);m->guard++;m->enters++;}
 static void leave(void *c){auto m=(PowerGPIO *)c;assert(m->guard==1);m->guard--;m->leaves++;}
 i9100_power_hw io(){i9100_power_hw h={this,read,write,delay,enter,leave};return h;}
};
static void bus_checks()
{
 for(bool fuel: {false,true}){
  PowerGPIO m(fuel);auto hw=m.io();m.memory[9]=0xa6;m.memory[10]=0x5b;
  uint32_t originalCon=m.regs[m.region][m.base/4],originalPull=m.regs[m.region][(m.base+8)/4];
  uint16_t word=0;uint8_t byte=0;
  if(fuel)assert(i9100_power_fuel_read(&hw,9,&word) && word==0x5ba6);
  else assert(i9100_power_pmic_read(&hw,9,&byte) && byte==0xa6);
  std::vector<uint8_t> expected={(uint8_t)(m.address<<1),9,(uint8_t)((m.address<<1)|1)};
  assert(m.received==expected && m.starts==2 && m.enters==m.leaves && !m.guard);
  assert(m.regs[m.region][m.base/4]==originalCon && m.regs[m.region][(m.base+8)/4]==originalPull);
  if(!fuel){assert(i9100_power_pmic_write(&hw,0x53,5));assert(m.memory[0x53]==5);}
  m.nack=true;word=0xabcd;byte=0xcc;
  if(fuel)assert(!i9100_power_fuel_read(&hw,9,&word) && word==0xabcd);
  else assert(!i9100_power_pmic_read(&hw,9,&byte) && byte==0xcc);
  m.nack=false;m.stuckClock=true;unsigned before=m.waits;
  if(fuel)assert(!i9100_power_fuel_read(&hw,9,&word));else assert(!i9100_power_pmic_read(&hw,9,&byte));
  assert(m.waits-before<1000 && m.enters==m.leaves && !m.guard);
 }
 PowerGPIO m(false);auto hw=m.io();
 m.regs[0][0x104/4]=11;assert(i9100_power_revision(&hw)==11);
 m.regs[0][0x100/4]=1;assert(i9100_power_revision(&hw)==0xffffffffU);
 m.regs[2][0xc950/4]=0xa501; m.regs[3][0]=4;m.regs[3][0x1c/4]=2;
 unsigned adc;assert(i9100_power_adc_read(&hw,7,&adc) && adc==989 && m.adcSamples==5);
 assert(m.regs[2][0xc950/4]==0xa501 && m.regs[3][0]==4 && m.regs[3][0x1c/4]==2);
 m.adcStuck=true;assert(!i9100_power_adc_read(&hw,6,&adc) && m.regs[2][0xc950/4]==0xa501 && m.regs[3][0]==4);
 m.regs[1][0x100/4]=0xabcdef00;m.regs[1][0x104/4]=0x88;
 assert(i9100_power_sub_enable(&hw,1) && (m.regs[1][0x104/4]&4));
 assert(i9100_power_sub_enable(&hw,0) && !(m.regs[1][0x104/4]&4));
 assert((m.regs[1][0x100/4]&~(15U<<8))==(0xabcdef00U&~(15U<<8)));
 unsigned connected,charging;assert(i9100_power_sub_read(&hw,&connected,&charging) && connected && charging);
 puts("Power hardware: dedicated addresses, repeated START, ACK/NACK, little-endian fuel words, open-drain/pin restoration, bounded timeout, ADC clock/config restoration, revision and charger GPIO masks passed");
}
int main(){policy_checks();bus_checks();return 0;}
