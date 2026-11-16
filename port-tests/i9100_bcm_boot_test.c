/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_runtime_model.h"
static void startup_transport(void)
{
 setup_boot();unsigned before=m.ops;
 assert(i9100_bcm_fw_abort(&firmware)==I9100_BCM_FW_BUSY&&m.ops==before);
 assert(i9100_bcm_fw_close(&firmware)==I9100_BCM_FW_BUSY);
 assert(!i9100_bcm_boot_start(&boot)&&boot.ready&&boot.running&&firmware.execution_possible&&bus.protocol_ready&&packets.enabled);
 assert(bus.tx_sequence==255U&&bus.tx_max==0U&&!(m.arm_reset&1U));
 uint8_t ethernet[64]={0};assert(i9100_bcm_send_data(&bus,ethernet,sizeof(ethernet),0)==I9100_BCM_CREDIT);
 const uint8_t query_buffer[4]={0};
 assert(!i9100_bcm_ioctl(&bus,1,0,query_buffer,4)&&bus.pending&&tx[4]==255U&&bus.tx_sequence==0U);
 memset(rx,0,sizeof(rx));put16(rx,32);put16(rx+2,(uint16_t)~32U);rx[4]=7;rx[5]=0;rx[7]=12;rx[9]=4;
 put32(rx+12,1);put32(rx+16,4);put32(rx+20,(uint32_t)bus.request_id<<16);memcpy(rx+28,"OKAY",4);rx_bytes=32;
 uint8_t output[2048];unsigned bytes=0;assert(!i9100_bcm_f2_receive(&packets,output,sizeof(output),&bytes)&&bytes==32U);
 struct i9100_bcm_frame frame;assert(!i9100_bcm_receive(&bus,output,bytes,&frame)&&!bus.pending&&frame.bytes==4U);
 assert(!i9100_bcm_send_data(&bus,ethernet,sizeof(ethernet),0)&&bus.tx_sequence==1U);
 before=m.ops;assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_BUSY&&m.ops==before);assert(i9100_bcm_boot_close(&boot)==I9100_BCM_BOOT_BUSY);
 close_boot();assert(!firmware.execution_possible&&(m.arm_reset&1U)&&(m.d11_reset&1U));
}
static void failures(void)
{
 setup_boot();m.no_mailbox=1;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_TIMEOUT&&boot.owned&&!boot.ready&&m.event_raised==0U);close_boot();
 /* Independent vendor allocation:SW1=FC change,SW3=HOST_INT. */
 setup_boot();m.mailbox_interrupt=0x20U;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_TIMEOUT&&m.event_raised&&!boot.ready);close_boot();
 setup_boot();m.sdio_regs[0x20U/4U]=0x80U;m.no_mailbox=1;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_TIMEOUT&&!boot.ready);close_boot();
 setup_boot();m.mailbox_pre_data=0x40000004U;
 assert(!i9100_bcm_boot_start(&boot)&&boot.ready&&m.mailbox_ack_count==2U&&bus.flow_mask==0x40U);close_boot();
 setup_boot();m.ht_ready=0;m.freeze_time=1;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_TIMEOUT&&!boot.ready&&!boot.running);m.freeze_time=0;close_boot();
 setup_boot();m.f2_ready=0;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_TIMEOUT&&!boot.ready);close_boot();
 setup_boot();m.firmware_mailbox=0x5000aU;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_PROTOCOL&&!boot.ready);close_boot();
 setup_boot();m.firmware_mailbox=0x40010U;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_HALTED&&!boot.ready);close_boot();
 setup_boot();unsigned before=m.ops;assert(!i9100_bcm_boot_start(&boot));unsigned count=m.ops-before;close_boot();
 for(unsigned n=1;n<=count;n++){
  setup_boot();m.fail_op=m.ops+n;int e=i9100_bcm_boot_start(&boot);
  assert(e==I9100_BCM_BOOT_IO&&!boot.ready);assert(card.fault);
  unsigned old=m.ops;assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_IO&&m.ops==old);
  if(boot.owned){assert(i9100_bcm_boot_stop(&boot)==I9100_BCM_BOOT_IO&&boot.owned);assert(i9100_bcm_boot_close(&boot)==I9100_BCM_BOOT_BUSY);}
  close_boot();
 }
 setup_boot();assert(!i9100_bcm_boot_start(&boot));m.reset_stall=1;
 assert(i9100_bcm_boot_stop(&boot)==I9100_BCM_BOOT_IO&&boot.owned&&firmware.execution_possible);m.reset_stall=0;close_boot();
 setup_boot();ignore_cfg_write=0x211U;
 assert(i9100_bcm_boot_start(&boot)==I9100_BCM_BOOT_IO&&boot.owned&&!boot.ready&&!card.fault);
 ignore_cfg_write=0xffffffffU;close_boot();
 setup_boot();assert(!i9100_bcm_boot_start(&boot));before=m.ops;assert(!i9100_bcm_boot_stop(&boot));count=m.ops-before;
 const uint8_t image[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 const uint8_t vars[]="macaddr=02:11:22:33:44:66\0boardtype=0x053e\0\0";unsigned idle_ops=m.ops;
 assert(i9100_bcm_fw_load(&firmware,image,vars)==I9100_BCM_FW_BUSY&&m.ops==idle_ops);
 assert(!i9100_bcm_boot_close(&boot));assert(!i9100_bcm_fw_close(&firmware));assert(!i9100_bcm_core_download_stop(&chip));finish();
 for(unsigned n=1;n<=count;n++){
  setup_boot();assert(!i9100_bcm_boot_start(&boot));m.fail_op=m.ops+n;
  assert(i9100_bcm_boot_stop(&boot)==I9100_BCM_BOOT_IO&&!boot.ready&&boot.owned&&firmware.boot_holds==1U);
  assert(i9100_bcm_boot_close(&boot)==I9100_BCM_BOOT_BUSY);close_boot();
 }
}
int main(void)
{startup_transport();failures();puts("PASS staged-SHA RAM->CM3/HT/F2/fresh mailbox v4/initial control slot/real F2+CDC reply/credit+Ethernet flow; bounded timeout/version/FWHALT/all start transactions/host fault retention/external recovery/halt before token invalidation; no real firmware or radio");return 0;}
