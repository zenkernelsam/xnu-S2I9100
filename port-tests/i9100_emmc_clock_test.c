/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_clock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct clock_model {uint32_t first[40],second[40],field_reads[40],reads,host_reads;uint64_t began,ended;};
static struct clock_model m;
static uint32_t read32(void *cookie,unsigned region,unsigned off)
{
 assert(cookie==&m&&off!=EM_DATA);m.reads++;
 for(unsigned f=0;f<40;f++)if(i9100_emmc_clock_region(f)==region&&i9100_emmc_clock_offset(f)==off){
  if(region==EC_HOST)m.host_reads++;return m.field_reads[f]++?m.second[f]:m.first[f];
 }assert(0);return 0;
}
static uint64_t time_now(void *cookie){assert(cookie==&m);return m.reads?m.ended:m.began;}
static void setup()
{
 memset(&m,0,sizeof(m));m.began=1000000000ULL;m.ended=m.began+3000;
 m.first[EC_CHIP_ID]=0x43210011;m.first[EC_IP_GATE]=1U<<9;m.first[EC_SOURCE_GATE]=1U<<16;
 m.first[EC_SOURCE_MUX]=6U<<16;m.first[EC_SOURCE_DIV]=0x500U;
 m.first[EC_CPU_SOURCE]=1U<<8;m.first[EC_MPLL0]=0xa0960301U;
 m.first[EC_GPK0_CON]=0x3133;m.first[EC_GPK0_DAT]=4;m.first[EC_GPK0_DRV]=0xaa;
 m.first[EC_POWER]=1;m.first[EC_CLKDIV]=3;m.first[EC_CLKENA]=1;
 m.first[EC_CLKSEL]=0x10001;m.first[EC_VER]=0x5342220a;m.first[EC_HCON]=1U<<7;
 memcpy(m.second,m.first,sizeof(m.first));
}
static void save(const char *folder,const char *name,const void *b,unsigned n)
{char path[1024];int length=snprintf(path,sizeof(path),"%s/%s",folder,name);assert(length>0&&(unsigned)length<sizeof(path));FILE *f=fopen(path,"wb");assert(f&&fwrite(b,1,n,f)==n&&!fclose(f));}
int main(int argc,char **argv)
{
 assert(argc==2);const struct i9100_emmc_clock_io io={&m,read32,time_now,1};
 uint8_t wire[512],untouched[512];struct i9100_emmc_clock_result r,decoded;
 setup();memset(wire,0x55,512);memcpy(untouched,wire,512);
 assert(i9100_emmc_clock_capture(0,&io,24000000,24000000,wire,&r)==I9100_EMMC_UNSUPPORTED&&!m.reads&&!memcmp(wire,untouched,512));
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r));
 assert(r.parent_hz==1200000000&&r.fsys_hz==200000000&&r.ciu_hz==100000000&&r.card_hz==16666666&&r.idle&&r.pins_legacy&&m.reads==80);
 assert(!i9100_emmc_clock_decode(wire,512,&decoded)&&!memcmp(&r,&decoded,sizeof(r)));save(argv[1],"clock-mpll.bin",wire,512);
 wire[204]^=1;decoded=r;assert(i9100_emmc_clock_decode(wire,512,&decoded)==I9100_EMMC_ARGUMENT&&!memcmp(&r,&decoded,sizeof(r)));wire[204]^=1;
 setup();m.first[EC_CLKENA]=m.second[EC_CLKENA]=0;
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.card_hz==0&&r.ciu_hz==100000000);
 save(argv[1],"clock-card-disabled.bin",wire,512);
 setup();m.first[EC_CLKSRC]=m.second[EC_CLKSRC]=1;
 m.first[EC_CLKDIV]=m.second[EC_CLKDIV]=3U|(5U<<8);
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.host_div==5&&r.card_hz==10000000);
 save(argv[1],"clock-card-source1.bin",wire,512);
 setup();assert(!i9100_emmc_clock_rate(&io,24000000,24000000,&r)&&r.ciu_hz==100000000&&!r.card_hz);
 assert(m.reads==30&&!m.host_reads); /* CIU rate never claims unsampled host state. */
 setup();m.second[EC_MPLL0]^=1U<<16;
 assert(i9100_emmc_clock_rate(&io,24000000,24000000,&r)==I9100_EMMC_CLOCK&&r.error_field==EC_MPLL0&&!m.host_reads);
 setup();m.second[EC_SOURCE_MUX]^=1U; /* Other SD/WiFi mux is unrelated. */
 assert(!i9100_emmc_clock_rate(&io,24000000,24000000,&r)&&!m.host_reads);
 for(unsigned source=0;source<=1;source++){
  setup();m.first[EC_SOURCE_MUX]=m.second[EC_SOURCE_MUX]=source<<16;m.first[EC_SOURCE_DIV]=m.second[EC_SOURCE_DIV]=0;
  assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.ciu_hz==12000000);
 }
 // Inherited MPLL bypass must use the board reference, never its stale PLL.
 setup();m.first[EC_CPU_SOURCE]=m.second[EC_CPU_SOURCE]=0;m.first[EC_SOURCE_DIV]=m.second[EC_SOURCE_DIV]=0;
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.ciu_hz==12000000);
 setup();m.first[EC_SOURCE_MUX]=m.second[EC_SOURCE_MUX]=7U<<16;m.first[EC_SOURCE_DIV]=m.second[EC_SOURCE_DIV]=0;
 m.first[EC_TOP0]=m.second[EC_TOP0]=1U<<4;m.first[EC_EPLL0]=m.second[EC_EPLL0]=0xa05a0302U;
 m.first[EC_EPLL1]=m.second[EC_EPLL1]=32768;
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.parent_hz==181000000&&r.ciu_hz==90500000);save(argv[1],"clock-epll.bin",wire,512);
 setup();m.first[EC_SOURCE_MUX]=m.second[EC_SOURCE_MUX]=8U<<16;m.first[EC_SOURCE_DIV]=m.second[EC_SOURCE_DIV]=0;
 m.first[EC_TOP0]=m.second[EC_TOP0]=1U<<8;m.first[EC_VPLL0]=m.second[EC_VPLL0]=0xa0320301U;
 m.first[EC_VPLL1]=m.second[EC_VPLL1]=512;
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.parent_hz==202000000&&r.ciu_hz==101000000);save(argv[1],"clock-vpll.bin",wire,512);
 for(unsigned fault=0;fault<15;fault++){
  setup();uint32_t xtal=24000000;
  if(fault==0)m.second[EC_SOURCE_DIV]^=1U;
  if(fault==1)m.second[EC_MPLL0]^=1U<<16;
  if(fault==2)m.first[EC_DIV_STATUS]=m.second[EC_DIV_STATUS]=1;
  if(fault==3)m.first[EC_STATUS]=m.second[EC_STATUS]=1U<<9;
  if(fault==4)m.first[EC_GIC_ENABLE]=m.second[EC_GIC_ENABLE]=1U<<13;
  if(fault==5)m.first[EC_IDINTEN]=m.second[EC_IDINTEN]=1;
  if(fault==6)m.first[EC_CTRL]=m.second[EC_CTRL]=1U<<25;
  if(fault==7)m.first[EC_GPK0_CON]=m.second[EC_GPK0_CON]=0x3333;
  if(fault==8)m.first[EC_MPLL0]=m.second[EC_MPLL0]=0x20960301;
  if(fault==9)m.first[EC_SOURCE_MUX]=m.second[EC_SOURCE_MUX]=3U<<16;
  if(fault==10)m.first[EC_IP_GATE]=m.second[EC_IP_GATE]=0;
  if(fault==11)m.first[EC_POWER]=m.second[EC_POWER]=0;
  if(fault==12)m.first[EC_HCON]=m.second[EC_HCON]=7U<<7;
  if(fault==13)m.ended=m.began-1U;
  if(fault==14)xtal=26000000;
  int e=i9100_emmc_clock_capture(1,&io,xtal,24000000,wire,&r);assert(e&&e==r.error&&r.error_field<40);
  assert(i9100_emmc_clock_decode(wire,512,&decoded)==e&&!memcmp(&decoded,&r,sizeof(r)));
  char name[64];snprintf(name,sizeof(name),"clock-fault-%u.bin",fault);save(argv[1],name,wire,512);
  if(fault==10)assert(!m.host_reads); // Gated host is never touched.
 }
 setup();m.second[EC_SOURCE_MUX]^=1U; // Other SD/WiFi source field, not MMC4.
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r));
 setup();m.first[EC_CLKSEL]=m.second[EC_CLKSEL]=0x07010001U;
 assert(!i9100_emmc_clock_capture(1,&io,24000000,24000000,wire,&r)&&r.ciu_hz==100000000); // 4210 fixed/2, not later divider.
 puts("PASS actual clock capture/rate/CRC: MPLL45/EPLL46/VPLL4650C + twoCMUdividers +4210FIXED/2,80 bounded reads,noFIFO/writes,gatedhost/faults/drift/CRC/defaultOFF; register/time/roots explicit models");return 0;
}
