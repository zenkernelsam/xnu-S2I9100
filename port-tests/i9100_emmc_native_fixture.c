/* SPDX-License-Identifier: BSD-2-Clause; register/media boundary only. */
#define I9100_EMMC_WRITER_MAIN legacy_writer_fixture_main
#include "i9100_emmc_writer_test.c"
static unsigned maps_started;
static uint8_t hfs_system[512],hfs_data[512];
static struct i9100_emmc_writer_identity expected_identity;
static struct {unsigned enabled,switches,busy,width,timing,power_class,failure;} sdr_fixture;
static struct {unsigned enabled,requests,failure;uint32_t desc_pa,data_pa;uint8_t *descriptors,*data;} dma_fixture;
static void make_hfs(uint8_t h[512],uint32_t bytes_blocks,int journaled)
{memset(h,0,512);h[0]='H';h[1]='X';h[3]=5;big32(h+4,0x100U|(journaled?0x2000U:0));
 big32(h+40,8192);big32(h+44,bytes_blocks);big32(h+48,1U);}
int fixture_setup(const char *source)
{
 if(wm.d.head)return -1;expected_identity=setup_writer(source,1);
 wm.m.count=wm.m.position=0;wm.m.commands=wm.m.writes=0;wm.m.state=0;wm.m.cmd1=0;
 maps_started=0;memset(&sdr_fixture,0,sizeof(sdr_fixture));memset(&dma_fixture,0,sizeof(dma_fixture));
 make_hfs(hfs_system,196608U,0);make_hfs(hfs_data,786432U,1);return 0;
}
void fixture_finish(void){done();}
void fixture_identity(struct i9100_emmc_writer_identity *i){*i=expected_identity;}
static void output_file(const char *folder,const char *name,const void *bytes,unsigned length)
{char path[1024];int n=snprintf(path,sizeof(path),"%s/%s",folder,name);assert(n>0&&(unsigned)n<sizeof(path));FILE *f=fopen(path,"wb");assert(f&&fwrite(bytes,1,length,f)==length&&!fclose(f));}
void fixture_export_table_evidence(const char *folder)
{
 uint8_t primary[512+16384],backup[512+16384],cid[16];
 uint64_t sectors=wm.d.sectors;assert(!disk_read(&wm.d,1,primary)&&!disk_read(&wm.d,sectors-1,backup));
 unsigned count=get32(primary+80),blocks=(count*128U+511U)/512U;assert(blocks<=32);
 uint64_t pa=get64(primary+72),ba=get64(backup+72);
 for(unsigned i=0;i<blocks;i++){assert(!disk_read(&wm.d,pa+i,primary+512+i*512U));assert(!disk_read(&wm.d,ba+i,backup+512+i*512U));}
 for(unsigned i=0;i<4;i++)put32(cid+i*4U,expected_identity.cid[i]);
 output_file(folder,"cid.bin",cid,sizeof(cid));output_file(folder,"gpt-primary.bin",primary,512+blocks*512U);
 output_file(folder,"gpt-backup.bin",backup,512+blocks*512U);
 unsigned pitbytes=28U+get32(wm.d.head+0x4404U)*132U;assert(0x4400U+pitbytes<=1048576U);
 output_file(folder,"pit.bin",wm.d.head+0x4400U,pitbytes);
}
void fixture_enable_sdr(unsigned failure){sdr_fixture.enabled=1;sdr_fixture.failure=failure;}
unsigned fixture_sdr_switches(void){return sdr_fixture.switches;}
void fixture_dma_buffers(uint8_t *desc,uint32_t dp,uint8_t *data,uint32_t bp,unsigned failure)
{dma_fixture.enabled=1;dma_fixture.descriptors=desc;dma_fixture.desc_pa=dp;dma_fixture.data=data;dma_fixture.data_pa=bp;dma_fixture.failure=failure;}
unsigned fixture_dma_requests(void){return dma_fixture.requests;}
static void fixture_dma_transfer(unsigned cmd,uint32_t lba,uint32_t bytes)
{
 assert(dma_fixture.enabled&&wm.m.state==4&&wm.m.regs[EM_DBADDR/4]==dma_fixture.desc_pa&&bytes&&bytes<=16384&&!(bytes&511U));
 unsigned count=(bytes+4095)/4096U,remaining=bytes;
 for(unsigned i=0;i<count;i++){
  uint8_t *d=dma_fixture.descriptors+i*16;unsigned n=remaining>4096?4096:remaining;
  uint32_t control=1U<<31|(i==0?8U:0U)|(i+1==count?4U:18U);
  assert(get32(d)==control&&get32(d+4)==n&&get32(d+8)==dma_fixture.data_pa+i*4096U&&
     get32(d+12)==(i+1==count?0U:dma_fixture.desc_pa+(i+1)*16U));put32(d,control&~(1U<<31));remaining-=n;
 }
 dma_fixture.requests++;maps_started++;wm.m.commands++;wm.m.regs[EM_CMD/4]&=~(1U<<31);
 wm.m.regs[EM_RINTSTS/4]|=1U<<2;response(&wm.m,(4U<<9)|(1U<<8));
 if(cmd==24U||cmd==25U){
  wm.stored_lba=lba;wm.stored_count=bytes/512U;wm.write_commands++;memcpy(wm.stored,dma_fixture.data,bytes);wm.m.state=cmd==25U?5U:4U;
 }else{
  for(unsigned i=0;i<bytes/512U;i++){
   uint64_t sector=(uint64_t)lba+i;uint8_t *p=dma_fixture.data+i*512U;
   if(wm.stored_count&&sector>=wm.stored_lba&&sector<wm.stored_lba+wm.stored_count)memcpy(p,wm.stored+(sector-wm.stored_lba)*512U,512U);
   else if(sector==344064ULL+2||sector==344064ULL+3145728ULL-2)memcpy(p,hfs_system,512);
   else if(sector==3489792ULL+2||sector==3489792ULL+12582912ULL-2)memcpy(p,hfs_data,512);
   else if(disk_read(&wm.d,sector,p))memset(p,0,512);
  }
  wm.m.state=cmd==18U?5U:4U;
 }
 wm.m.count=wm.m.position=0;wm.m.regs[EM_TCBCNT/4]=bytes;wm.m.regs[EM_TBBCNT/4]=bytes;
 wm.m.regs[EM_RINTSTS/4]|=1U<<3;wm.m.regs[EM_IDSTS/4]=0x100U|(cmd==24U||cmd==25U?1U:2U);
 if(dma_fixture.failure==1){wm.m.regs[EM_RINTSTS/4]&=~(1U<<3);wm.m.regs[EM_IDSTS/4]=1U<<13;}
 if(dma_fixture.failure==2)wm.m.regs[EM_TCBCNT/4]-=4;
 if(dma_fixture.failure==3)put32(dma_fixture.descriptors,get32(dma_fixture.descriptors)|(1U<<31));
}
uint32_t fixture_read(unsigned off)
{if(sdr_fixture.enabled&&off==EM_STATUS&&sdr_fixture.busy)return 1U<<9;return wread(&wm,off);}
void fixture_write(unsigned off,uint32_t value)
{
 if(dma_fixture.enabled&&off==EM_CTRL){
  if(dma_fixture.failure==4&&!(value&(1U<<25))&&wm.m.regs[EM_CTRL/4]&(1U<<25))return;
  wm.m.regs[off/4]=value&~7U;maps_started++;return;
 }
 if(dma_fixture.enabled&&(off==EM_BMOD||off==EM_DBADDR)){
  wm.m.regs[off/4]=off==EM_BMOD?value&~1U:value;maps_started++;return;
 }
 if(dma_fixture.enabled&&off==EM_CMD&&!(value&(1U<<21))&&wm.m.regs[EM_CTRL/4]&(1U<<25)){
  unsigned cmd=value&63U;assert(cmd==17U||cmd==18U||cmd==24U||cmd==25U);
  wm.m.regs[EM_CMD/4]=value;fixture_dma_transfer(cmd,wm.m.regs[EM_CMDARG/4],wm.m.regs[EM_BYTCNT/4]);return;
 }
 if(sdr_fixture.enabled&&off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==6U&&wm.m.regs[EM_CMDARG/4]!=0x03200100U){
  uint32_t arg=wm.m.regs[EM_CMDARG/4];unsigned index=(arg>>16)&255U,data_value=(arg>>8)&255U;
  assert((arg&0xff0000ffU)==0x03000001U&&(index==183U||index==185U||index==187U)&&!(value&((1U<<9)|(1U<<10))));
  sdr_fixture.switches++;maps_started++;wm.m.commands++;wm.m.regs[EM_CMD/4]=value&~(1U<<31);wm.m.regs[EM_RINTSTS/4]|=1U<<2;
  if(sdr_fixture.failure==1&&sdr_fixture.switches==1){response(&wm.m,1U<<7);return;}
  if(sdr_fixture.failure==2&&sdr_fixture.switches>3){response(&wm.m,1U<<7);return;}
  if(index==183U)sdr_fixture.width=data_value;else if(index==185U)sdr_fixture.timing=data_value;else sdr_fixture.power_class=data_value;
  sdr_fixture.busy=2;response(&wm.m,7U<<9);return;
 }
 if(sdr_fixture.enabled&&off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==13U&&sdr_fixture.busy){
  sdr_fixture.busy--;maps_started++;wm.m.commands++;wm.m.regs[EM_CMD/4]=value&~(1U<<31);
  wm.m.regs[EM_RINTSTS/4]|=1U<<2;response(&wm.m,7U<<9);return;
 }
 wwrite(&wm,off,value);maps_started++;
 if(sdr_fixture.enabled&&off==EM_CMD&&!(value&(1U<<21))&&(value&63U)==8U){
  const unsigned fields[]={183,185,187,196,202,203,248};
  const uint8_t bytes[]={sdr_fixture.width,sdr_fixture.timing,sdr_fixture.power_class,3,0x43,0x21,20};
  for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
   unsigned shift=(fields[i]&3U)*8U;wm.m.words[fields[i]/4U]=(wm.m.words[fields[i]/4U]&~(255U<<shift))|((uint32_t)bytes[i]<<shift);
  }
  if(sdr_fixture.failure==3&&sdr_fixture.switches)wm.m.words[212/4U]^=1U;
  unsigned ctype=sdr_fixture.width==0?0:sdr_fixture.width==1?1:1U<<16;
  if(wm.m.regs[EM_CTYPE/4]!=ctype)wm.m.regs[EM_RINTSTS/4]|=1U<<7;
 }
 if(off==EM_CMD&&!(value&(1U<<21))&&((value&63U)==17U||(value&63U)==18U)){
  uint64_t lba=wm.m.regs[EM_CMDARG/4];unsigned count=wm.m.regs[EM_BYTCNT/4]/512U;
  for(unsigned i=0;i<count;i++){
   const uint8_t *sector=0;
   if(lba+i==344064ULL+2U||lba+i==344064ULL+3145728ULL-2U)sector=hfs_system;
   if(lba+i==3489792ULL+2U||lba+i==3489792ULL+12582912ULL-2U)sector=hfs_data;
   if(sector)for(unsigned k=0;k<128;k++)wm.m.words[i*128U+k]=get32(sector+k*4U);
  }
 }
}
uint64_t fixture_time(void){return wm.m.time;}
void fixture_delay(unsigned us){wm.m.time+=(uint64_t)us*1000U;}
unsigned fixture_writes(void){return maps_started;}
unsigned fixture_sector_writes(void){return wm.write_commands;}
unsigned fixture_flushes(void){return wm.flushes;}
void fixture_volume_ids(uint32_t system_date,uint32_t data_date,const uint8_t system[8],const uint8_t data[8])
{big32(hfs_system+16,system_date);big32(hfs_data+16,data_date);memcpy(hfs_system+104,system,8);memcpy(hfs_data+104,data,8);big32(hfs_data+12,13U);}
void fixture_fault(unsigned n)
{
 if(n==1)wm.corrupt_readback=1;
 if(n==2)wm.full_stuck=1;
 if(n==3)wm.flush_error=1;
 if(n==4)wm.error_write=1;
}
void fixture_clear_fault(void){wm.corrupt_readback=wm.full_stuck=wm.flush_error=wm.error_write=0;}
