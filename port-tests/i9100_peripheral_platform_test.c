/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_peripheral_board.h"
#include "i9100_i2c_samsung.h"
#include "i9100_irq_cascade.h"
#include "i9100_audss.h"
struct model {
 uint32_t regs[3][16384],writes,reads,locked,irq_saved,fail_write;
 uint32_t pending_byte,rx,active,address_phase,address,starts,stops,register_address;
 uint32_t ack_count,acks[32],fail_address,fail_data,arbitration,stalled,stop_stalled;
 uint8_t data[256],pointer;
 uint64_t ns;
 uint32_t freeze_time,domain,domain_fail,domain_put_fail,consumer_busy;
 struct i9100_irq_cascade *cascade;struct i9100_cascade_lease *lease;
 unsigned handler_count,clear_source,handler_error;
};
static struct model m;
static int lock(void *c){struct model *p=c;if(p->locked)return -1;p->locked=1;return 0;}
static void unlock(void *c){struct model *p=c;assert(p->locked);p->locked=0;}
static uint32_t enter(void *c){struct model *p=c;assert(!p->locked);p->locked=1;return p->irq_saved++;}
static void leave(void *c,uint32_t saved){struct model *p=c;(void)saved;assert(p->locked);p->locked=0;}
static uint64_t now(void *c){return ((struct model *)c)->ns;}
static int delay(void *c,uint32_t us){struct model *p=c;assert(p->locked);if(!p->freeze_time)p->ns+=us*1000ULL;return 0;}
static uint32_t read_generic(void *c,unsigned region,unsigned off)
{struct model *p=c;assert(p->locked&&region<3U&&off<65536U&&!(off&3U));p->reads++;return p->regs[region][off/4];}
static void write_generic(void *c,unsigned region,unsigned off,uint32_t value)
{struct model *p=c;assert(p->locked&&region<3U&&off<65536U&&!(off&3U));p->writes++;if(p->fail_write!=p->writes)p->regs[region][off/4]=value;}
static void board(void)
{
 memset(&m,0,sizeof(m));struct i9100_gpio_broker b={0};
 struct i9100_gpio_io io={&m,read_generic,write_generic,lock,unlock};
 assert(!i9100_u1_profile_validate());assert(I9100_BANK_COUNT==37);
 assert(!i9100_gpio_broker_init(&b,&io,i9100_u1_banks,I9100_BANK_COUNT));
 assert(!m.reads&&!m.writes);assert(!i9100_gpio_broker_enable(&b,1));
 struct i9100_gpio_lease l={0};
 for(unsigned n=0;n<I9100_BANK_COUNT;n++)for(unsigned pin=0;pin<i9100_u1_banks[n].pins;pin++){
  if(i9100_u1_banks[n].reserved_mask&(1U<<pin))assert(i9100_gpio_claim(&b,n,1U<<pin,&l)==I9100_GPIO_RESERVED);
 }
 assert(!i9100_gpio_claim(&b,I9100_GPZ,31U,&l));
 assert(!i9100_gpio_function(&l,0,2));assert(m.regs[2][0]==2U);
 assert(!i9100_gpio_release(&l,I9100_GPIO_RESTORE));assert(!m.regs[2][0]);
 assert(i9100_u1_i2c[0].sda==2&&i9100_u1_i2c[0].scl==3);
 assert(i9100_u1_i2c[1].physical==0x138c0000U&&i9100_u1_i2c[1].function==4);
 uint32_t allowed[8];i9100_u1_irq_whitelist(allowed);
 assert(allowed[108/32]&(1U<<(108%32)));
 const unsigned reserved[]={89,107,109,52,86,87,111,113};
 for(unsigned n=0;n<sizeof(reserved)/sizeof(reserved[0]);n++)assert(!(allowed[reserved[n]/32]&(1U<<(reserved[n]%32))));
}
static uint32_t i2c_read(void *c,unsigned off)
{
 struct model *p=c;assert(p->locked&&off<=16);p->reads++;
 if(off==0&&p->active&&!(p->regs[0][0]&16U)&&!p->stalled){
  p->regs[0][0]|=16U;p->regs[0][1]&=~9U;
  if(p->arbitration)p->regs[0][1]|=8U;
  else if(p->address_phase){if(p->fail_address)p->regs[0][1]|=1U;p->address_phase=0;}
  else if(p->rx){p->regs[0][3]=p->data[p->pointer++];assert(p->ack_count<32U);p->acks[p->ack_count++]=!!(p->regs[0][0]&128U);}
  else {if(p->fail_data)p->regs[0][1]|=1U;
    else if(p->register_address){p->pointer=(uint8_t)p->regs[0][3];p->register_address=0;}
    else p->data[p->pointer++]=(uint8_t)p->regs[0][3];}
 }
 return p->regs[0][off/4];
}
static void i2c_write(void *c,unsigned off,uint32_t v)
{
 struct model *p=c;assert(p->locked&&off<=16);p->writes++;
 uint32_t old=p->regs[0][off/4];p->regs[0][off/4]=v;
 if(off==4&&(v&32U)){
  p->active=1;p->rx=(v&0xc0U)==0x80U;p->address_phase=1;p->starts++;
  p->register_address=!p->rx;
 }else if(off==0&&!(v&16U)&&!(p->regs[0][1]&32U)&&p->active){
  if(p->stop_stalled)p->regs[0][1]|=32U;
  else{p->active=0;p->stops++;}
 }
 (void)old;
}
static void i2c(void)
{
 struct i9100_i2c_samsung b={0};memset(&m,0,sizeof(m));
 struct i9100_i2c_samsung_io io={&m,i2c_read,i2c_write,lock,unlock,now,delay};
 assert(!i9100_i2c_samsung_init(&b,&io,100000000U,100000U));assert(!m.reads&&!m.writes);
 assert(b.bus_hz<=100000U);assert(i9100_i2c_samsung_enable(&b,1,0)==I9100_I2C_BUSY);
 assert(!i9100_i2c_samsung_enable(&b,1,1));
 uint8_t pointer=0x20,readbuf[3]={0};m.data[0x20]=1;m.data[0x21]=2;m.data[0x22]=3;
 struct i9100_i2c_message msg[2]={{0x19,0,1,&pointer},{0x19,1,3,readbuf}};unsigned done=0;
 assert(!i9100_i2c_samsung_transfer(&b,msg,2,&done));assert(done==2&&m.starts==2&&m.stops==1);
 assert(!memcmp(readbuf,"\1\2\3",3));assert(m.ack_count==3&&m.acks[0]&&m.acks[1]&&!m.acks[2]);
 m.fail_address=1;done=99;assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_ADDRESS_NACK&&done==0);m.fail_address=0;
 m.fail_data=1;assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_DATA_NACK);m.fail_data=0;
 m.arbitration=1;unsigned stops=m.stops;assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_ARBITRATION);
 assert(stops==m.stops&&b.fault);assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_IO);
 m.arbitration=m.active=0;m.regs[0][1]&=~40U;
 assert(!i9100_i2c_samsung_enable(&b,0,1));assert(!i9100_i2c_samsung_enable(&b,1,1));
 m.stalled=m.freeze_time=1;b.poll_limit=17;
 assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_TIMEOUT&&b.fault);
 assert(!m.active);m.stalled=0;m.freeze_time=0;
 assert(!i9100_i2c_samsung_enable(&b,0,1));assert(!i9100_i2c_samsung_enable(&b,1,1));
 m.stop_stalled=1;assert(i9100_i2c_samsung_transfer(&b,msg,2,&done)==I9100_I2C_TIMEOUT);
 assert(i9100_i2c_samsung_enable(&b,0,1)==I9100_I2C_BUS); /* retain ownership until actual STOP */
}
static void cascade_write(void *c,unsigned region,unsigned off,uint32_t value)
{
 struct model *p=c;assert(p->locked);p->writes++;if(p->fail_write==p->writes)return;
 if(!region&&off>=0xf40U&&off<=0xf4cU)p->regs[region][off/4]&=~value;
 else if(region&&off%16U==0)p->regs[region][off/4]|=value;
 else if(region&&off%16U==4)p->regs[region][(off-4U)/4]&=~value;
 else p->regs[region][off/4]=value;
}
static int handler(void *c,unsigned child)
{
 struct model *p=c;assert(!p->locked);p->handler_count++;
 assert(i9100_cascade_unregister(p->lease)==I9100_CASCADE_BUSY);
 if(p->clear_source&&p->cascade->kind==I9100_CASCADE_COMBINER)
  p->regs[1][(p->cascade->group/4U*16U+12U)/4]&=~(1U<<(p->cascade->group%4U*8U+child));
 return p->handler_error?-1:0;
}
static void cascade(void)
{
 memset(&m,0,sizeof(m));struct i9100_irq_cascade c={0};struct i9100_cascade_lease l={0};m.cascade=&c;m.lease=&l;
 struct i9100_cascade_io io={&m,read_generic,cascade_write,enter,leave};
 assert(!i9100_cascade_init(&c,&io,I9100_CASCADE_EINT,0,1U<<21));assert(!m.writes);
 assert(!i9100_cascade_enable(&c,1));for(unsigned n=0;n<4;n++)m.regs[0][(0xf00U+n*4U)/4]=255U;
 assert(i9100_cascade_register(&c,4,3,handler,&m,&l)==I9100_CASCADE_RESERVED);
 assert(!i9100_cascade_register(&c,21,3,handler,&m,&l));m.regs[0][0xf48/4]|=(1U<<5)|(1U<<2);m.regs[0][0xf08/4]&=~(1U<<2);
 uint32_t unowned;assert(i9100_cascade_dispatch(&c,&unowned)==1&&unowned==(1U<<18));assert(m.handler_count==1);
 assert(m.regs[0][0xf48/4]&(1U<<2));assert(!i9100_cascade_unregister(&l));assert(m.regs[0][0xf08/4]&(1U<<5));
 memset(&c,0,sizeof(c));memset(&m,0,sizeof(m));m.cascade=&c;m.lease=&l;
 assert(!i9100_cascade_init(&c,&io,I9100_CASCADE_COMBINER,5,1U<<6));assert(!i9100_cascade_enable(&c,1));
 assert(!i9100_cascade_register(&c,6,0,handler,&m,&l));
 m.regs[1][0x1c/4]=1U<<14;m.clear_source=0;
 assert(i9100_cascade_dispatch(&c,&unowned)==1);assert(c.slots[6].fault&&!(m.regs[1][0x10/4]&(1U<<14)));
 assert(!i9100_cascade_unregister(&l));assert(!i9100_cascade_enable(&c,0));
}
static uint32_t aud_read(void *c,unsigned off){return read_generic(c,0,off);}
static void aud_write(void *c,unsigned off,uint32_t value){write_generic(c,0,off,value);}
static int domain_get(void *c){struct model *p=c;if(p->domain_fail)return -1;p->domain++;return 0;}
static int domain_put(void *c){struct model *p=c;if(p->domain_put_fail)return -1;assert(p->domain);p->domain--;return 0;}
static int consumer_idle(void *c){return ((struct model *)c)->consumer_busy?-1:0;}
static void audss(void)
{
 memset(&m,0,sizeof(m));struct i9100_audss a={0};
 struct i9100_audss_io io={&m,aud_read,aud_write,lock,unlock,domain_get,domain_put,consumer_idle};
 struct i9100_audss_rates rates={24000000,192000000,12288000,12288000,0,0};
 assert(!i9100_audss_init(&a,&io,&rates));assert(i9100_audss_acquire(&a,0)==I9100_AUDSS_DISABLED);
 assert(!m.writes&&!m.reads);assert(!i9100_audss_enable(&a,1));
 m.regs[0][0]=8U;m.regs[0][2]=0x84U;
 assert(!i9100_audss_acquire(&a,0));assert(a.rates.i2s==12288000&&a.rates.bus==24000000);
 assert(m.regs[0][2]==0xccU);m.consumer_busy=1;assert(i9100_audss_release(&a)==I9100_AUDSS_BUSY);
 m.consumer_busy=0;assert(!i9100_audss_release(&a));assert(m.regs[0][2]==0x84U&&!m.domain);
 struct i9100_audss_config conf={1,2,2,2,1};
 assert(i9100_audss_acquire(&a,&conf)==I9100_AUDSS_BUSY&&!m.domain);
 m.regs[0][2]=0;assert(!i9100_audss_acquire(&a,&conf));assert(a.rates.bus==48000000&&a.rates.i2s==12288000);
 m.domain_put_fail=1;assert(i9100_audss_release(&a)==I9100_AUDSS_IO&&a.domain_owned);
 m.domain_put_fail=0;assert(!i9100_audss_release(&a)&&!m.domain);assert(m.regs[0][0]==8U&&!m.regs[0][1]);
 m.domain_fail=1;assert(i9100_audss_acquire(&a,&conf)==I9100_AUDSS_IO&&!m.domain);
 m.domain_fail=0;m.regs[0][0]=12U;assert(i9100_audss_acquire(&a,0)==I9100_AUDSS_CLOCK&&!m.domain);
}
int main(void)
{board();i2c();cascade();audss();puts("PASS U1 reserved pins/IRQ IDs; Samsung I2C repeated-start/NACK/arbitration/STOP/clock stalls; EINT/combiner ownership/storm; AUDSS source/divider/domain lifecycle");return 0;}
