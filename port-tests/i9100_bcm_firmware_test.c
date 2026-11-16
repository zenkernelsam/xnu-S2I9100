/* SPDX-License-Identifier: BSD-2-Clause */
#include <openssl/sha.h>
#include "i9100_bcm_firmware.h"
#include "i9100_bcm_wire_model.h"
static uint8_t ram_image[393216],firmware[131075];
static const uint8_t vars[]="macaddr=02:11:22:33:44:66\0boardtype=0x053e\0boardrev=0x1204\0ccode=US\0regrev=0\0\0";
static const uint8_t factory_mac[6]={2,0x11,0x22,0x33,0x44,0x66};
static struct i9100_bcm_firmware loader;
static struct i9100_bcm_fw_profile profile;
static unsigned crypto_calls,crypto_missing;
static int digest(void *cookie,const void *bytes,uint32_t n,uint8_t out[32])
{(void)cookie;crypto_calls++;if(crypto_missing)return -1;return SHA256(bytes,n,out)?0:-1;}
static void prepare(uint32_t bytes,unsigned sdrev)
{
 setup(8);m.erom[14]=((uint32_t)sdrev<<24)|(1U<<19)|(1U<<9)|1U;
 m.words--;core(0x812,16,0x18001000U,0x18101000U,1);m.erom[m.words++]=15U;
 assert(!i9100_bcm_chip_probe(&chip,1,1));assert(!i9100_bcm_chip_ram_probe(&chip));
 memset(&loader,0,sizeof(loader));memset(&profile,0,sizeof(profile));
 for(unsigned n=0;n<sizeof(firmware);n++)firmware[n]=(uint8_t)(n*13U+7U);
 memset(ram_image,0xa5,sizeof(ram_image));m.ram=ram_image;m.ram_capacity=sizeof(ram_image);
 profile.chip_revision=3;profile.ram_bytes=sizeof(ram_image);profile.firmware_bytes=bytes;
 profile.nvram_bytes=sizeof(vars);profile.board_data_reviewed=1;memcpy(profile.verified_factory_mac,factory_mac,6);
 assert(SHA256(firmware,bytes,profile.firmware_sha256));assert(SHA256(vars,sizeof(vars),profile.nvram_sha256));
 crypto_calls=crypto_missing=0;
}
static void open_loader(void)
{struct i9100_bcm_fw_integrity io={0,digest};assert(!i9100_bcm_fw_init(&loader,1,&chip,&profile,&io));}
static uint32_t ramword(uint32_t offset)
{return ram_image[offset]|((uint32_t)ram_image[offset+1U]<<8)|((uint32_t)ram_image[offset+2U]<<16)|((uint32_t)ram_image[offset+3U]<<24);}
static void close_loader(void)
{
 assert(!i9100_bcm_fw_abort(&loader));assert(!chip.ram_image_owned);
 assert(!i9100_bcm_fw_close(&loader));assert(!chip.firmware_holds);finish();
}
static void layout_and_vars(void)
{
 struct i9100_bcm_fw_layout l,old;memset(&l,0xa5,sizeof(l));old=l;
 assert(i9100_bcm_fw_make_layout(393216U,7,393216U,100U,&l)==I9100_BCM_FW_LAYOUT&&!memcmp(&l,&old,sizeof(l)));
 /* Independent known PR85623 case:16B vars starts RAM-20 (not affected),
  * 64B vars starts RAM-68, low6=3c, so rev7 adds4 and token is17 words. */
 assert(!i9100_bcm_fw_make_layout(393216U,7,32768U,64U,&l));
 assert(l.nvram_padded==68U&&l.nvram_address==393144U&&l.token==0xffee0011U);
 assert(!i9100_bcm_fw_make_layout(393216U,12,32769U,64U,&l)&&l.nvram_padded==64U&&l.firmware_padded==32772U);
 assert(!i9100_bcm_nvram_validate(vars,sizeof(vars),factory_mac));
 uint8_t duplicate[]="macaddr=02:11:22:33:44:66\0ccode=US\0ccode=JP\0\0";
 assert(i9100_bcm_nvram_validate(duplicate,sizeof(duplicate),factory_mac)==I9100_BCM_FW_LAYOUT);
 uint8_t malformed[]="macaddr=03:11:22:33:44:66\0\0";
 assert(i9100_bcm_nvram_validate(malformed,sizeof(malformed),factory_mac)==I9100_BCM_FW_ID);
 assert(i9100_bcm_nvram_validate(vars,sizeof(vars)-3U,factory_mac)==I9100_BCM_FW_ARGUMENT);
 uint8_t extra[]="macaddr=02:11:22:33:44:66\0\0hidden=1\0\0";
 assert(i9100_bcm_nvram_validate(extra,sizeof(extra),factory_mac)==I9100_BCM_FW_LAYOUT);
 uint8_t no_mac[]="boardtype=0x053e\0\0";
 assert(i9100_bcm_nvram_validate(no_mac,sizeof(no_mac),factory_mac)==I9100_BCM_FW_ID);
}
static void safe_preflight(void)
{
 prepare(1025U,12);struct i9100_bcm_fw_integrity io={0,digest};unsigned before=m.ops;
 assert(i9100_bcm_fw_init(&loader,0,&chip,&profile,&io)==I9100_BCM_FW_DISABLED&&m.ops==before);
 profile.board_data_reviewed=0;
 assert(i9100_bcm_fw_init(&loader,1,&chip,&profile,&io)==I9100_BCM_FW_ARGUMENT&&m.ops==before);
 profile.board_data_reviewed=1;profile.ram_bytes--;
 assert(i9100_bcm_fw_init(&loader,1,&chip,&profile,&io)==I9100_BCM_FW_ARGUMENT&&m.ops==before);
 profile.ram_bytes++;profile.chip_revision=2;
 assert(i9100_bcm_fw_init(&loader,1,&chip,&profile,&io)==I9100_BCM_FW_ARGUMENT&&m.ops==before);
 profile.chip_revision=3;open_loader();before=m.ops;crypto_missing=1;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_CRYPTO&&m.ops==before&&!m.ram_writes);
 crypto_missing=0;firmware[10]^=1U;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_HASH&&m.ops==before&&!m.ram_writes);firmware[10]^=1U;
 m.arm_reset=0;assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_NOT_READY&&!m.ram_writes);
 m.arm_reset=1;m.d11_reset=0;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_NOT_READY&&!m.ram_writes);
 m.d11_reset=1;m.chip_id=0x10044330U;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_ID&&!m.ram_writes);m.chip_id=0x10034330U;
 struct i9100_bcm_firmware copy=loader;
 before=m.ops;assert(i9100_bcm_fw_load(&copy,firmware,vars)==I9100_BCM_FW_ARGUMENT&&m.ops==before);
 assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);
 assert(i9100_bcm_enable(&bus,0,0,0)==I9100_BCM_BUSY);
 close_loader();
}
static void download(void)
{
 const uint32_t sizes[]={8,511,512,513,1025,32767,32768,32769,65539,131075};
 for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++){
  prepare(sizes[n],12);open_loader();assert(!i9100_bcm_fw_load(&loader,firmware,vars));
  assert(loader.loaded&&loader.token_verified&&chip.ram_image_owned&&m.arm_reset==1U&&m.ram_clock==1U);
  assert(loader.confirmed_firmware_bytes==sizes[n]&&loader.confirmed_nvram_bytes==sizeof(vars)&&crypto_calls==4U);
  assert(!memcmp(ram_image,firmware,sizes[n]));
  for(unsigned i=sizes[n];i<loader.layout.firmware_padded;i++)assert(!ram_image[i]);
  assert(!memcmp(ram_image+loader.layout.nvram_address,vars,sizeof(vars)));
  for(unsigned i=sizeof(vars);i<loader.layout.nvram_padded;i++)assert(!ram_image[loader.layout.nvram_address+i]);
  assert(ramword(loader.layout.token_address)==loader.layout.token&&m.token_writes==2U);window_original();
  unsigned before=m.ops;
  assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_BUSY&&m.ops==before);
  assert(i9100_bcm_fw_close(&loader)==I9100_BCM_FW_BUSY&&chip.firmware_holds==1U);
  close_loader();assert(!ramword(sizeof(ram_image)-4U));
 }
}
static void failures(void)
{
 prepare(1025U,12);open_loader();unsigned before=m.ops;
 assert(!i9100_bcm_fw_load(&loader,firmware,vars));unsigned count=m.ops-before;close_loader();
 for(unsigned n=1;n<=count;n++){
  prepare(1025U,12);open_loader();m.fail_op=m.ops+n;
  assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_IO&&!loader.loaded&&!loader.token_verified);
  if(chip.ram_image_owned||bus.window_restore_pending)assert(i9100_bcm_fw_close(&loader)==I9100_BCM_FW_BUSY);
  close_loader();if(m.ram_writes)assert(!ramword(sizeof(ram_image)-4U));
 }
 prepare(1025U,12);open_loader();m.ignore_ram_write=1;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_VERIFY&&chip.ram_image_owned&&!loader.loaded);
 m.ignore_ram_write=0;close_loader();
 prepare(1025U,12);open_loader();m.ignore_token_write=1;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_VERIFY&&chip.ram_image_owned&&!loader.loaded);
 assert(i9100_bcm_fw_abort(&loader)==I9100_BCM_FW_VERIFY&&chip.ram_image_owned);
 m.ignore_token_write=0;close_loader();
 prepare(1025U,12);open_loader();m.corrupt_ram_address=511U;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_VERIFY&&!loader.loaded);close_loader();
 prepare(1025U,12);open_loader();m.mutate_source=firmware;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_HASH&&!loader.loaded&&m.token_writes==1U);
 close_loader();
 prepare(1025U,12);open_loader();m.ignore_ram_write=1;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_VERIFY);m.ignore_ram_write=0;m.arm_reset=0;
 assert(i9100_bcm_fw_abort(&loader)==I9100_BCM_FW_NOT_READY&&chip.ram_image_owned);
 m.arm_reset=1;close_loader();
 prepare(1025U,12);open_loader();m.step=2000000000ULL;
 assert(i9100_bcm_fw_load(&loader,firmware,vars)==I9100_BCM_FW_TIMEOUT&&!loader.loaded&&chip.ram_image_owned);
 m.step=0;close_loader();
}
static void reload_counts(void)
{
 prepare(1025U,12);open_loader();assert(!i9100_bcm_fw_load(&loader,firmware,vars));
 assert(!i9100_bcm_fw_abort(&loader));assert(!i9100_bcm_fw_load(&loader,firmware,vars));
 assert(loader.confirmed_firmware_bytes==1025U&&loader.confirmed_nvram_bytes==sizeof(vars));close_loader();
}
int main(void)
{layout_and_vars();safe_preflight();download();failures();reload_counts();puts("PASS exact SHA256/MAC/preflight no-IO; halted BCM RAM chunks/window crossings/padding/PR85623/readback/token-last/reload counters; every transport failure/failed-abort owner; no core activation/RF");return 0;}
