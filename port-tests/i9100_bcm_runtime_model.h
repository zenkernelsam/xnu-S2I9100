/* SPDX-License-Identifier: BSD-2-Clause; shared actual upper-driver runtime model. */
#ifndef I9100_BCM_RUNTIME_MODEL_H
#define I9100_BCM_RUNTIME_MODEL_H
#include <openssl/sha.h>
#include "i9100_bcm_boot.h"
#include "i9100_bcm_core_download.h"
#include "i9100_bcm_wire_model.h"
static struct i9100_sdio card;
static struct i9100_bcm_f2 packets;
static struct i9100_bcm_firmware firmware;
static struct i9100_bcm_boot boot;
static uint8_t ram[393216],rx[4096],tx[4096];
static unsigned rx_bytes,rx_cursor,tx_bytes,rx_wire_calls;
static uint32_t ignore_cfg_write=0xffffffffU;
int i9100_sdio_cmd52(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,uint8_t *value)
{
 assert(s==&card&&m.locked);if(s->fault)return I9100_SDIO_IO;int e=0;
 if(fn==1U)e=cmd52(&m,write,fn,address,value);
 else{
  assert(fn==0U);uint8_t *v=address==2U?&m.function_enable:address==0x210U?&m.fbr_low:address==0x211U?&m.fbr_high:0;
  if(v){if(write){if(address!=ignore_cfg_write)*v=*value;}else *value=*v;}
  else{assert(!write&&address==3U);*value=m.function_enable&~4U;if(m.f2_ready&&!(m.arm_reset&1U))*value|=m.function_enable&4U;}
  e=operation(&m);
 }
 if(e)s->fault=1;return e?I9100_SDIO_IO:0;
}
int i9100_sdio_cmd53(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,int increment,uint8_t *data,uint32_t bytes)
{
 assert(s==&card&&m.locked);if(s->fault)return I9100_SDIO_IO;int e;
 if(fn==1U)e=cmd53(&m,write,fn,address,increment,data,bytes);
 else{
  assert(fn==2U&&!increment&&address==0x8000U&&(m.function_enable&4U));
  if(write){assert(tx_bytes+bytes<=sizeof(tx));memcpy(tx+tx_bytes,data,bytes);tx_bytes+=bytes;}
  else{assert(rx_cursor+bytes<=rx_bytes);memcpy(data,rx+rx_cursor,bytes);rx_cursor+=bytes;rx_wire_calls++;}
  e=operation(&m);
 }
 if(e)s->fault=1;return e?I9100_SDIO_IO:0;
}
static int digest(void *cookie,const void *bytes,uint32_t n,uint8_t out[32])
{(void)cookie;return SHA256((const unsigned char *)bytes,n,out)?0:-1;}
static void setup_boot(void)
{
 setup(8);finish();memset(&bus,0,sizeof(bus));memset(&chip,0,sizeof(chip));memset(&card,0,sizeof(card));memset(&packets,0,sizeof(packets));
 memset(&firmware,0,sizeof(firmware));memset(&boot,0,sizeof(boot));rx_bytes=rx_cursor=tx_bytes=rx_wire_calls=0;ignore_cfg_write=0xffffffffU;
 m.words--;core(0x812,16,0x18001000U,0x18101000U,1);m.erom[m.words++]=15U;
 m.core_actions_allowed=1;m.ht_ready=m.f2_ready=1;m.function_enable=2;m.fbr_high=1;m.boot_model=1;
 m.firmware_mailbox=0x4000aU;m.sdio_regs[0x20U/4U]=0x20U;m.sdio_regs[0x48U/4U]=0x55070011U;m.sdio_regs[0x4cU/4U]=0x4000aU;
 card.initialized=card.active=1;card.functions=2;card.block_size[1]=64;card.block_size[2]=256;
 struct i9100_f2_io fio={&m,lock,unlock,now,delay};assert(!i9100_bcm_f2_init(&packets,&card,&fio));
 struct i9100_bcm_io bio;assert(!i9100_bcm_f2_get_io(&packets,&bio));assert(!i9100_bcm_init(&bus,&bio));
 assert(!i9100_bcm_enable(&bus,1,0x2d0,0x4330));assert(!i9100_bcm_chip_init(&chip,&bus));assert(!i9100_bcm_chip_probe(&chip,1,1));
 assert(!i9100_bcm_core_download_prepare(&chip,1));assert(!i9100_bcm_chip_ram_probe(&chip));
 m.ram=ram;m.ram_capacity=sizeof(ram);memset(ram,0xa5,sizeof(ram));
 const uint8_t image[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};const uint8_t vars[]="macaddr=02:11:22:33:44:66\0boardtype=0x053e\0\0";
 struct i9100_bcm_fw_profile p={0};p.chip_revision=3;p.ram_bytes=sizeof(ram);p.firmware_bytes=sizeof(image);p.nvram_bytes=sizeof(vars);p.board_data_reviewed=1;
 const uint8_t mac[6]={2,0x11,0x22,0x33,0x44,0x66};memcpy(p.verified_factory_mac,mac,6);
 assert(SHA256(image,sizeof(image),p.firmware_sha256)&&SHA256(vars,sizeof(vars),p.nvram_sha256));
 struct i9100_bcm_fw_integrity integrity={0,digest};assert(!i9100_bcm_fw_init(&firmware,1,&chip,&p,&integrity));
 assert(!i9100_bcm_fw_load(&firmware,image,vars));m.expected_token=firmware.layout.token;
 assert(!i9100_bcm_boot_init(&boot,1,&firmware,&packets,1));
}
static void recover_host(void)
{card.fault=0;packets.fault=0;m.fail_op=0; /* explicit external controller recovery in this stand-in, never performed by driver */}
static void close_boot(void)
{
 recover_host();assert(!i9100_bcm_boot_stop(&boot));assert(!i9100_bcm_boot_close(&boot));
 assert(!i9100_bcm_fw_abort(&firmware));assert(!i9100_bcm_fw_close(&firmware));assert(!i9100_bcm_core_download_stop(&chip));finish();
 assert(m.function_enable==2U&&m.fbr_high==1U&&m.fbr_low==0U&&card.block_size[2]==256U);
 assert((m.sdio_regs[0x48U/4U]&0xff0000U)==0x70000U);
}
static void put16(uint8_t *b,uint16_t n){b[0]=(uint8_t)n;b[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *b,uint32_t n){for(unsigned i=0;i<4U;i++)b[i]=(uint8_t)(n>>(8U*i));}

#endif
