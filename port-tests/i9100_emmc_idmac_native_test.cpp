/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_IDMAC_NATIVE_MODEL 1
#define main legacy_emmc_main
#include "i9100_emmc_native_test.cpp"
#undef main
extern "C" {void fixture_dma_buffers(uint8_t *,uint32_t,uint8_t *,uint32_t,unsigned);unsigned fixture_dma_requests(void);}
static void reset_dma_fixture(const char *source,unsigned failure=0)
{
 setup(source);assert(!dma_model_descriptors&&!dma_model_prepared);dma_model_next=0;dma_model_bad_alias=0;
 idmac_try_calls=idmac_fail_lock_call=0;idmac_buffer_locks[0]=idmac_buffer_locks[1]=nullptr;
 memset(dma_model_memory,0,sizeof(dma_model_memory));
 fixture_dma_buffers(dma_model_memory,0x46000000,dma_model_memory+4096U,0x46001000,failure);
}
int main(int argc,char **argv)
{
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);i9100_emmc_native *n=nullptr;
 if(argc==2){
  unsigned failure=!strcmp(argv[1],"timeout")?1:!strcmp(argv[1],"short-counter")?2:!strcmp(argv[1],"owned-descriptor")?3:4;
  reset_dma_fixture(source,failure);int e=i9100_emmc_native_open_configured(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,nullptr,1,&n);
  assert(e&&n&&m.maps==5&&dma_model_descriptors==2&&parent.holds==1);
  struct i9100_emmc_idmac observed;assert(!i9100_emmc_native_idmac_snapshot(n,&observed)&&observed.binding.poisoned);
  assert(observed.data->state==I9100_DMA_QUARANTINED||observed.descriptors->state==I9100_DMA_QUARANTINED);
  unsigned requests=fixture_dma_requests(),writes=fixture_writes();
  assert(i9100_emmc_native_close(&n)!=0&&n&&fixture_dma_requests()==requests&&fixture_writes()==writes);
  i9100_emmc_host_lease other={};assert(i9100_emmc_host_acquire(&other)==I9100_EMMC_BUSY);
  static i9100_emmc_native *volatile retained;retained=n;
  static std::mutex *volatile locks[4];unsigned i=0;for(auto &entry:host_locks){assert(i<4);locks[i++]=entry.second;}
  assert(i==4&&retained&&locks[0]);fixture_finish();
  puts("PASS actualnative IDMAC failed-read preserves real allocator memory/maps/hostlease and rejects close retries; MMIO/PTE/time facade");return 0;
 }
 assert(argc==1);reset_dma_fixture(source);
 assert(i9100_emmc_native_enable_idmac(nullptr,0,0)==I9100_EMMC_UNSUPPORTED&&!m.count);fixture_finish();
 for(unsigned fault=3;fault<=4;fault++){
  reset_dma_fixture(source);idmac_fail_lock_call=fault;
  assert(i9100_emmc_native_open_configured(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,nullptr,1,&n)==I9100_DMA_BUSY);
  assert(!n&&!dma_model_descriptors&&!dma_model_prepared&&!m.maps&&!m.contexts&&!m.locks&&!parent.holds);
  assert(!fixture_dma_requests());fixture_finish();
 }
 for(unsigned inspect=0;inspect<2;inspect++){
  reset_dma_fixture(source);i9100_emmc_writer_identity approved;fixture_identity(&approved);
  assert(!i9100_emmc_native_open_configured(1,&parent,&profile,inspect?I9100_EMMC_N_INSPECT:I9100_EMMC_N_DATA_RW,inspect?nullptr:&approved,nullptr,1,&n));
  struct i9100_emmc_idmac observed;assert(!i9100_emmc_native_idmac_snapshot(n,&observed));
  assert(observed.binding.enabled&&!observed.binding.poisoned&&dma_model_descriptors==2&&dma_model_prepared==2);
  assert(observed.descriptors->physical==0x46000000&&observed.data->physical==0x46001000&&observed.data->state==I9100_DMA_CPU&&observed.descriptors->state==I9100_DMA_CPU);
  if(inspect)if(const char *path=getenv("I9100_EMMC_IDMAC_CLOCK_RECORD")){
   uint8_t clock[512];assert(!i9100_emmc_native_clock_snapshot(n,clock));
   FILE *file=fopen(path,"wb");assert(file&&fwrite(clock,1,sizeof(clock),file)==sizeof(clock)&&!fclose(file));
  }
  uint8_t bytes[16384],again[16384];for(unsigned k=0;k<sizeof(bytes);k++)bytes[k]=(uint8_t)(k*19U);
  uint64_t lba=3489792ULL+8388608ULL;
  if(!inspect){assert(!i9100_emmc_native_write_data(n,lba,32,bytes));assert(!i9100_emmc_native_read(n,lba,32,again)&&!memcmp(bytes,again,16384));
   assert(!i9100_emmc_native_synchronize(n)&&fixture_flushes()==1);}
  else{unsigned writes=fixture_sector_writes();assert(i9100_emmc_native_write_data(n,lba,1,bytes)==I9100_EMMC_PARTITION&&fixture_sector_writes()==writes);}
  assert(!i9100_emmc_native_close(&n)&&!n&&!dma_model_descriptors&&!dma_model_prepared&&!m.maps&&!m.contexts&&!parent.holds);fixture_finish();
 }
 for(unsigned fault=1;fault<=8;fault++){
  reset_dma_fixture(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&n));
  m.fail_at=m.count+fault;int e=i9100_emmc_native_enable_idmac(n,1,1);
  if(e){assert(!dma_model_descriptors&&!dma_model_prepared);}
  assert(!i9100_emmc_native_close(&n)&&!n&&!dma_model_descriptors&&!dma_model_prepared&&!m.contexts&&!m.maps);fixture_finish();
 }
 reset_dma_fixture(source);assert(!i9100_emmc_native_open(1,&parent,&profile,I9100_EMMC_N_INSPECT,nullptr,&n));dma_model_bad_alias=1;
 assert(i9100_emmc_native_enable_idmac(n,1,1)==I9100_DMA_IO&&!dma_model_descriptors&&!dma_model_prepared);
 assert(!i9100_emmc_native_close(&n)&&!n);fixture_finish();
 puts("PASS actualnative5maps+real DMAallocator/NormalNC-PTEaliases/IDMAC/core/currentGPTPIT/>4GiB16KiBDATAwrite-readback/ROinspect/flush/8allocationfaults/badalias/cleanclose/defaultOFF; hardware facades only");return 0;
}
