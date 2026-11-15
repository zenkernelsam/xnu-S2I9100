/* SPDX-License-Identifier: BSD-2-Clause
 * GPIO electrical/byte slave model independent of the master implementation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_gpio_broker.h"
#include "i9100_i2c_gpio.h"
#include "i9100_clock_gates.h"
#include "i9100_irq_registry.h"
#include "i9100_dma_guard.h"
enum {IDLE,RX,ACK,TX,MASTER_ACK,WAIT_STOP};
struct model {
 uint32_t regs[2][1024];unsigned locked,reads,writes,failed_lock;
 unsigned slave,sda,scl,base,region,state,bits,byte,address_phase,read_mode,need_register;
 unsigned out_low,ack_low,ack_seen,master_ack,starts,stops,received[32],received_count,read_acks[32],read_ack_count;
 unsigned force_clock,force_bus,arbitrate,nack_address,nack_data,recovery_rises;
 uint8_t memory[256],pointer,tx;
 uint64_t ns;
 int stopped_clock;
};
static unsigned host(struct model *m,unsigned pin)
{unsigned con=(m->regs[m->region][m->base/4]>>(pin*4))&15U;return con!=1||!!(m->regs[m->region][m->base/4+1]&(1U<<pin));}
static unsigned scl(struct model *m){return host(m,m->scl)&&!m->force_clock;}
static unsigned sda(struct model *m){return host(m,m->sda)&&!m->out_low&&!m->force_bus;}
static void tx_bit(struct model *m){m->out_low=!((m->tx>>(7U-m->bits))&1U);}
static void rise(struct model *m)
{
 if(m->force_bus&&++m->recovery_rises>=9)m->force_bus=0;
 if(!m->slave)return;
 if(m->state==RX){
  if(m->arbitrate&&host(m,m->sda)){m->out_low=1;m->state=WAIT_STOP;return;}
  m->byte=(m->byte<<1)|sda(m);m->bits++;
 }else if(m->state==ACK)m->ack_seen=1;
 else if(m->state==TX)m->bits++;
 else if(m->state==MASTER_ACK){m->master_ack=!host(m,m->sda);assert(m->read_ack_count<32);m->read_acks[m->read_ack_count++]=m->master_ack;}
}
static void fall(struct model *m)
{
 if(!m->slave)return;
 if(m->state==RX&&m->bits==8){
  assert(m->received_count<32);m->received[m->received_count++]=m->byte;
  if(m->address_phase){m->read_mode=m->byte&1U;m->ack_low=(m->byte>>1)==0x19&&!m->nack_address;}
  else m->ack_low=!m->nack_data;
  if(m->ack_low&&!m->address_phase){if(m->need_register){m->pointer=(uint8_t)m->byte;m->need_register=0;}else m->memory[m->pointer++]=(uint8_t)m->byte;}
  m->out_low=m->ack_low;m->state=ACK;m->ack_seen=0;
 }else if(m->state==ACK&&m->ack_seen){
  m->out_low=0;m->bits=m->byte=0;
  if(!m->ack_low){m->state=WAIT_STOP;return;}
  if(m->address_phase&&m->read_mode){m->state=TX;m->tx=m->memory[m->pointer];tx_bit(m);}
  else{m->state=RX;if(m->address_phase)m->need_register=1;}
  m->address_phase=0;
 }else if(m->state==TX){
  if(m->bits==8){m->state=MASTER_ACK;m->out_low=0;}else tx_bit(m);
 }else if(m->state==MASTER_ACK){
  if(m->master_ack){m->pointer++;m->tx=m->memory[m->pointer];m->bits=0;m->state=TX;tx_bit(m);}
  else{m->state=WAIT_STOP;m->out_low=0;}
 }
}
static uint32_t read32(void *cookie,unsigned region,unsigned off)
{
 struct model *m=cookie;assert(m->locked&&region<2&&off<4096&&!(off&3));m->reads++;
 uint32_t value=m->regs[region][off/4];
 if(region==m->region&&off==m->base+4){
  value=(value&~((1U<<m->sda)|(1U<<m->scl)))|(sda(m)<<m->sda)|(scl(m)<<m->scl);
 }
 return value;
}
static void write32(void *cookie,unsigned region,unsigned off,uint32_t value)
{
 struct model *m=cookie;assert(m->locked&&region<2&&off<4096&&!(off&3));m->writes++;
 unsigned clock=scl(m),data=sda(m);m->regs[region][off/4]=value;
 if(!m->slave&&!m->force_bus)return;
 /* No push-pull high may be generated on either I2C line. */
 for(unsigned n=0;n<2;n++){unsigned pin=n?m->scl:m->sda;assert(((m->regs[m->region][m->base/4]>>(pin*4))&15U)!=1||!(m->regs[m->region][m->base/4+1]&(1U<<pin)));}
 unsigned newclock=scl(m),newdata=sda(m);
 if(clock&&newclock&&data&&!newdata){m->starts++;m->state=RX;m->address_phase=1;m->bits=m->byte=0;m->out_low=0;}
 else if(clock&&newclock&&!data&&newdata){m->stops++;m->state=IDLE;m->out_low=0;}
 else if(!clock&&newclock)rise(m);else if(clock&&!newclock)fall(m);
}
static int lock(void *cookie){struct model *m=cookie;if(m->failed_lock)return 1;assert(!m->locked);m->locked=1;return 0;}
static void unlock(void *cookie){struct model *m=cookie;assert(m->locked);m->locked=0;}
static uint64_t now(void *cookie){struct model *m=cookie;return m->stopped_clock?0:m->ns;}
static int delay(void *cookie,uint32_t us){struct model *m=cookie;assert(!m->locked);m->ns+=(uint64_t)us*1000U;return 0;}
static void setup(struct model *m,struct i9100_gpio_broker *r)
{
 memset(m,0,sizeof(*m));memset(r,0,sizeof(*r));m->base=0x80;m->sda=3;m->scl=4;
 for(unsigned n=0;n<4;n++)m->regs[0][m->base/4+n]=0xdeadbeefU;
 m->regs[0][m->base/4]&=~((15U<<12)|(15U<<16));m->regs[0][m->base/4+1]&=~((1U<<3)|(1U<<4));
 struct i9100_gpio_bank_config cfg[2]={{0x40,0,8,0xc0},{0x80,0,8,2}};
 struct i9100_gpio_io io={m,read32,write32,lock,unlock};assert(!i9100_gpio_broker_init(r,&io,cfg,2));
}
static void gpio_tests(void)
{
 struct model m;struct i9100_gpio_broker r;struct i9100_gpio_lease a={0},b={0};setup(&m,&r);
 assert(!m.reads&&!m.writes&&i9100_gpio_claim(&r,1,24,&a)==I9100_GPIO_DISABLED);
 assert(!m.reads&&!m.writes);assert(!i9100_gpio_broker_enable(&r,1));
 assert(i9100_gpio_claim(&r,0,0x40,&a)==I9100_GPIO_RESERVED&&!m.reads&&!m.writes);
 assert(!i9100_gpio_claim(&r,1,24,&a));assert(i9100_gpio_claim(&r,1,8,&b)==I9100_GPIO_BUSY);
 assert(i9100_gpio_broker_enable(&r,0)==I9100_GPIO_BUSY);
 assert(i9100_gpio_output(&a,2,1)==I9100_GPIO_OWNER);
 /* DAT reads the electrical input level, not an inaccessible output latch. */
 uint32_t original[4];memcpy(original,a.saved,16);
 assert(!i9100_gpio_output(&a,3,0)&&!i9100_gpio_pull(&a,3,0)&&!i9100_gpio_drive(&a,4,2));
 m.regs[0][m.base/4]^=15U<<24;m.regs[0][m.base/4+1]^=1U<<6;m.regs[0][m.base/4+2]^=3U<<12;m.regs[0][m.base/4+3]^=3U<<12;
 uint32_t foreign[4];memcpy(foreign,m.regs[0]+m.base/4,16);struct i9100_gpio_lease stale=a;
 assert(!i9100_gpio_release(&a,I9100_GPIO_RESTORE));uint32_t masks[4]={(15U<<12)|(15U<<16),24,(3U<<6)|(3U<<8),(3U<<6)|(3U<<8)};
 for(unsigned n=0;n<4;n++)assert(m.regs[0][m.base/4+n]==((foreign[n]&~masks[n])|(original[n]&masks[n])));
 assert(!i9100_gpio_claim(&r,1,24,&b));unsigned writes=m.writes;assert(i9100_gpio_output(&stale,3,0)==I9100_GPIO_OWNER&&m.writes==writes);
 assert(!i9100_gpio_release(&b,I9100_GPIO_HIGH_Z));assert(!i9100_gpio_broker_enable(&r,0));
 puts("PASS GPIO disabled/reserved/exclusive leases,stale-token refusal,foreign-field preservation and safe release");
}
static void bus_setup(struct model *m,struct i9100_gpio_broker *r,struct i9100_i2c_gpio *b)
{
 setup(m,r);memset(b,0,sizeof(*b));assert(!i9100_gpio_broker_enable(r,1));
 struct i9100_i2c_timing t={m,now,delay,5,32,1000000U,100000000U};assert(!i9100_i2c_gpio_init(b,r,1,3,4,&t));
 m->slave=1;m->memory[15]=0x33;m->memory[16]=0x84;
}
static void i2c_tests(void)
{
 struct model m;struct i9100_gpio_broker r;struct i9100_i2c_gpio b;unsigned done;uint8_t reg=15,bytes[2]={0};
 struct i9100_i2c_message msgs[2]={{0x19,0,1,&reg},{0x19,1,2,bytes}};
 bus_setup(&m,&r,&b);assert(!i9100_i2c_gpio_transfer(&b,msgs,2,&done)&&done==2&&bytes[0]==0x33&&bytes[1]==0x84);
 assert(m.starts==2&&m.stops==1&&m.received_count==3&&m.received[0]==0x32&&m.received[1]==15&&m.received[2]==0x33);
 assert(m.read_ack_count==2&&m.read_acks[0]==1&&m.read_acks[1]==0);m.slave=0;assert(!i9100_i2c_gpio_destroy(&b));
 bus_setup(&m,&r,&b);m.nack_address=1;assert(i9100_i2c_gpio_transfer(&b,msgs,2,&done)==I9100_I2C_ADDRESS_NACK&&!done&&!b.busy);
 m.slave=0;assert(!i9100_i2c_gpio_destroy(&b));
 bus_setup(&m,&r,&b);m.nack_data=1;assert(i9100_i2c_gpio_transfer(&b,msgs,2,&done)==I9100_I2C_DATA_NACK&&!done&&!b.busy);
 m.slave=0;assert(!i9100_i2c_gpio_destroy(&b));
 bus_setup(&m,&r,&b);m.force_clock=m.stopped_clock=1;assert(i9100_i2c_gpio_transfer(&b,msgs,2,&done)==I9100_I2C_TIMEOUT&&!b.busy);
 m.slave=m.force_clock=0;assert(!i9100_i2c_gpio_destroy(&b));
 bus_setup(&m,&r,&b);m.arbitrate=1;unsigned stops=m.stops;assert(i9100_i2c_gpio_transfer(&b,msgs,2,&done)==I9100_I2C_ARBITRATION&&!b.busy&&m.stops==stops);
 m.slave=m.out_low=0;assert(!i9100_i2c_gpio_destroy(&b));
 bus_setup(&m,&r,&b);m.slave=0;m.force_bus=1;assert(!i9100_i2c_gpio_recover(&b)&&m.recovery_rises==9);assert(!i9100_i2c_gpio_destroy(&b));
 puts("PASS electrical I2C repeated START,address/data ACK,read ACK/NACK,clock-stall,NACK,arbitration and explicit recovery");
}
struct clock_model {uint32_t regs[32];unsigned locked,writes,ignore;};
static uint32_t clock_read(void *cookie,unsigned offset){struct clock_model *m=cookie;assert(m->locked&&offset>=0xc900&&offset<=0xc970);return m->regs[(offset-0xc900)/4];}
static void clock_write(void *cookie,unsigned offset,uint32_t value){struct clock_model *m=cookie;assert(m->locked&&offset>=0xc900&&offset<=0xc970);m->writes++;if(!m->ignore)m->regs[(offset-0xc900)/4]=value;}
static int clock_lock(void *cookie){struct clock_model *m=cookie;assert(!m->locked);m->locked=1;return 0;}
static void clock_unlock(void *cookie){struct clock_model *m=cookie;assert(m->locked);m->locked=0;}
static void clock_tests(void)
{
 struct clock_model model;memset(&model,0,sizeof(model));model.regs[(0xc940-0xc900)/4]=1;
 struct i9100_clock_gates m={0};struct i9100_clock_gate_config c[2]={{0xc950,1U<<7},{0xc940,1}};
 struct i9100_clock_io io={&model,clock_read,clock_write,clock_lock,clock_unlock};
 struct i9100_clock_lease a={0},b={0},initial={0};assert(!i9100_clock_init(&m,&io,c,2));
 assert(i9100_clock_acquire(&m,0,&a)==I9100_CLOCK_DISABLED&&!model.writes);
 assert(!i9100_clock_enable(&m,1)&&!i9100_clock_acquire(&m,0,&a)&&!i9100_clock_acquire(&m,0,&b));
 assert(m.gates[0].refs==2&&model.regs[(0xc950-0xc900)/4]&(1U<<7));
 struct i9100_clock_lease stale=a;assert(!i9100_clock_release(&a)&&m.gates[0].refs==1);
 unsigned writes=model.writes;assert(i9100_clock_release(&stale)==I9100_CLOCK_OWNER&&writes==model.writes);
 model.regs[(0xc950-0xc900)/4]|=1U<<30;assert(!i9100_clock_release(&b)&&model.regs[(0xc950-0xc900)/4]==1U<<30);
 assert(!i9100_clock_acquire(&m,1,&initial)&&!i9100_clock_release(&initial)&&model.regs[(0xc940-0xc900)/4]==1);
 model.ignore=1;assert(i9100_clock_acquire(&m,0,&a)==I9100_CLOCK_IO&&!a.active);
 model.ignore=0;assert(!i9100_clock_acquire(&m,0,&a));model.ignore=1;assert(i9100_clock_release(&a)==I9100_CLOCK_IO&&a.active);
 assert(i9100_clock_enable(&m,0)==I9100_CLOCK_BUSY);model.ignore=0;assert(!i9100_clock_release(&a)&&!i9100_clock_enable(&m,0));
 puts("PASS gate-only clock leases,reference counts,pre-existing clocks,foreign bits,stale tokens and failed-write ownership retention");
}
struct irq_model {uint32_t regs[1024],masked,locked,last_ack,writes;int fail_enable,fail_mask,handler_error;struct i9100_irq_lease *lease;};
static uint32_t irq_read(void *cookie,unsigned off){struct irq_model *m=cookie;assert(m->locked&&off<4096&&!(off&3));return m->regs[off/4];}
static void irq_write(void *cookie,unsigned off,uint32_t v){
 struct irq_model *m=cookie;assert(m->locked&&off<4096&&!(off&3));m->writes++;
 if(off>=0x100&&off<0x120){if(!m->fail_enable)m->regs[off/4]|=v;}
 else if(off>=0x180&&off<0x1a0){if(!m->fail_mask)m->regs[(off-0x80)/4]&=~v;}
 else m->regs[off/4]=v;
}
static uint32_t irq_enter(void *cookie){struct irq_model *m=cookie;assert(!m->locked);uint32_t old=m->masked;m->masked=1;m->locked=1;return old;}
static void irq_leave(void *cookie,uint32_t old){struct irq_model *m=cookie;assert(m->locked);m->locked=0;m->masked=old;}
static int irq_handler(void *cookie,uint32_t ack){struct irq_model *m=cookie;assert(!m->locked);m->last_ack=ack;if(m->lease)assert(i9100_irq_unregister(m->lease)==I9100_IRQ_BUSY);return m->handler_error;}
static void irq_tests(void)
{
 struct irq_model m;memset(&m,0,sizeof(m));struct i9100_irq_registry r={0};struct i9100_irq_lease l={0},other={0};
 uint32_t allow[8]={0};allow[91/32]|=1U<<(91&31);allow[96/32]|=1U<<(96&31);
 struct i9100_irq_io io={&m,irq_read,irq_write,irq_enter,irq_leave};assert(!i9100_irq_registry_init(&r,&io,allow));
 assert(i9100_irq_register(&r,91,0,irq_handler,&m,&l)==I9100_IRQ_DISABLED&&!m.writes);
 assert(!i9100_irq_registry_enable(&r,1));assert(i9100_irq_register(&r,89,0,irq_handler,&m,&l)==I9100_IRQ_ARGUMENT);
 m.regs[(0x400+91/4*4)/4]=0x12345678;m.regs[(0x800+91/4*4)/4]=0x87654321;
 uint32_t priority=m.regs[(0x400+91/4*4)/4],target=m.regs[(0x800+91/4*4)/4];
 assert(!i9100_irq_register(&r,91,0,irq_handler,&m,&l));assert(i9100_irq_register(&r,91,0,irq_handler,&m,&other)==I9100_IRQ_BUSY);
 m.lease=&l;uint32_t ack=91U|(3U<<10);m.masked=1;assert(i9100_irq_dispatch(&r,ack)==1&&m.last_ack==ack&&m.masked==1);m.masked=0;
 assert(!i9100_irq_dispatch(&r,1023));m.lease=0;m.handler_error=-1;assert(i9100_irq_dispatch(&r,ack)==1&&!(m.regs[(0x100+91/32*4)/4]&(1U<<(91&31))));
 struct i9100_irq_lease stale=l;assert(!i9100_irq_unregister(&l));assert(m.regs[(0x400+91/4*4)/4]==priority&&m.regs[(0x800+91/4*4)/4]==target);
 assert(!i9100_irq_register(&r,91,1,irq_handler,&m,&other));unsigned writes=m.writes;assert(i9100_irq_unregister(&stale)==I9100_IRQ_OWNER&&m.writes==writes);assert(!i9100_irq_unregister(&other));
 m.regs[(0x100+96/32*4)/4]|=1U<<(96&31);assert(i9100_irq_register(&r,96,0,irq_handler,&m,&l)==I9100_IRQ_BUSY);
 m.fail_enable=1;assert(i9100_irq_register(&r,91,0,irq_handler,&m,&l)==I9100_IRQ_IO&&!l.active);assert(!i9100_irq_registry_enable(&r,0));
 puts("PASS IRQ whitelist/exclusive leases,full ACK token,no implicit EOI,active callback lifetime,source masking and original GIC fields");
}
struct dma_model {unsigned locked,busy,released,fail_sync,count,actions[32];uintptr_t cpu;uint32_t pa,bytes;};
static int dma_lock(void *cookie){struct dma_model *m=cookie;assert(!m->locked);m->locked=1;return 0;}
static void dma_unlock(void *cookie){struct dma_model *m=cookie;assert(m->locked);m->locked=0;}
static int dma_sync(void *cookie,uintptr_t cpu,uint32_t pa,uint32_t bytes,unsigned action){struct dma_model *m=cookie;assert(m->locked&&cpu==m->cpu&&pa==m->pa&&bytes==m->bytes&&m->count<32);m->actions[m->count++]=action;return m->fail_sync;}
static int dma_idle(void *cookie){struct dma_model *m=cookie;assert(m->locked);return m->busy;}
static void dma_release(void *cookie,uintptr_t cpu,uint32_t pa,uint32_t bytes){struct dma_model *m=cookie;assert(!m->locked&&cpu==m->cpu&&pa==m->pa&&bytes==m->bytes&&!m->busy);m->released++;}
static void dma_tests(void)
{
 struct dma_model m={0};m.cpu=0x90000000U;m.pa=0x50000000U;m.bytes=4096;
 struct i9100_dma_io io={&m,dma_lock,dma_unlock,dma_sync,dma_idle,dma_release};struct i9100_dma_buffer b={0};
 assert(i9100_dma_bind(&b,&io,m.cpu,m.pa+1,m.bytes)==I9100_DMA_ARGUMENT);
 assert(!i9100_dma_bind(&b,&io,m.cpu,m.pa,m.bytes));assert(i9100_dma_prepare(&b,I9100_DMA_TO_DEVICE)==I9100_DMA_DISABLED&&!m.count);
 assert(!i9100_dma_enable(&b,1));struct i9100_dma_buffer copied=b;assert(i9100_dma_prepare(&copied,I9100_DMA_TO_DEVICE)==I9100_DMA_OWNER);
 assert(!i9100_dma_prepare(&b,I9100_DMA_TO_DEVICE)&&m.actions[0]==I9100_DMA_CLEAN&&m.actions[1]==I9100_DMA_BARRIER);
 assert(i9100_dma_release(&b)==I9100_DMA_OWNER&&!m.released);m.busy=1;assert(i9100_dma_complete(&b)==I9100_DMA_BUSY&&b.state==I9100_DMA_QUARANTINED);
 m.busy=0;assert(!i9100_dma_abort(&b)&&b.state==I9100_DMA_CPU);m.count=0;
 assert(!i9100_dma_prepare(&b,I9100_DMA_FROM_DEVICE));assert(m.actions[0]==I9100_DMA_CLEAN_INVALIDATE&&m.actions[1]==I9100_DMA_BARRIER);
 assert(!i9100_dma_complete(&b)&&m.actions[3]==I9100_DMA_INVALIDATE);m.count=0;m.fail_sync=1;
 assert(i9100_dma_prepare(&b,I9100_DMA_FROM_DEVICE)==I9100_DMA_IO&&b.state==I9100_DMA_QUARANTINED&&!b.handed_to_device);
 m.fail_sync=0;m.count=0;assert(!i9100_dma_abort(&b)&&m.actions[1]==I9100_DMA_CLEAN);assert(!i9100_dma_release(&b)&&m.released==1);
 assert(i9100_dma_release(&b)==I9100_DMA_OWNER&&m.released==1);
 puts("PASS DMA32 ranges/cache-line ownership,directional sync,dirty-data-safe abort,quarantine,idle proof and no active/free reuse");
}
int main(void){gpio_tests();i2c_tests();clock_tests();irq_tests();dma_tests();return 0;}
