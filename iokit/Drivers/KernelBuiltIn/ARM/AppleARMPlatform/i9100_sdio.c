/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sdio.h"
#include <string.h>
enum {BLOCK_SIZE=4,BLOCK_COUNT=6,ARG=8,TRANSFER=12,COMMAND=14,RESPONSE=16,BUFFER=32,
 PRESENT=36,HOST=40,POWER=41,CLOCK=44,TIMEOUT=46,RESET=47,STATUS=48,INTEN=52,SIGNAL=56,CAPABILITIES=64,CTRL2=128,CTRL3=132,VERSION=254};
#define ERRORS 0xffff8000U
#define FEEDBACK_MASK ((1U<<31)|(1U<<30)|(1U<<15)|(1U<<14)|(3U<<9)|(1U<<8)|(3U<<4))
#define FEEDBACK_VALUE ((1U<<31)|(1U<<30)|(1U<<14)|(1U<<8)|(2U<<4))
#define DELAY_VALUE 0x80808080U
static uint8_t r8(struct i9100_sdio *s,unsigned r){return s->io.read8(s->io.cookie,r);}
static uint16_t r16(struct i9100_sdio *s,unsigned r){return s->io.read16(s->io.cookie,r);}
static uint32_t r32(struct i9100_sdio *s,unsigned r){return s->io.read32(s->io.cookie,r);}
static void w8(struct i9100_sdio *s,unsigned r,uint8_t v){s->io.write8(s->io.cookie,r,v);}
static void w16(struct i9100_sdio *s,unsigned r,uint16_t v){s->io.write16(s->io.cookie,r,v);}
static void w32(struct i9100_sdio *s,unsigned r,uint32_t v){s->io.write32(s->io.cookie,r,v);}
static int expired(struct i9100_sdio *s,uint64_t span)
{uint64_t now=s->io.now_ns(s->io.cookie);return now<s->started||now-s->started>span;}
static int error(struct i9100_sdio *s,uint32_t status)
{s->last_status=status;if(status&((1U<<16)|(1U<<20)))return I9100_SDIO_TIMEOUT;if(status&((1U<<17)|(1U<<21)))return I9100_SDIO_CRC;return I9100_SDIO_IO;}
static int wait_irq(struct i9100_sdio *s,uint32_t mask)
{
 for(unsigned p=0;p<s->poll_limit;p++){
  uint32_t status=r32(s,STATUS);if(status&ERRORS)return error(s,status);
  if(status&mask)return 0;if(expired(s,s->timeout_ns))break;
  if(s->io.delay_us(s->io.cookie,2))return I9100_SDIO_IO;
 }
 return I9100_SDIO_TIMEOUT;
}
static int wait_idle(struct i9100_sdio *s,uint32_t mask)
{
 for(unsigned p=0;p<s->poll_limit;p++){
  uint32_t status=r32(s,STATUS);if(status&ERRORS)return error(s,status);
  if(!(r32(s,PRESENT)&mask))return 0;if(expired(s,s->timeout_ns))break;
  if(s->io.delay_us(s->io.cookie,2))return I9100_SDIO_IO;
 }
 return I9100_SDIO_TIMEOUT;
}
static int command(struct i9100_sdio *s,unsigned index,uint32_t argument,uint16_t flags,uint32_t *response)
{
 s->started=s->io.now_ns(s->io.cookie);s->timeout_ns=100000000ULL;
 int e=wait_idle(s,flags&32U?3U:1U);if(e)return e;
 w32(s,STATUS,0xffffffffU);w32(s,ARG,argument);w16(s,COMMAND,(uint16_t)((index<<8)|flags));
 e=wait_irq(s,1);if(e)return e;w32(s,STATUS,1);
 if(flags&3U){uint32_t r=r32(s,RESPONSE);s->last_response=r;if(response)*response=r;}
 if((flags&3U)==3U){e=wait_idle(s,2);if(!e)w32(s,STATUS,2);}
 return e;
}
static int cmd52(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,uint8_t *value)
{
 if(!value||fn>2U||address>0x1ffffU)return I9100_SDIO_ARGUMENT;
 uint32_t argument=((uint32_t)write<<31)|(fn<<28)|(address<<9)|(write?*value:0U),response=0;
 int e=command(s,52,argument,26,&response);if(e)return e;
 if(response&0xcb00U)return I9100_SDIO_CARD;*value=(uint8_t)response;return 0;
}
static int reset_lines(struct i9100_sdio *s)
{
 w8(s,RESET,6);for(unsigned p=0;p<s->poll_limit;p++){
  if(!(r8(s,RESET)&6U)){w32(s,STATUS,0xffffffffU);return 0;}
  if(s->io.delay_us(s->io.cookie,2))return I9100_SDIO_IO;
 }
 return I9100_SDIO_TIMEOUT;
}
static int release_host(struct i9100_sdio *s)
{
 if(!s->host_owned)return 0;
 w32(s,SIGNAL,0);w16(s,CLOCK,0);w8(s,POWER,0);
 if(r32(s,PRESENT)&7U){int e=reset_lines(s);if(e)return e;if(r32(s,PRESENT)&7U)return I9100_SDIO_BUSY;}
 w32(s,CTRL2,(r32(s,CTRL2)&~FEEDBACK_MASK)|(s->saved_feedback[0]&FEEDBACK_MASK));
 w32(s,CTRL3,s->saved_feedback[1]);
 if((r32(s,CTRL2)&FEEDBACK_MASK)!=(s->saved_feedback[0]&FEEDBACK_MASK)||r32(s,CTRL3)!=s->saved_feedback[1])return I9100_SDIO_IO;
 w32(s,INTEN,s->saved_inten);w32(s,SIGNAL,s->saved_signal);
 s->host_owned=0;return 0;
}
int i9100_sdio_release_host(struct i9100_sdio *s)
{
 if(!s||!s->initialized)return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=s->active?I9100_SDIO_BUSY:release_host(s);if(e&&e!=I9100_SDIO_BUSY)s->fault=1;
 s->io.unlock(s->io.cookie);return e;
}
static int set_clock(struct i9100_sdio *s,uint32_t target)
{
 unsigned divisor=1;
 while(divisor<256U&&s->input_clock_hz/divisor>target)divisor*=2U;
 if(s->input_clock_hz/divisor>target)return I9100_SDIO_ARGUMENT;
 uint16_t field=(uint16_t)((divisor==1U?0U:divisor/2U)<<8);
 w16(s,CLOCK,0);w16(s,CLOCK,field|1U);
 for(unsigned p=0;p<s->poll_limit;p++){
  if(r16(s,CLOCK)&2U){w16(s,CLOCK,field|5U);s->clock_hz=s->input_clock_hz/divisor;return 0;}
  if(s->io.delay_us(s->io.cookie,2))return I9100_SDIO_IO;
 }
 return I9100_SDIO_TIMEOUT;
}
static int cis(struct i9100_sdio *s)
{
 uint32_t pointer=0;for(unsigned n=0;n<3;n++){
  uint8_t v=0;int e=cmd52(s,0,0,0x109U+n,&v);if(e)return e;pointer|=(uint32_t)v<<(n*8U);
 }
 if(!pointer||pointer>0x1ffffU)return I9100_SDIO_CARD;
 unsigned consumed=0;
 for(unsigned tuple=0;tuple<256U&&consumed<4096U;tuple++){
  if(pointer>0x1ffffU)return I9100_SDIO_CARD;
  uint8_t code=0;int e=cmd52(s,0,0,pointer,&code);if(e)return e;pointer++;consumed++;
  if(code==0xffU)return I9100_SDIO_CARD;if(!code)continue;
  if(pointer>0x1ffffU)return I9100_SDIO_CARD;
  uint8_t length=0;e=cmd52(s,0,0,pointer,&length);if(e)return e;pointer++;consumed++;
  if(!length||pointer>0x1ffffU||length>0x20000U-pointer||consumed+length>4096U)return I9100_SDIO_CARD;
  if(code==0x20U){
   if(length!=4U)return I9100_SDIO_CARD;uint8_t bytes[4];
   for(unsigned n=0;n<4;n++){e=cmd52(s,0,0,pointer+n,&bytes[n]);if(e)return e;}
   s->vendor=(uint16_t)(bytes[0]|((uint16_t)bytes[1]<<8));s->device=(uint16_t)(bytes[2]|((uint16_t)bytes[3]<<8));return 0;
  }
  pointer+=length;consumed+=length;
 }
 return I9100_SDIO_CARD;
}
int i9100_sdio_init(struct i9100_sdio *s,const struct i9100_sdio_io *io,uint32_t clock)
{
 if(!s||s->initialized||!io||!io->read8||!io->read16||!io->read32||!io->write8||!io->write16||!io->write32||
    !io->lock||!io->unlock||!io->now_ns||!io->delay_us||clock<1000000U||clock>100000000U)return I9100_SDIO_ARGUMENT;
 memset(s,0,sizeof(*s));s->io=*io;s->input_clock_hz=clock;s->poll_limit=50000;s->initialized=1;return 0;
}
int i9100_sdio_enable(struct i9100_sdio *s,int enabled,int verified)
{
 if(!s||!s->initialized||(enabled!=0&&enabled!=1))return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=0;
 if(!enabled){
  if(!s->active){e=I9100_SDIO_DISABLED;goto done;}
  if(s->fault){e=reset_lines(s);if(e)goto done;
   if(s->last_function){uint8_t fn=s->last_function;e=cmd52(s,1,0,6,&fn);if(e)goto done;}
   s->fault=0;}
  uint8_t mask=s->saved_io_enable;e=cmd52(s,1,0,2,&mask);
  if(e){s->fault=1;goto done;}
  s->active=s->last_function=0;e=release_host(s);if(e)s->fault=1;
  goto done;
 }
 if(!verified){e=I9100_SDIO_ARGUMENT;goto done;}
 if(s->active||s->fault||s->host_owned||(r32(s,PRESENT)&7U)){e=I9100_SDIO_BUSY;goto done;}
 if((r16(s,VERSION)&255U)<1U||!(r32(s,CAPABILITIES)&(1U<<24))){e=I9100_SDIO_CARD;goto done;}
 s->saved_inten=r32(s,INTEN);s->saved_signal=r32(s,SIGNAL);
 s->saved_feedback[0]=r32(s,CTRL2);s->saved_feedback[1]=r32(s,CTRL3);s->host_owned=1;
 w32(s,SIGNAL,0);w32(s,INTEN,0xffffffffU);w32(s,STATUS,0xffffffffU);
 w16(s,CLOCK,0);w32(s,CTRL2,(s->saved_feedback[0]&~FEEDBACK_MASK)|FEEDBACK_VALUE);w32(s,CTRL3,DELAY_VALUE);
 if((r32(s,CTRL2)&FEEDBACK_MASK)!=FEEDBACK_VALUE||r32(s,CTRL3)!=DELAY_VALUE){e=I9100_SDIO_IO;goto failed;}
 e=reset_lines(s);if(e)goto failed;w8(s,TIMEOUT,14U);w8(s,HOST,0);w8(s,POWER,0xfU);
 if(s->io.delay_us(s->io.cookie,25000)){e=I9100_SDIO_IO;goto failed;}
 e=set_clock(s,400000);if(e)goto failed;
 e=command(s,0,0,0,0);if(e)goto failed;
 uint32_t response=0;e=command(s,5,0,2,&response);if(e)goto failed;
 s->functions=(response>>28)&7U;s->ocr=response&0x00ff8000U;
 if(s->functions<2U||!s->ocr){e=I9100_SDIO_CARD;goto failed;}
 for(unsigned p=0;p<100U;p++){
  e=command(s,5,s->ocr,2,&response);if(e)goto failed;
  if(response&0x80000000U)break;
  if(p==99U){e=I9100_SDIO_TIMEOUT;goto failed;}
  if(s->io.delay_us(s->io.cookie,1000)){e=I9100_SDIO_IO;goto failed;}
 }
 e=command(s,3,0,26,&response);if(e)goto failed;
 if(response&0xe000U){e=I9100_SDIO_CARD;goto failed;}
 s->rca=response>>16;if(!s->rca){e=I9100_SDIO_CARD;goto failed;}
 e=command(s,7,s->rca<<16,27,&response);if(e)goto failed;
 if(response&0xfdffe008U){e=I9100_SDIO_CARD;goto failed;}
 e=cmd52(s,0,0,0,&s->revision);if(e)goto failed;
 if((s->revision&15U)>3U){e=I9100_SDIO_CARD;goto failed;}
 e=cmd52(s,0,0,2,&s->saved_io_enable);if(e)goto failed;
 if(s->saved_io_enable&6U){e=I9100_SDIO_BUSY;goto failed;}
 e=cmd52(s,0,0,8,&s->capabilities);if(e)goto failed;
 e=cmd52(s,0,0,7,&s->bus_control);if(e)goto failed;
 uint8_t width=s->bus_control&~3U;e=cmd52(s,1,0,7,&width);if(e)goto failed;
 e=cis(s);if(e)goto failed;s->active=1;goto done;
failed:
 (void)release_host(s);s->fault=1;
done:
 s->io.unlock(s->io.cookie);return e;
}
int i9100_sdio_function(struct i9100_sdio *s,unsigned fn,int enabled)
{
 if(!s||!s->initialized||fn<1U||fn>2U||(enabled!=0&&enabled!=1))return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=0;uint8_t mask=0;
 if(!s->active){e=I9100_SDIO_DISABLED;goto done;}if(s->fault){e=I9100_SDIO_IO;goto done;}
 e=cmd52(s,0,0,2,&mask);if(e)goto done;
 mask=(uint8_t)((mask&~(1U<<fn))|(enabled?(1U<<fn):0U));e=cmd52(s,1,0,2,&mask);if(e)goto done;
 if(enabled){
  unsigned ready=0;for(unsigned p=0;p<100U;p++){
   uint8_t bits=0;e=cmd52(s,0,0,3,&bits);if(e)goto done;
   if(bits&(1U<<fn)){ready=1;break;}if(s->io.delay_us(s->io.cookie,1000)){e=I9100_SDIO_IO;goto done;}
  }
  if(!ready){e=I9100_SDIO_TIMEOUT;goto done;}
  uint16_t size=fn==1U?64U:512U;
  for(unsigned n=0;n<2;n++){uint8_t v=(uint8_t)(size>>(8U*n));e=cmd52(s,1,0,fn*256U+16U+n,&v);if(e)goto done;}
  s->block_size[fn]=size;
 }else s->block_size[fn]=0;
done:
 if(e&&e!=I9100_SDIO_DISABLED)s->fault=1;s->io.unlock(s->io.cookie);return e;
}
int i9100_sdio_cmd52(struct i9100_sdio *s,int write,unsigned fn,uint32_t addr,uint8_t *value)
{
 if(!s||!s->initialized||!value||fn>2U||addr>0x1ffffU||(write!=0&&write!=1))return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=!s->active?I9100_SDIO_DISABLED:s->fault?I9100_SDIO_IO:0;
 if(!e)e=cmd52(s,write,fn,addr,value);
 if(e&&e!=I9100_SDIO_DISABLED)s->fault=1;s->io.unlock(s->io.cookie);return e;
}
int i9100_sdio_set_bus(struct i9100_sdio *s,int four,uint32_t target)
{
 if(!s||!s->initialized||(four!=0&&four!=1)||target<100000U||target>25000000U)return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=0;uint8_t bus=0;
 if(!s->active){e=I9100_SDIO_DISABLED;goto done;}
 if(s->fault){e=I9100_SDIO_IO;goto done;}
 if((s->capabilities&64U)&&((four&&!(s->capabilities&128U))||target>400000U))
  {e=I9100_SDIO_CARD;goto done;}
 e=cmd52(s,0,0,7,&bus);if(e)goto done;
 bus=(uint8_t)((bus&~3U)|(four?2U:0U));e=cmd52(s,1,0,7,&bus);if(e)goto done;
 uint8_t verify=0;e=cmd52(s,0,0,7,&verify);if(e)goto done;
 if((verify&3U)!=(four?2U:0U)){e=I9100_SDIO_CARD;goto done;}
 w8(s,HOST,(uint8_t)((r8(s,HOST)&~2U)|(four?2U:0U)));e=set_clock(s,target);
done:
 if(e&&e!=I9100_SDIO_DISABLED&&e!=I9100_SDIO_ARGUMENT)s->fault=1;
 s->io.unlock(s->io.cookie);return e;
}
int i9100_sdio_cmd53(struct i9100_sdio *s,int write,unsigned fn,uint32_t addr,int increment,uint8_t *bytes,uint32_t length)
{
 if(!s||!s->initialized||!bytes||!length||length>65536U||fn<1U||fn>2U||addr>0x1ffffU||
    (increment!=0&&increment!=1)||(write!=0&&write!=1)||(increment&&length>0x20000U-addr))return I9100_SDIO_ARGUMENT;
 if(s->io.lock(s->io.cookie))return I9100_SDIO_BUSY;
 int e=0;
 if(!s->active){e=I9100_SDIO_DISABLED;goto done;}if(s->fault){e=I9100_SDIO_IO;goto done;}
 uint32_t size=s->block_size[fn];if(!size){e=I9100_SDIO_CARD;goto done;}
 int block=length>512U;
 if(block&&length%size){e=I9100_SDIO_ARGUMENT;goto done;}
 if(block&&!(s->capabilities&2U)){e=I9100_SDIO_CARD;goto done;}
 unsigned blocks=block?length/size:1U,unit=block?size:length,count=block?blocks:(length==512U?0U:length);
 if(count>511U){e=I9100_SDIO_ARGUMENT;goto done;}
 w16(s,BLOCK_SIZE,(uint16_t)unit);w16(s,BLOCK_COUNT,(uint16_t)blocks);
 w16(s,TRANSFER,(uint16_t)(2U|(write?0U:16U)|(blocks>1U?32U:0U)));
 uint32_t argument=((uint32_t)write<<31)|(fn<<28)|((uint32_t)block<<27)|((uint32_t)increment<<26)|(addr<<9)|count,response=0;
 s->last_function=(uint8_t)fn;
 e=command(s,53,argument,58,&response);if(e)goto done;
 s->timeout_ns=3000000000ULL;
 if(response&0xcb00U){e=I9100_SDIO_CARD;goto done;}
 uint32_t cursor=0;
 for(unsigned b=0;b<blocks&&!e;b++){
  uint32_t ready=write?16U:32U;e=wait_irq(s,ready);if(e)break;w32(s,STATUS,ready);
  for(unsigned n=0;n<unit;n+=4U){
   unsigned amount=unit-n<4U?unit-n:4U;uint32_t word=0;
   if(write){for(unsigned j=0;j<amount;j++)word|=(uint32_t)bytes[cursor+j]<<(j*8U);w32(s,BUFFER,word);}
   else{word=r32(s,BUFFER);for(unsigned j=0;j<amount;j++)bytes[cursor+j]=(uint8_t)(word>>(j*8U));}
   cursor+=amount;
  }
 }
 if(!e)e=wait_irq(s,2);if(!e)e=wait_idle(s,7);if(!e){w32(s,STATUS,2);s->last_function=0;}
done:
 if(e&&e!=I9100_SDIO_ARGUMENT&&e!=I9100_SDIO_DISABLED)s->fault=1;
 s->io.unlock(s->io.cookie);return e;
}
