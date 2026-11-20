/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_SDR_NATIVE_MODEL 1
static unsigned sdr_gpio_drop_offset=~0U,sdr_gpio_writes;
#define main native_fixture_main
#include "i9100_emmc_native_test.cpp"
#undef main
extern "C" {void fixture_enable_sdr(unsigned);unsigned fixture_sdr_switches(void);}
static i9100_emmc_sdr_plan sdr_setup(const char *source,unsigned width,unsigned fault=0)
{
 setup(source);fixture_enable_sdr(fault);sdr_gpio_drop_offset=~0U;sdr_gpio_writes=0;
 regs[GPIO][0x40/4]|=0x00555000U; /* D0 low nibble kept as baseline function3. */
 regs[GPIO][0x40/4]=(regs[GPIO][0x40/4]&~0x0000f000U)|0x00003000U;
 regs[GPIO][0x48/4]=0xaaaaaaaaU&~0xffU;regs[GPIO][0x4c/4]=0x555500aaU;
 regs[GPIO][0x60/4]=0x12345678U;regs[GPIO][0x68/4]=0x01020304U;regs[GPIO][0x6c/4]=0x22222222U;
 i9100_emmc_sdr_plan p={1,width,1,50000000U,100000000U,3300,{0}};
 i9100_emmc_writer_identity identity;fixture_identity(&identity);memcpy(p.cid,identity.cid,16);return p;
}
static void quiet(i9100_emmc_native **n)
{assert(!i9100_emmc_native_close(n)&&!*n&&!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!parent.holds);fixture_finish();}
int main(int argc,char **argv)
{
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);i9100_emmc_native *n=nullptr;
 if(argc==2){
  unsigned failure=!strcmp(argv[1],"switch-error")?1U:!strcmp(argv[1],"restore-error")?2U:!strcmp(argv[1],"extcsd-corrupt")?3U:0U;
  auto plan=sdr_setup(source,8,failure);
  if(!strcmp(argv[1],"pad-readback"))sdr_gpio_drop_offset=0x60;
  int e=i9100_emmc_native_open_sdr(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&plan,&n);
  if(!strcmp(argv[1],"restore-error")||!strcmp(argv[1],"pad-restore")){
   assert(!e&&n);if(!strcmp(argv[1],"pad-restore"))sdr_gpio_drop_offset=0x60;
   e=i9100_emmc_native_close(&n);
  }
  assert(e&&n&&m.maps==5&&parent.holds==1);
  unsigned commands=fixture_writes(),switches=fixture_sdr_switches();
  assert(i9100_emmc_native_close(&n)!=0&&n&&fixture_writes()==commands&&fixture_sdr_switches()==switches);
  i9100_emmc_host_lease other={};assert(i9100_emmc_host_acquire(&other)==I9100_EMMC_BUSY);
  static i9100_emmc_native *volatile retained;retained=n;
  static std::mutex *volatile retained_locks[2];unsigned index=0;
  for(auto &entry:host_locks){assert(index<2);retained_locks[index++]=entry.second;}
  assert(index==2&&retained&&retained_locks[0]);fixture_finish();
  puts("PASS failedSDR native keeps commandowner/maps/pads/card and never resends switch or restore; explicit fault facade");return 0;
 }
 assert(argc==1);
 auto plan=sdr_setup(source,4);assert(i9100_emmc_native_open_sdr(0,nullptr,nullptr,I9100_EMMC_N_INSPECT,nullptr,nullptr,&n)==I9100_EMMC_UNSUPPORTED&&!m.count);fixture_finish();
 for(unsigned width=4;width<=8;width+=4){
  plan=sdr_setup(source,width);uint32_t original[7];const unsigned offsets[]={0x40,0x44,0x48,0x4c,0x60,0x68,0x6c};
  for(unsigned k=0;k<7;k++)original[k]=regs[GPIO][offsets[k]/4];
  i9100_emmc_writer_identity identity;fixture_identity(&identity);
  assert(!i9100_emmc_native_open_sdr(1,&parent,&profile,I9100_EMMC_N_DATA_RW,&identity,&plan,&n)&&n);
  i9100_emmc_sdr_session observed;assert(!i9100_emmc_native_sdr_snapshot(n,&observed));
  assert(observed.verified&&observed.width==width&&observed.clock_hz==50000000U&&fixture_sdr_switches()==3);
  assert(regs[GPIO][0x44/4]==original[1]);assert(!i9100_emmc_native_validate(n));
  uint8_t bytes[512],again[512];for(unsigned k=0;k<512;k++)bytes[k]=(uint8_t)(k*17U);
  const uint64_t lba=3489792ULL+8388608ULL;
  assert(!i9100_emmc_native_write_data(n,lba,1,bytes)&&!i9100_emmc_native_read(n,lba,1,again)&&!memcmp(bytes,again,512));
  assert(i9100_emmc_native_write_data(n,344064,1,bytes)==I9100_EMMC_PARTITION);
  assert(!i9100_emmc_native_synchronize(n)&&fixture_flushes()==1U);
  quiet(&n);for(unsigned k=0;k<7;k++)assert(regs[GPIO][offsets[k]/4]==original[k]);
  assert(sdr_gpio_writes==(width==8?12U:6U));
 }
 plan=sdr_setup(source,8);plan.cid[0]^=1;unsigned commands=fixture_writes();
 assert(i9100_emmc_native_open_sdr(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&plan,&n)==I9100_EMMC_ARGUMENT&&!n&&fixture_sdr_switches()==0&&sdr_gpio_writes==0);
 assert(fixture_writes()>commands);fixture_finish();
 puts("PASS actualnative SDR5maps/hostlease/data-onlypadmasks/ENpreserved/currentGPTPIT-HFS/>4GiBdatawriter/ROsystem/flush/exactcard+padrestore/defaultOFF/CIDrefusal; IOKit/card/MMIO facade only");return 0;
}
