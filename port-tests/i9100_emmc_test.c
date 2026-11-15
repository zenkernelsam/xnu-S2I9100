/* SPDX-License-Identifier: BSD-2-Clause
 * Independent MMC card/PIO peripheral model plus historical-device tables.
 * Generated backup-GPT/HFS fixture sectors are NOT real hardware evidence. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "i9100_emmc.h"
#include "i9100_emmc_layout.h"
struct model {
 uint32_t regs[128],words[4096],count,position,state,commands,writes,cmd1;
 uint32_t inject_command,inject_error,partition,bus_width,sector_size;
 int cmd_stuck,reset_stuck,fifo_stuck,clock_stuck,early_done,bad_r1;
 uint64_t time;
};
static void response(struct model *m,uint32_t v){m->regs[EM_RESP0/4]=v;}
static void data(struct model *m,unsigned cmd,uint32_t lba)
{
 m->position=0;m->count=m->regs[EM_BYTCNT/4]/4U;assert(m->count<=4096);
 memset(m->words,0,sizeof(m->words));
 if(cmd==8){
  uint8_t b[512]={0};b[192]=7;b[179]=(uint8_t)m->partition;b[183]=(uint8_t)m->bus_width;b[61]=(uint8_t)m->sector_size;
  uint32_t sectors=30777344U;for(unsigned n=0;n<4;n++)b[212+n]=(uint8_t)(sectors>>(n*8));
  for(unsigned n=0;n<128;n++)m->words[n]=(uint32_t)b[n*4]|(uint32_t)b[n*4+1]<<8|(uint32_t)b[n*4+2]<<16|(uint32_t)b[n*4+3]<<24;
 }else if(lba)for(unsigned n=0;n<m->count;n++)m->words[n]=lba^(n*0x01020304U);
 if(m->early_done)m->regs[EM_RINTSTS/4]|=1U<<3;
}
static uint32_t read32(void *cookie,unsigned r)
{
 struct model *m=cookie;assert(r<=EM_DATA&&!(r&3U));
 if(r==EM_STATUS){
  uint32_t n=m->count-m->position;if(m->fifo_stuck)n=0;if(n>16)n=16;
  return n<<17;
 }
 if(r==EM_DATA){
  assert(m->position<m->count&&!m->fifo_stuck);uint32_t v=m->words[m->position++];
  if(m->position==m->count){m->regs[EM_RINTSTS/4]|=1U<<3;if(m->state==6)m->state=4;}
  return v;
 }
 return m->regs[r/4];
}
static void write32(void *cookie,unsigned r,uint32_t value)
{
 struct model *m=cookie;m->writes++;assert(r<EM_DATA&&!(r&3U));
 /* An implementation is forbidden to enable DMA or program its addresses. */
 assert(r!=0x88&&r!=0x94&&r!=0x98);
 if(r==EM_RINTSTS||r==EM_IDSTS){m->regs[r/4]&=~value;return;}
 if(r==EM_CTRL){assert(!(value&((1U<<5)|(1U<<25))));m->regs[r/4]=m->reset_stuck?value:value&~7U;return;}
 m->regs[r/4]=value;
 if(r!=EM_CMD)return;
 if(m->cmd_stuck)return;
 m->regs[EM_CMD/4]&=~(1U<<31);
 if(value&(1U<<21))return;
 unsigned cmd=value&63U;uint32_t arg=m->regs[EM_CMDARG/4];m->commands++;
 assert(!(value&(1U<<10))&&cmd!=6&&cmd!=24&&cmd!=25&&cmd!=35&&cmd!=36&&cmd!=38&&cmd!=42);
 m->regs[EM_RINTSTS/4]|=1U<<2;
 if(m->inject_command==cmd){m->regs[EM_RINTSTS/4]|=m->inject_error;return;}
 switch(cmd){
 case 0:assert(value&(1U<<15));m->state=0;response(m,0);break;
 case 1:assert(m->state<=1&&arg==0x40ff8000U&&!(value&(1U<<8)));m->state=1;response(m,(++m->cmd1>1?1U<<31:0)|(1U<<30)|0xff8000U);break;
 case 2:assert(m->state==1&&(value&(1U<<7)));m->state=2;m->regs[EM_RESP3/4]=0x15010053;m->regs[EM_RESP2/4]=0x414d5355;m->regs[EM_RESP1/4]=0x4e470001;m->regs[EM_RESP0/4]=0x12345679;break;
 case 3:assert(m->state==2&&arg==1U<<16);m->state=3;response(m,3U<<9);break;
 case 9:assert(m->state==3&&arg==1U<<16);m->regs[EM_RESP3/4]=(4U<<26)|0x32U;m->regs[EM_RESP2/4]=0;m->regs[EM_RESP1/4]=0;m->regs[EM_RESP0/4]=0;break;
 case 7:assert(m->state==3&&arg==1U<<16);m->state=4;response(m,(4U<<9)|(1U<<8));break;
 case 13:assert(arg==1U<<16);response(m,(m->state<<9)|(m->state==4?1U<<8:0));break;
 case 8:assert(m->state==4&&arg==0&&m->regs[EM_BLKSIZ/4]==512&&(value&(1U<<9)));m->state=6;response(m,(4U<<9)|(1U<<8));data(m,cmd,0);break;
 case 17:case 18:
  assert(m->state==4&&m->regs[EM_BLKSIZ/4]==512&&(value&(1U<<9)));
  response(m,m->bad_r1?1U<<31:(4U<<9)|(1U<<8));m->state=cmd==18?5:6;data(m,cmd,arg);break;
 case 12:assert(m->state==5&&(value&(1U<<14)));m->state=4;response(m,(4U<<9)|(1U<<8));break;
 default:assert(!"unsupported card command");
 }
}
static uint64_t now(void *cookie){struct model *m=cookie;return m->clock_stuck?0:m->time;}
static int delay(void *cookie,uint32_t us){struct model *m=cookie;m->time+=(uint64_t)us*1000U;return 0;}
static void reset(struct model *m,struct i9100_emmc_card *c)
{
 memset(m,0,sizeof(*m));m->regs[EM_VERID/4]=0x5342220aU;m->regs[EM_HCON/4]=1U<<7;m->regs[EM_PWREN/4]=1;
 m->inject_command=UINT32_MAX;
 struct i9100_emmc_io io={m,read32,write32,now,delay,100000000U,20000000U,5000U};i9100_emmc_init(c,&io);
}
static void protocol_tests(void)
{
 struct model *m=calloc(1,sizeof(*m));struct i9100_emmc_card c;uint8_t *buffer=malloc(16384);assert(m&&buffer);
 reset(m,&c);assert(!i9100_emmc_probe(&c)&&c.ready&&c.sectors==30777344U&&c.cid[0]==0x15010053U&&c.clock_hz<=20000000U);
 assert(!i9100_emmc_read(&c,0,1,buffer));for(unsigned n=0;n<512;n++)assert(!buffer[n]);
 uint64_t high=3489792ULL+8388608ULL;assert(!i9100_emmc_read(&c,high,32,buffer));
 for(unsigned n=0;n<4096;n++){uint32_t v=(uint32_t)high^(n*0x01020304U);for(unsigned j=0;j<4;j++)assert(buffer[n*4+j]==(uint8_t)(v>>(j*8)));}
 unsigned cmds=m->commands;assert(i9100_emmc_read(&c,c.sectors,1,buffer)==I9100_EMMC_ARGUMENT);
 assert(i9100_emmc_read(&c,0,33,buffer)==I9100_EMMC_ARGUMENT&&m->commands==cmds&&c.ready);
 assert(i9100_emmc_read(&c,1ULL<<32,1,buffer)==I9100_EMMC_ARGUMENT&&m->commands==cmds);
 reset(m,&c);c.io.input_clock_hz=0;assert(i9100_emmc_probe(&c)==I9100_EMMC_CLOCK&&!m->writes);
 reset(m,&c);m->regs[EM_HCON/4]=2U<<7;assert(i9100_emmc_probe(&c)==I9100_EMMC_UNSUPPORTED&&!m->writes);
 reset(m,&c);m->partition=1;assert(i9100_emmc_probe(&c)==I9100_EMMC_PARTITION&&!c.ready);
 reset(m,&c);m->sector_size=1;assert(i9100_emmc_probe(&c)==I9100_EMMC_UNSUPPORTED);
 reset(m,&c);m->inject_command=2;m->inject_error=1U<<6;assert(i9100_emmc_probe(&c)==I9100_EMMC_RESPONSE_CRC);
 reset(m,&c);m->inject_command=8;m->inject_error=1U<<7;assert(i9100_emmc_probe(&c)==I9100_EMMC_DATA_CRC);
 reset(m,&c);m->cmd_stuck=m->clock_stuck=1;c.io.poll_bound=5;assert(i9100_emmc_probe(&c)==I9100_EMMC_TIMEOUT);
 reset(m,&c);m->fifo_stuck=m->clock_stuck=1;c.io.poll_bound=30;assert(i9100_emmc_probe(&c)==I9100_EMMC_TIMEOUT);
 reset(m,&c);m->early_done=1;assert(i9100_emmc_probe(&c)==I9100_EMMC_IO);
 reset(m,&c);assert(!i9100_emmc_probe(&c));m->bad_r1=1;assert(i9100_emmc_read(&c,100,1,buffer)==I9100_EMMC_CARD_ERROR&&!c.ready);
 free(buffer);free(m);
 puts("PASS MMC state/PIO,32-sector transfer beyond4GiB,zero sector,CRC/R1/FIFO/stopped-clock failures and forbidden commands/DMA");
}
static uint32_t crc(uint8_t *p,unsigned n){uint32_t c=0xffffffffU;while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return c^0xffffffffU;}
static void put32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(n*8));}
static void put64(uint8_t *p,uint64_t v){for(unsigned n=0;n<8;n++)p[n]=(uint8_t)(v>>(n*8));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static void big32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(24-n*8));}
struct disk {uint8_t *head,backup[512],hfs[512];uint64_t sectors,data_start,data_sectors;int damage_backup,io_fail;};
static int disk_read(void *cookie,uint64_t lba,uint8_t out[512])
{
 struct disk *d=cookie;if(d->io_fail||lba>=d->sectors)return -1;
 if(lba<2048){memcpy(out,d->head+lba*512U,512);return 0;}
 if(lba==d->sectors-1){memcpy(out,d->backup,512);return 0;}
 if(lba>=d->sectors-33&&lba<d->sectors-1){memcpy(out,d->head+(2U+lba-(d->sectors-33))*512U,512);if(d->damage_backup)out[0]^=1;return 0;}
 if(lba==d->data_start+2||lba==d->data_start+d->data_sectors-2){memcpy(out,d->hfs,512);return 0;}
 return -1;
}
static void reseal(struct disk *d)
{
 uint8_t *h=d->head+512;unsigned bytes=get32(h+80)*get32(h+84);
 put32(h+88,crc(d->head+get64(h+72)*512U,bytes));put32(h+16,0);put32(h+16,crc(h,get32(h+12)));
 memcpy(d->backup,h,512);put64(d->backup+24,d->sectors-1);put64(d->backup+32,1);put64(d->backup+72,d->sectors-33);
 put32(d->backup+16,0);put32(d->backup+16,crc(d->backup,get32(h+12)));
}
static void layout_tests(const char *file)
{
 struct disk d;memset(&d,0,sizeof(d));d.head=malloc(1048576);assert(d.head);
 FILE *f=fopen(file,"rb");assert(f&&fread(d.head,1,1048576,f)==1048576);fclose(f);
 d.sectors=get64(d.head+512+32)+1;uint8_t *original=malloc(1048576);assert(original);memcpy(original,d.head,1048576);
 reseal(&d);struct i9100_layout_io io={&d,disk_read,d.sectors};struct i9100_emmc_layout l;
 assert(!i9100_emmc_read_layout(&io,&l)&&l.valid&&l.count==12);assert(!i9100_emmc_check_pit(&io,&l)&&l.pit_valid);
 const struct i9100_emmc_partition *p=i9100_emmc_find_partition(&l,"DATAFS");assert(p&&p->sectors*512U==6442450944ULL&&p->pit_identifier==11);
 d.data_start=p->start_lba;d.data_sectors=p->sectors;
 d.hfs[0]='H';d.hfs[1]='X';d.hfs[3]=5;big32(d.hfs+4,1U<<13);big32(d.hfs+40,8192);big32(d.hfs+44,(uint32_t)(p->sectors/16));big32(d.hfs+48,1);
 struct i9100_hfs_volume volume;assert(!i9100_emmc_hfs_volume(&io,p,&volume)&&volume.bytes==6442450944ULL);
 d.hfs[1]='+';assert(i9100_emmc_hfs_volume(&io,p,&volume)==I9100_LAYOUT_HFS);d.hfs[1]='X';
 d.damage_backup=1;assert(i9100_emmc_read_layout(&io,&l)==I9100_LAYOUT_CRC);d.damage_backup=0;
 d.head[512+16]^=1;assert(i9100_emmc_read_layout(&io,&l)==I9100_LAYOUT_CRC);memcpy(d.head,original,1048576);
 uint8_t *table=d.head+get64(d.head+512+72)*512U;put64(table+128+32,get64(table+32)+1);reseal(&d);assert(i9100_emmc_read_layout(&io,&l)==I9100_LAYOUT_OVERLAP);
 memcpy(d.head,original,1048576);table=d.head+get64(d.head+512+72)*512U;memcpy(table+128+16,table+16,16);reseal(&d);assert(i9100_emmc_read_layout(&io,&l)==I9100_LAYOUT_ENTRY);
 memcpy(d.head,original,1048576);reseal(&d);assert(!i9100_emmc_read_layout(&io,&l));
 unsigned altered=0;for(unsigned n=0;n<get32(d.head+0x4404);n++){
  uint8_t *entry=d.head+0x4400+28+n*132U;if(!memcmp(entry+36,"FACTORYFS",10)){entry[20]^=1;altered=1;break;}
 }
 assert(altered&&i9100_emmc_check_pit(&io,&l)==I9100_LAYOUT_PIT);
 memcpy(d.head,original,1048576);reseal(&d);d.io_fail=1;assert(i9100_emmc_read_layout(&io,&l)==I9100_LAYOUT_IO);
 free(original);free(d.head);
 puts("PASS historical primary GPT/PIT with SYNTHETIC backup/HFS,6GiB volume,CRC/overlap/duplicate-GUID/PIT/I-O rejection");
}
int main(int argc,char **argv){assert(argc==2);protocol_tests();layout_tests(argv[1]);return 0;}
