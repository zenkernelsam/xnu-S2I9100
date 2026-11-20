/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_clock.h"
#include <string.h>
static const uint8_t regions[I9100_EMMC_CLOCK_COUNT]={EC_CHIP,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,
 EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_CMU,EC_GPIO,EC_GPIO,EC_GPIO,EC_GPIO,
 EC_GPIO,EC_GPIO,EC_GPIO,EC_GIC,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,
 EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST,EC_HOST};
static const uint32_t offsets[I9100_EMMC_CLOCK_COUNT]={0,0xc940,0xc340,0xc240,0xc54c,0xc64c,
 0xc210,0xc214,0x14200,0x14108,0x1410c,0xc110,0xc114,0xc120,0xc124,
 0x40,0x44,0x48,0x4c,0x60,0x68,0x6c,0x10c,
 EM_CTRL,EM_PWREN,EM_CLKDIV,EM_CLKSRC,EM_CLKENA,EM_CTYPE,EM_INTMASK,EM_CMD,EM_STATUS,EM_RINTSTS,
 EM_VERID,EM_HCON,EM_UHS,EM_BMOD,EM_IDSTS,EM_IDINTEN,0x9c};
uint32_t i9100_emmc_clock_region(unsigned f){return f<I9100_EMMC_CLOCK_COUNT?regions[f]:UINT32_MAX;}
uint32_t i9100_emmc_clock_offset(unsigned f){return f<I9100_EMMC_CLOCK_COUNT?offsets[f]:UINT32_MAX;}
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8U*i));}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static uint32_t crc(const uint8_t *p,unsigned n){uint32_t c=~0U;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return c^~0U;}
static int fail(struct i9100_emmc_clock_result *r,int e,unsigned f){r->error=e;r->error_field=f;return e;}
static int pll(uint32_t reference,uint32_t con0,uint32_t con1,unsigned type,uint32_t *out)
{
 if(con0==UINT32_MAX||con1==UINT32_MAX||(con0&0xa0000000U)!=0xa0000000U)return I9100_EMMC_CLOCK;
 uint32_t m=(con0>>16)&(type==0?1023U:511U),p=(con0>>8)&63U,s=con0&7U;
 if(!m||!p||(!type&&!s))return I9100_EMMC_CLOCK;
 uint64_t rate=(uint64_t)reference*m;
 if(!type)rate/=((uint64_t)p<<(s-1U));
 else{
  unsigned frac=type==1U?16U:10U;uint32_t k=con1&(type==1U?65535U:4095U);
  rate=(uint64_t)reference*((m<<frac)+k);rate/=((uint64_t)p<<s);rate>>=frac;
 }
 if(rate<10000000ULL||rate>1600000000ULL)return I9100_EMMC_CLOCK;*out=(uint32_t)rate;return 0;
}
static int derive(const uint32_t v[I9100_EMMC_CLOCK_COUNT],uint32_t xxti,uint32_t usb,struct i9100_emmc_clock_result *r)
{
 if(xxti!=24000000U||usb!=24000000U)return fail(r,I9100_EMMC_CLOCK,EC_CPU_SOURCE);
 if(v[EC_CHIP_ID]==UINT32_MAX||(v[EC_CHIP_ID]&0xfffe0000U)!=(0x43210000U&0xfffe0000U))return fail(r,I9100_EMMC_OWNER,EC_CHIP_ID);
 const unsigned must[]={EC_IP_GATE,EC_SOURCE_GATE,EC_SOURCE_MUX,EC_SOURCE_DIV,EC_DIV_STATUS,EC_TOP0,EC_TOP1,EC_CPU_SOURCE};
 for(unsigned i=0;i<sizeof(must)/sizeof(must[0]);i++)if(v[must[i]]==UINT32_MAX)return fail(r,I9100_EMMC_IO,must[i]);
 if(!(v[EC_IP_GATE]&(1U<<9))||!(v[EC_SOURCE_GATE]&(1U<<16))||v[EC_DIV_STATUS])return fail(r,I9100_EMMC_CLOCK,EC_DIV_STATUS);
 unsigned source=(v[EC_SOURCE_MUX]>>16)&15U;r->selected_source=source;uint32_t parent=0;int e=0;
 switch(source){
 case 0:parent=xxti;break;case 1:parent=usb;break;
 case 6:if(v[EC_CPU_SOURCE]&(1U<<8))e=pll(xxti,v[EC_MPLL0],v[EC_MPLL1],0,&parent);else parent=xxti;break;
 case 7:if(v[EC_TOP0]&(1U<<4))e=pll(xxti,v[EC_EPLL0],v[EC_EPLL1],1,&parent);else parent=xxti;break;
 case 8:{
  /* Vendor and mainline agree on VPLL reference mux; source1's frequency
   * differs across reference generations, so do not invent HDMI rate. */
  if(v[EC_TOP1]&1U)return fail(r,I9100_EMMC_UNSUPPORTED,EC_TOP1);
  if(v[EC_TOP0]&(1U<<8))e=pll(xxti,v[EC_VPLL0],v[EC_VPLL1],2,&parent);else parent=xxti;
  break;}
 default:return fail(r,I9100_EMMC_UNSUPPORTED,EC_SOURCE_MUX);
 }
 if(e)return fail(r,e,source==6?EC_MPLL0:source==7?EC_EPLL0:EC_VPLL0);
 r->parent_hz=parent;r->cmu_div=(v[EC_SOURCE_DIV]&15U)+1U;r->cmu_pre=((v[EC_SOURCE_DIV]>>8)&255U)+1U;
 r->fsys_hz=parent/r->cmu_div/r->cmu_pre;r->ciu_hz=r->fsys_hz/2U;
 if(r->ciu_hz<400000U||r->ciu_hz>200000000U)return fail(r,I9100_EMMC_CLOCK,EC_SOURCE_DIV);
 if(v[EC_CLKDIV]!=UINT32_MAX&&v[EC_CLKSRC]!=UINT32_MAX){
  r->host_div=(v[EC_CLKDIV]>>(8U*(v[EC_CLKSRC]&3U)))&255U;
  if(v[EC_CLKENA]!=UINT32_MAX&&(v[EC_CLKENA]&1U))
   r->card_hz=r->host_div?r->ciu_hz/(r->host_div*2U):r->ciu_hz;
 }
 return 0;
}
static uint32_t stable_mask(unsigned f,unsigned source)
{
 switch(f){
 case EC_IP_GATE:return 1U<<9;case EC_SOURCE_GATE:return 1U<<16;case EC_SOURCE_MUX:return 15U<<16;
 case EC_SOURCE_DIV:return 0xff0fU;case EC_DIV_STATUS:return UINT32_MAX;
 case EC_TOP0:return source==7?1U<<4:source==8?1U<<8:0;case EC_TOP1:return source==8?1U:0;
 case EC_CPU_SOURCE:return source==6?1U<<8:0;
 case EC_MPLL0:return source==6?0xa3ff3f07U:0;case EC_MPLL1:return 0;
 case EC_EPLL0:return source==7?0xa1ff3f07U:0;case EC_EPLL1:return source==7?65535U:0;
 case EC_VPLL0:return source==8?0xa1ff3f07U:0;case EC_VPLL1:return source==8?4095U:0;
 case EC_GPK0_CON:return 0xffffU;case EC_GPK0_DAT:return 4U;case EC_GPK0_PUD:case EC_GPK0_DRV:return 255U;
 case EC_GPK1_CON:case EC_GPK1_PUD:case EC_GPK1_DRV:return 0;
 case EC_GIC_ENABLE:return 1U<<13;case EC_RINT:case EC_IDSTS:return 0;
 default:return UINT32_MAX;
 }
}
static int analyze(const uint32_t first[I9100_EMMC_CLOCK_COUNT],const uint32_t second[I9100_EMMC_CLOCK_COUNT],
 uint32_t xxti,uint32_t usb,uint64_t began,uint64_t ended,struct i9100_emmc_clock_result *r)
{
 memset(r,0,sizeof(*r));r->error_field=UINT32_MAX;
 if(ended<began||ended-began>100000000ULL)return fail(r,I9100_EMMC_TIMEOUT,EC_STATUS);
 int e=derive(second,xxti,usb,r);if(e)return e;
 for(unsigned f=0;f<I9100_EMMC_CLOCK_COUNT;f++){
  uint32_t mask=stable_mask(f,r->selected_source);
  if(mask&&((first[f]^second[f])&mask))return fail(r,I9100_EMMC_CLOCK,f);
 }
 r->source_stable=1;
 if(second[EC_GIC_ENABLE]==UINT32_MAX)return fail(r,I9100_EMMC_IO,EC_GIC_ENABLE);
 const unsigned host[]={EC_CTRL,EC_POWER,EC_CLKDIV,EC_CLKSRC,EC_CLKENA,EC_CTYPE,EC_INTMASK,EC_CMD,EC_STATUS,
  EC_VER,EC_HCON,EC_UHS,EC_BMOD,EC_IDINTEN,EC_CLKSEL};
 for(unsigned i=0;i<sizeof(host)/sizeof(host[0]);i++)if(second[host[i]]==UINT32_MAX)return fail(r,I9100_EMMC_IO,host[i]);
 if((second[EC_VER]&65535U)<0x2000U||(second[EC_VER]&65535U)>=0x240aU||((second[EC_HCON]>>7)&7U)!=1U)return fail(r,I9100_EMMC_UNSUPPORTED,EC_HCON);
 if(!(second[EC_POWER]&1U))return fail(r,I9100_EMMC_NO_DEVICE,EC_POWER);
 if(second[EC_CTRL]&(7U|(1U<<5)|(1U<<25))||second[EC_CMD]&(1U<<31)||
    second[EC_STATUS]&((15U<<4)|(1U<<9)|(1U<<10)|(0x1fffU<<17)|(1U<<30)|(1U<<31))||
    second[EC_BMOD]&(1U<<7)||second[EC_INTMASK]||second[EC_IDINTEN]||second[EC_GIC_ENABLE]&(1U<<13))return fail(r,I9100_EMMC_BUSY,EC_STATUS);
 r->idle=1;
 if(second[EC_GPK0_CON]==UINT32_MAX||second[EC_GPK0_DAT]==UINT32_MAX||second[EC_GPK0_PUD]==UINT32_MAX||second[EC_GPK0_DRV]==UINT32_MAX||
    (second[EC_GPK0_CON]&65535U)!=0x3133U||!(second[EC_GPK0_DAT]&4U))return fail(r,I9100_EMMC_OWNER,EC_GPK0_CON);
 r->pins_legacy=1;return 0;
}
int i9100_emmc_clock_decode(const uint8_t *raw,unsigned bytes,struct i9100_emmc_clock_result *out)
{
 if(!raw||!out||bytes!=I9100_EMMC_CLOCK_BYTES||memcmp(raw,"I9EC",4)||get32(raw+4)!=1||get32(raw+8)!=bytes||
    get32(raw+12)!=I9100_EMMC_CLOCK_COUNT||get32(raw+16)&~I9100_EMMC_CLOCK_MODEL||get32(raw+28)||
    get32(raw+508)!=crc(raw,508))return I9100_EMMC_ARGUMENT;
 for(unsigned i=48;i<64;i++)if(raw[i])return I9100_EMMC_ARGUMENT;
 for(unsigned i=384;i<508;i++)if(raw[i])return I9100_EMMC_ARGUMENT;
 uint32_t first[I9100_EMMC_CLOCK_COUNT],second[I9100_EMMC_CLOCK_COUNT];
 for(unsigned f=0;f<I9100_EMMC_CLOCK_COUNT;f++){first[f]=get32(raw+64+f*4U);second[f]=get32(raw+224+f*4U);}
 struct i9100_emmc_clock_result result;int e=analyze(first,second,get32(raw+20),get32(raw+24),get64(raw+32),get64(raw+40),&result);
 *out=result;return e;
}
int i9100_emmc_clock_capture(int enabled,const struct i9100_emmc_clock_io *io,uint32_t xxti,uint32_t usb,uint8_t *out,struct i9100_emmc_clock_result *r)
{
 if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(enabled!=1||!io||!io->read32||!io->now_ns||io->model_fixture>1U||!out||!r)return I9100_EMMC_ARGUMENT;
 uint8_t b[I9100_EMMC_CLOCK_BYTES]={0};memcpy(b,"I9EC",4);put32(b+4,1);put32(b+8,sizeof(b));put32(b+12,I9100_EMMC_CLOCK_COUNT);
 put32(b+16,io->model_fixture);put32(b+20,xxti);put32(b+24,usb);put64(b+32,io->now_ns(io->cookie));
 for(unsigned sample=0;sample<2;sample++){
  uint32_t ip=io->read32(io->cookie,EC_CMU,0xc940),gate=io->read32(io->cookie,EC_CMU,0xc340);
  for(unsigned f=0;f<I9100_EMMC_CLOCK_COUNT;f++){
   uint32_t value=f==EC_IP_GATE?ip:f==EC_SOURCE_GATE?gate:
     regions[f]==EC_HOST&&(!(ip&(1U<<9))||!(gate&(1U<<16))||ip==UINT32_MAX||gate==UINT32_MAX)?UINT32_MAX:
     io->read32(io->cookie,regions[f],offsets[f]);
   put32(b+64+sample*160U+f*4U,value);
  }
 }
 put64(b+40,io->now_ns(io->cookie));put32(b+508,crc(b,508));memcpy(out,b,sizeof(b));return i9100_emmc_clock_decode(b,sizeof(b),r);
}
int i9100_emmc_clock_rate(const struct i9100_emmc_clock_io *io,uint32_t xxti,uint32_t usb,struct i9100_emmc_clock_result *out)
{
 if(!io||!io->read32||!out)return I9100_EMMC_ARGUMENT;
 uint32_t first[I9100_EMMC_CLOCK_COUNT],second[I9100_EMMC_CLOCK_COUNT];
 /* This is a live CIU-only check. Do not synthesize a running card clock
  * from host registers which this read-only operation never sampled. */
 for(unsigned f=0;f<I9100_EMMC_CLOCK_COUNT;f++)first[f]=second[f]=UINT32_MAX;
 for(unsigned f=0;f<=EC_VPLL1;f++)first[f]=io->read32(io->cookie,regions[f],offsets[f]);
 for(unsigned f=0;f<=EC_VPLL1;f++)second[f]=io->read32(io->cookie,regions[f],offsets[f]);
 struct i9100_emmc_clock_result r;memset(&r,0,sizeof(r));r.error_field=UINT32_MAX;int e=derive(second,xxti,usb,&r);
 if(!e)for(unsigned f=0;f<=EC_VPLL1;f++)if((first[f]^second[f])&stable_mask(f,r.selected_source)){
  e=fail(&r,I9100_EMMC_CLOCK,f);break;
 }
 *out=r;return e;
}
