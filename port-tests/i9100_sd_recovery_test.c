#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "i9100_sdmmc.h"

struct Model {
 uint8_t registers[256];uint32_t cid[4];unsigned data_fail,identity_fault,commands,reads,word,state;
 unsigned cmd7_none,cmd10,cmd17,resets;uint64_t ns;
};
static uint32_t read32(void *cookie,unsigned off)
{
 struct Model*m=cookie;uint32_t value;assert(off<256 && !(off&3));
 if(off==0x20){assert(m->word<128);value=0x11223344U+m->word++;
  if(m->word==128){uint32_t bits=3U;memcpy(m->registers+0x30,&bits,4);memset(m->registers+0x24,0,4);}
  return value;}
 memcpy(&value,m->registers+off,4);return value;
}
static void write32(void *cookie,unsigned off,uint32_t value)
{
 struct Model*m=cookie;assert(off<256 && !(off&3));
 if(off==0x30){uint32_t previous;memcpy(&previous,m->registers+off,4);value=previous&~value;}
 memcpy(m->registers+off,&value,4);
}
static uint16_t read16(void *cookie,unsigned off)
{uint16_t value;memcpy(&value,((struct Model*)cookie)->registers+off,2);return value;}
static void write16(void *cookie,unsigned off,uint16_t value)
{
 struct Model*m=cookie;memcpy(m->registers+off,&value,2);
 if(off==0x2c){value|=2U;memcpy(m->registers+off,&value,2);return;}
 if(off!=0x0e)return;
 unsigned command=value>>8,flags=value&255;uint32_t arg,status=1,response=0x900;
 memcpy(&arg,m->registers+8,4);m->commands++;
 if(command==17){m->reads++;m->cmd17++;m->word=0;
  if(m->data_fail)status=1U<<21;else status=1U|1U<<5;
 }else if(command==12){m->state=4;if(m->identity_fault==1)status=1U<<16;}
 else if(command==13){response=(m->state<<9)|1U<<8;if(m->identity_fault==1)status=1U<<16;if(m->identity_fault==6)response=0x600;}
 else if(command==7 && !arg){assert((flags&3U)==0);m->cmd7_none++;m->state=3;if(m->identity_fault==2)status=1U<<16;}
 else if(command==7){assert(arg==0x12340000U && (flags&3U)==3U);m->state=4;if(m->identity_fault==5)status=1U<<16;}
 else if(command==10){assert(arg==0x12340000U && (flags&3U)==1U);m->cmd10++;
  if(m->identity_fault==3)status=1U<<17;
  for(unsigned n=0;n<4;n++){uint32_t word=m->cid[n]+(m->identity_fault==4 && n==0);memcpy(m->registers+0x10+n*4,&word,4);}
 }else assert(0);
 if(command!=10)memcpy(m->registers+0x10,&response,4);
 memcpy(m->registers+0x30,&status,4);
}
static uint8_t read8(void *cookie,unsigned off){return ((struct Model*)cookie)->registers[off];}
static void write8(void *cookie,unsigned off,uint8_t value)
{struct Model*m=cookie;m->registers[off]=value;if(off==0x2f){m->registers[off]=0;memset(m->registers+0x24,0,4);m->resets++;}}
static int delay(void *cookie,uint32_t us){((struct Model*)cookie)->ns+=(uint64_t)us*1000U;return 1;}
static uint64_t now(void *cookie,int unused){(void)unused;return ((struct Model*)cookie)->ns;}
static void init(struct Model*m,struct i9100_sd_card*c)
{
 memset(m,0,sizeof(*m));m->state=4;m->ns=1;
 for(unsigned n=0;n<4;n++)m->cid[n]=0xa1234000U+n;
 struct i9100_sd_io io={m,read32,write32,read16,write16,read8,write8,delay,now,8};
 i9100_sd_init(c,&io);c->ready=c->sdhc=1;c->sectors=100000;c->rca=0x1234;c->clock_div=2;
 memcpy(c->cid,m->cid,sizeof(c->cid));
}
int main(void)
{
 alarm(10);struct Model m;struct i9100_sd_card c;uint8_t bytes[512];
 init(&m,&c);memset(bytes,0xa5,512);m.data_fail=1;
 assert(i9100_sd_read512(&c,20,bytes)==I9100_SD_CRC && c.identity_check_pending);
 assert(m.cmd17==4 && c.read_errors==4 && c.clock_div==128 && c.ready);
 for(unsigned n=0;n<512;n++)assert(bytes[n]==0xa5);
 m.data_fail=0;unsigned previous=m.cmd17;
 assert(i9100_sd_read512(&c,20,bytes)==0 && !c.identity_check_pending && c.ready);
 assert(m.cmd17==previous+1 && m.cmd7_none==1 && m.cmd10==1);
 assert(bytes[0]==0x44 && bytes[1]==0x33 && bytes[511]==0x11);
 assert(i9100_sd_read512(&c,21,bytes)==0 && m.cmd10==1); /* Healthy path has no re-probe. */
 for(unsigned fault=1;fault<=6;fault++){
  init(&m,&c);c.identity_check_pending=1;m.identity_fault=fault;memset(bytes,0xa5,512);
  assert(i9100_sd_read512(&c,20,bytes)<0 && !c.ready && c.identity_check_pending && !m.cmd17);
  unsigned commands=m.commands;assert(i9100_sd_read512(&c,20,bytes)==I9100_SD_NODEV && m.commands==commands);
  for(unsigned n=0;n<512;n++)assert(bytes[n]==0xa5);
 }
 init(&m,&c);c.identity_check_pending=1;
 assert(i9100_sd_read512(&c,100000,bytes)==I9100_SD_ARG && !m.commands);
 puts("PASS production SD request failure remains an error; next request verifies original CMD10 CID and reselects; changed/failed card is retired; healthy reads avoid re-probe; no failed data delivery");
}
