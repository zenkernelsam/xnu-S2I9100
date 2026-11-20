/* SPDX-License-Identifier: BSD-2-Clause; register/media boundary only. */
#define I9100_EMMC_WRITER_MAIN legacy_writer_fixture_main
#include "i9100_emmc_writer_test.c"
static unsigned maps_started;
static uint8_t hfs_system[512],hfs_data[512];
static struct i9100_emmc_writer_identity expected_identity;
static void make_hfs(uint8_t h[512],uint32_t bytes_blocks,int journaled)
{memset(h,0,512);h[0]='H';h[1]='X';h[3]=5;big32(h+4,0x100U|(journaled?0x2000U:0));
 big32(h+40,8192);big32(h+44,bytes_blocks);big32(h+48,1U);}
int fixture_setup(const char *source)
{
 if(wm.d.head)return -1;expected_identity=setup_writer(source,1);
 wm.m.count=wm.m.position=0;wm.m.commands=wm.m.writes=0;wm.m.state=0;wm.m.cmd1=0;
 maps_started=0;make_hfs(hfs_system,196608U,0);make_hfs(hfs_data,786432U,1);return 0;
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
uint32_t fixture_read(unsigned off){return wread(&wm,off);}
void fixture_write(unsigned off,uint32_t value)
{
 wwrite(&wm,off,value);maps_started++;
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
