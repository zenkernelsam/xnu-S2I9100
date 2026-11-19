/* SPDX-License-Identifier: BSD-2-Clause. Actual H4+FC18 transition. */
#define main bt_boot_reference_main
#include "i9100_bt_boot_test.c"
#undef main
static unsigned validates,changes,baud_fault;static uint32_t host_baud;
static int baud_validate(void *p,uint32_t expected,uint32_t target)
{assert(p==&model&&expected==host_baud&&target==2400000U);validates++;return baud_fault==1?I9100_BT_BUSY:0;}
static int baud_change(void *p,uint32_t expected,uint32_t target)
{assert(p==&model&&!model.locked&&expected==115200U&&target==2400000U);changes++;
 if(baud_fault==2)return -1;host_baud=target;return 0;}
static void prepared(struct i9100_bt_boot *b,unsigned fault)
{
 setup(b);b->io.validate_baud=baud_validate;b->io.change_baud=baud_change;
 assert(!i9100_bt_boot_allow_baud(b,1));bring_up(b,1);validates=changes=0;baud_fault=fault;host_baud=115200;
 model.head=model.tail=model.commands=model.delivered=0;
}
int main(void)
{
 assert(!bt_boot_reference_main());struct i9100_bt_boot b;setup(&b);bring_up(&b,0);
 assert(i9100_bt_boot_change_baud(&b,2400000U)==I9100_BT_BOOT_DISABLED);assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);assert(!i9100_bt_boot_change_baud(&b,2400000U));
 uint8_t expected[]={1,0x18,0xfc,6,0,0,0,0x9f,0x24,0};
 assert(model.tx_bytes==sizeof(expected)&&!memcmp(model.tx,expected,sizeof(expected))&&validates==1&&!changes&&b.status.baud_active&&!b.status.ready);
 ack(&b,0,1,1);assert(changes==1&&host_baud==2400000U&&b.status.current_baud==2400000U&&b.status.phase==I9100_BT_BOOT_BAUD_VERIFY);
 ack(&b,0,1,1);assert(b.status.ready&&!b.status.baud_active&&b.status.baud_completed==1&&b.status.generation==1&&b.status.protocol_epoch==1&&!model.delivered);
 assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);assert(!i9100_bt_boot_change_baud(&b,2400000));ack(&b,1,1,0);
 assert(b.status.fault&&!changes&&host_baud==115200&&b.status.baud_reply_seen&&b.status.baud_complete_raw[5]==1);
 assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);assert(!i9100_bt_boot_change_baud(&b,2400000));uint8_t accepted[]={4,0x0f,4,0,1,0x18,0xfc};queue(accepted,7);model.ns++;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_FRAME&&!changes&&host_baud==115200);assert(!i9100_bt_boot_stop(&b));
 prepared(&b,2);assert(!i9100_bt_boot_change_baud(&b,2400000));ack(&b,0,1,0);
 assert(b.status.error==I9100_BT_BOOT_IO&&changes==1&&!b.status.baud_host_changed);assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);model.partial=1;assert(i9100_bt_boot_change_baud(&b,2400000)==I9100_BT_IO&&!changes&&model.commands==1);
 assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);assert(!i9100_bt_boot_change_baud(&b,2400000));model.ns+=1000000001ULL;ack(&b,0,1,0);
 assert(b.status.fault&&!changes&&model.commands==1);assert(!i9100_bt_boot_stop(&b));
 prepared(&b,0);assert(!i9100_bt_boot_change_baud(&b,2400000));ack(&b,0,1,0);profile.patched.hci_revision^=1;ack(&b,0,1,0);
 assert(!b.status.ready&&b.status.error==I9100_BT_BOOT_ID&&host_baud==2400000U&&!b.status.baud_completed);assert(!i9100_bt_boot_stop(&b));
 prepared(&b,1);assert(i9100_bt_boot_change_baud(&b,2400000)==I9100_BT_BUSY&&!model.commands&&!b.status.baud_active);assert(!i9100_bt_boot_stop(&b));
 puts("PASS actual FC18 byte layout/defaultOFF/actual Complete before host switch,new-speed version,old RX/pending/late/Status/partial/host failure quarantine; clock/electrical facades");return 0;
}
