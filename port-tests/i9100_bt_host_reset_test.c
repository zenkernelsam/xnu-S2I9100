/* SPDX-License-Identifier: BSD-2-Clause; actual H4 and reset requalification. */
#define main bt_boot_reference_main
#include "i9100_bt_boot_test.c"
#undef main
static void ready(struct i9100_bt_boot *b,int reset)
{setup(b);assert(!i9100_bt_boot_allow_host_reset(b,reset));bring_up(b,1);model.head=model.tail=0;model.commands=0;}
static void restored(struct i9100_bt_boot *b)
{
 assert(!i9100_bt_boot_host_reset(b));assert(model.commands==1&&model.tx_bytes==4&&model.tx[1]==3&&model.tx[2]==0x0c);
 assert(!b->status.ready&&b->status.host_reset_active&&b->status.protocol_epoch==1);
 ack(b,0,1,1);assert(b->status.protocol_epoch==2&&b->status.host_reset_confirmed==1&&!b->status.acl_packets&&!model.delivered);
 assert(b->status.host_reset_complete_raw[0]==0x0e&&b->status.host_reset_complete_raw[2]==1);
 settle(b);assert(b->status.phase==I9100_BT_BOOT_HOST_VERSION);
 ack(b,0,1,1);assert(b->status.phase==I9100_BT_BOOT_HOST_ADDRESS);
 ack(b,0,1,1);assert(b->status.phase==I9100_BT_BOOT_HOST_BUFFER);
 ack(b,0,1,1);assert(b->status.phase==I9100_BT_BOOT_HOST_FEATURES);
 ack(b,0,1,1);assert(b->status.phase==I9100_BT_BOOT_HOST_SCAN_OFF);
 ack(b,0,1,1);assert(b->status.ready&&!b->status.host_reset_active&&b->status.protocol_epoch==2&&model.commands==6);
 assert(model.delivered==1U&&b->status.host_reset_valid_fields==47U&&b->status.acl_packets==8&&b->status.generation==1U);
}
int main(void)
{
 assert(!bt_boot_reference_main());struct i9100_bt_boot b;
 ready(&b,0);assert(i9100_bt_boot_host_reset(&b)==I9100_BT_BOOT_DISABLED&&!model.commands);
 ready(&b,1);restored(&b);assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);uint8_t incoming[]={4,0xff,1,7};queue(incoming,4);assert(i9100_bt_boot_host_reset(&b)==I9100_BT_BUSY&&!model.commands&&b.rx_used==4);
 model.ns++;assert(!i9100_bt_boot_step(&b,1)&&model.delivered==1);assert(!i9100_bt_boot_host_reset(&b)&&model.commands==1);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);queue(incoming,1);model.ns++;assert(!i9100_bt_boot_step(&b,1)&&b.h4.used==1);
 assert(i9100_bt_boot_host_reset(&b)==I9100_BT_BUSY&&!model.commands);assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_command(&b,0x1001,0,0));assert(i9100_bt_boot_host_reset(&b)==I9100_BT_BUSY&&model.commands==1);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));uint8_t status[]={4,0x0f,4,0,1,3,0x0c};queue(status,7);model.ns++;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_FRAME&&b.status.protocol_epoch==1U&&!b.status.host_reset_confirmed);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));ack(&b,1,1,0);assert(b.status.fault&&b.status.protocol_epoch==1U&&!model.delivered&&
  b.status.host_reset_reply_seen&&b.status.host_reset_complete_raw[5]==1U);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));model.ns+=1000000001ULL;ack(&b,0,1,0);
 assert(b.status.fault&&b.status.protocol_epoch==1U&&model.commands==1);assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);model.partial=1;assert(i9100_bt_boot_host_reset(&b)==I9100_BT_IO&&b.status.protocol_epoch==1U&&model.commands==1);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));ack(&b,0,1,0);settle(&b);profile.patched.hci_revision^=1;ack(&b,0,1,0);
 assert(b.status.error==I9100_BT_BOOT_ID&&!b.status.ready&&!model.delivered&&b.status.protocol_epoch==2U);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));ack(&b,0,0,0);settle(&b);
 assert(b.deferred_opcode==0x1001U&&model.commands==1U&&!b.status.ready);
 uint8_t credit[]={4,0x0e,3,1,0,0};queue(credit,6);model.ns++;assert(!i9100_bt_boot_step(&b,1)&&model.commands==2);
 ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);
 assert(b.status.ready&&b.status.host_credit_present&&model.delivered==2U&&b.status.host_reset_complete_raw[2]==0);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));ack(&b,0,1,0);settle(&b);
 ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,0,0);
 assert(!b.status.ready&&b.status.error==I9100_BT_BOOT_ID&&!model.delivered);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));ack(&b,0,1,0);settle(&b);
 model.ns=b.status.host_reset_started_ns+1500000001ULL;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_BOOT_TIMEOUT&&!model.delivered&&model.commands==2U);
 assert(!i9100_bt_boot_stop(&b));
 ready(&b,1);assert(!i9100_bt_boot_host_reset(&b));uint8_t early_credit[]={4,0x0e,3,0,0,0};queue(early_credit,6);model.ns++;
 assert(!i9100_bt_boot_step(&b,1)&&!b.status.host_credit_present);ack(&b,0,1,0);settle(&b);
 ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);ack(&b,0,1,0);
 assert(b.status.ready&&model.delivered==1&&!b.status.host_credit_present);assert(!i9100_bt_boot_stop(&b));
 puts("PASS actual host Reset:defaultOFF/idle RX preservation,exact Complete not Status,protocol epoch,real identity/buffer requalification,only original reply,late/partial/version failures and no physical-generation change");return 0;
}
