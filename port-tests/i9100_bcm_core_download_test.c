/* SPDX-License-Identifier: BSD-2-Clause */
#include <openssl/sha.h>
#include "i9100_bcm_core_download.h"
#include "i9100_bcm_firmware.h"
#include "i9100_bcm_wire_model.h"
static void setup_core(void)
{
 setup(8);m.words--;core(0x812,16,0x18001000U,0x18101000U,1);m.erom[m.words++]=15U;
 assert(!i9100_bcm_chip_probe(&chip,1,1));m.core_actions_allowed=1;
 m.arm_reset=0x12000000U;m.d11_reset=0x34000000U;m.ram_reset=0x56000000U;
 m.cm3_control=0xa5000030U;m.d11_control=0x5a000090U;m.ram_clock=0xc3000021U;
}
static void close_core(void)
{
 if(chip.core_download_owned)assert(!i9100_bcm_core_download_stop(&chip));
 assert(!chip.core_download_owned);finish();
}
static int digest(void *cookie,const void *bytes,uint32_t n,uint8_t out[32])
{(void)cookie;return SHA256(bytes,n,out)?0:-1;}
static uint8_t ram[393216];
static void integration(void)
{
 setup_core();assert(!i9100_bcm_core_download_prepare(&chip,1));
 assert(chip.core_download_ready&&chip.core_download_owned&&!chip.ram_ready&&chip.core_stage==6U);
 assert(m.arm_reset==0x12000001U&&m.d11_reset==0x34000001U&&m.ram_reset==0x56000000U);
 assert(m.cm3_control==0xa5000033U&&m.d11_control==0x5a000097U&&m.ram_clock==0xc3000021U);
 unsigned before=m.ops;assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY&&m.ops==before);
 assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_BUSY&&m.ops==before);
 assert(!i9100_bcm_chip_ram_probe(&chip)&&chip.ram_size==sizeof(ram));
 m.ram=ram;m.ram_capacity=sizeof(ram);memset(ram,0xa5,sizeof(ram));
 const uint8_t image[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 const uint8_t vars[]="macaddr=02:11:22:33:44:66\0boardtype=0x053e\0\0";
 struct i9100_bcm_fw_profile p={0};p.chip_revision=3;p.ram_bytes=sizeof(ram);p.board_data_reviewed=1;
 p.firmware_bytes=sizeof(image);p.nvram_bytes=sizeof(vars);const uint8_t mac[6]={2,0x11,0x22,0x33,0x44,0x66};
 memcpy(p.verified_factory_mac,mac,6);assert(SHA256(image,sizeof(image),p.firmware_sha256));assert(SHA256(vars,sizeof(vars),p.nvram_sha256));
 struct i9100_bcm_firmware fw={0};struct i9100_bcm_fw_integrity io={0,digest};
 assert(!i9100_bcm_fw_init(&fw,1,&chip,&p,&io));before=m.ops;
 assert(i9100_bcm_core_download_stop(&chip)==I9100_BCM_CHIP_BUSY&&m.ops==before);
 assert(!i9100_bcm_fw_load(&fw,image,vars)&&fw.loaded);assert((m.arm_reset&1U)&&(m.d11_reset&1U));
 assert(!i9100_bcm_fw_abort(&fw));assert(!i9100_bcm_fw_close(&fw));
 close_core();assert(m.arm_reset&1U);assert(m.d11_reset&1U);assert(m.ram_reset&1U);
}
static void guards(void)
{
 setup_core();unsigned before=m.ops;
 assert(i9100_bcm_core_download_prepare(&chip,0)==I9100_BCM_CHIP_ARGUMENT&&m.ops==before);
 struct i9100_bcm_chip copy=chip;
 assert(i9100_bcm_core_download_prepare(&copy,1)==I9100_BCM_CHIP_ARGUMENT&&m.ops==before);
 chip.firmware_holds=1;assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_BUSY&&m.ops==before);chip.firmware_holds=0;
 m.chip_id=0x10044330U;assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_ID&&!m.core_writes);m.chip_id=0x10034330U;
 close_core();setup(8);assert(!i9100_bcm_chip_probe(&chip,1,1));before=m.ops;
 assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_UNSUPPORTED&&m.ops==before);finish();
 setup_core();m.defer_reset_reads=3;
 assert(!i9100_bcm_core_download_prepare(&chip,1)&&chip.core_download_ready);close_core();
}
static void faults(void)
{
 setup_core();unsigned before=m.ops;assert(!i9100_bcm_core_download_prepare(&chip,1));unsigned count=m.ops-before;close_core();
 for(unsigned n=1;n<=count;n++){
  setup_core();m.fail_op=m.ops+n;
  assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_IO&&!chip.core_download_ready&&chip.core_error);
  close_core();window_original();
 }
 setup_core();m.drop_core_control=1;
 assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_IO&&chip.core_download_owned);
 assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);m.drop_core_control=0;close_core();
 for(unsigned core_index=1;core_index<=3U;core_index++){
  setup_core();m.reset_stall=core_index;m.freeze_time=1;
  assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_TIMEOUT&&!chip.core_download_ready&&chip.core_download_owned);
  assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);m.reset_stall=m.freeze_time=0;close_core();
 }
 setup_core();m.reset_status_busy=1;m.freeze_time=1;
 assert(i9100_bcm_core_download_prepare(&chip,1)==I9100_BCM_CHIP_TIMEOUT&&chip.core_download_owned);
 assert(!(m.arm_reset&1U)&&!(m.ram_reset&1U));m.reset_status_busy=m.freeze_time=0;close_core();
 setup_core();assert(!i9100_bcm_core_download_prepare(&chip,1));m.fail_op=m.ops+1U;
 assert(i9100_bcm_core_download_stop(&chip)==I9100_BCM_CHIP_IO&&chip.core_download_owned&&!chip.core_download_ready&&!chip.ram_ready);
 assert(i9100_bcm_chip_close(&chip)==I9100_BCM_CHIP_BUSY);close_core();
}
int main(void)
{integration();guards();faults();puts("PASS F1/DMP CM3+D11 held reset/SOCRAM-only release/masked fields/delayed reset/busy status/all transactions/frozen-clock quarantine; RAM-probe+SHA-staging+abort+safe-stop lifecycle, no CPU/D11 activation");return 0;}
