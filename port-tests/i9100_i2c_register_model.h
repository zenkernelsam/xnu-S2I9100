/* SPDX-License-Identifier: BSD-2-Clause; controller/slave boundary stand-in. */
#ifndef I9100_I2C_REGISTER_MODEL_H
#define I9100_I2C_REGISTER_MODEL_H
#include <assert.h>
#include <stdint.h>
#include <string.h>
namespace i9100_i2c_card_model {
struct host {
 uint32_t regs[5],locked,reads,writes,fail_write,drop_register,drop_value,drop_enabled;
 uint32_t active,rx,address_phase,register_address,address,starts,stops;
 uint32_t acked_reads,nacked_reads,address_nack,data_nack,arbitration,stalled,stop_stalled;
 uint32_t slave_drop,slave_address,slave_register,slave_value;
 uint8_t data[128][256],pointer;
};
static host hosts[3];
static uint32_t read(host *h,unsigned off)
{
 assert(h->locked&&off<=16&&!(off&3U));h->reads++;
 if(off==0&&h->active&&!(h->regs[0]&16U)&&!h->stalled){
  h->regs[0]|=16U;h->regs[1]&=~9U;
  if(h->arbitration)h->regs[1]|=8U;
  else if(h->address_phase){
   h->address=h->regs[3]>>1;assert(h->address<128U);
   if(h->address_nack)h->regs[1]|=1U;h->address_phase=0;
  }else if(h->rx){
   uint8_t pointer=h->pointer++;h->regs[3]=h->data[h->address][pointer];
   if((h->address==0x19||h->address==0x69)&&pointer==0x2d)h->data[h->address][0x27]&=~0x88U;
   if(h->address==0x0c&&pointer==9){h->data[0x0c][2]=0;h->data[0x0c][0x0a]=0;}
   if(h->regs[0]&128U)h->acked_reads++;else h->nacked_reads++;
  }else if(h->data_nack)h->regs[1]|=1U;
  else if(h->register_address){
   h->pointer=(uint8_t)h->regs[3];
   if(h->address==0x19||h->address==0x69)h->pointer&=127U;
   h->register_address=0;
  }else{
   uint8_t pointer=h->pointer++;uint8_t value=(uint8_t)h->regs[3];
   if(!(h->slave_drop&&h->address==h->slave_address&&pointer==h->slave_register&&value==h->slave_value)){
    h->data[h->address][pointer]=value;if(h->address==0x0c&&pointer==0x0a)h->data[0x0c][2]=0;
   }
  }
 }
 return h->regs[off/4];
}
static void write(host *h,unsigned off,uint32_t value)
{
 assert(h->locked&&off<=16&&!(off&3U));h->writes++;
 if(h->writes==h->fail_write||(h->drop_enabled&&off==h->drop_register&&value==h->drop_value))return;
 h->regs[off/4]=value;
 if(off==4&&(value&32U)){
  h->active=1;h->rx=(value&0xc0U)==0x80U;h->address_phase=1;
  h->register_address=!h->rx;h->starts++;
 }else if(off==0&&!(value&16U)&&!(h->regs[1]&32U)&&h->active){
  if(h->stop_stalled)h->regs[1]|=32U;else{h->active=0;h->stops++;}
 }
}
static void reset()
{
 memset(hosts,0,sizeof(hosts));
 for(unsigned n=0;n<3;n++){
  hosts[n].regs[0]=0x82;hosts[n].regs[1]=0x80;
  hosts[n].regs[2]=0x5a;hosts[n].regs[4]=3;
  hosts[n].data[0x19][0x0f]=0x33;hosts[n].data[0x69][0x0f]=0xd3;
  hosts[n].data[0x0c][0]=0x48;
  hosts[n].data[0x0c][0x10]=hosts[n].data[0x0c][0x11]=hosts[n].data[0x0c][0x12]=128;
 }
}
}
#endif
