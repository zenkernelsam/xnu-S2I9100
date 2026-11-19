/* SPDX-License-Identifier: BSD-2-Clause; original replies, no HCD/expected ROM. */
#define main bt_boot_reference_main
#include "i9100_bt_boot_test.c"
#undef main
#include "i9100_bt_probe.h"
static void probe_setup(struct i9100_bt_probe *p)
{
 struct i9100_bt_boot b;setup(&b);assert(!i9100_bt_boot_stop(&b));model.commands=model.quiesces=0;memset(p,0,sizeof(*p));
 struct i9100_bt_probe_io bio={{&model,lock,unlock,write,quiesce,now},&model,read,verify};
 assert(!i9100_bt_probe_init(p,1,&bio,1,10000));assert(!model.commands&&!p->status.valid_fields);
}
static void probe_ack(struct i9100_bt_probe *p,unsigned split)
{
 uint16_t op=model.tx[1]|((uint16_t)model.tx[2]<<8);uint8_t reply[255]={4,0x0e,4,1,(uint8_t)op,(uint8_t)(op>>8),0};unsigned bytes=7;
 if(op==0x1001U){reply[2]=12;reply[7]=6;reply[8]=4;reply[10]=6;reply[11]=15;reply[13]=3;reply[14]=0x41;bytes=15;}
 else if(op==0x0c14U){reply[2]=252;memcpy(reply+7,"BCM4330B1",9);bytes=255;}
 else if(op==0x1009U){reply[2]=10;uint8_t default_address[6]={0,0,0,0xb1,0x30,0x43};memcpy(reply+7,default_address,6);bytes=13;}
 else if(op==0x1005U){reply[2]=11;reply[7]=0xfd;reply[8]=3;reply[9]=64;reply[10]=8;bytes=14;}
 else {assert(op==0x1003U);reply[2]=12;reply[7]=0xff;bytes=15;}
 if(split&&split<bytes){queue(reply,split);model.ns++;assert(!i9100_bt_probe_step(p,1));queue(reply+split,bytes-split);}
 else queue(reply,bytes);
 model.ns++;assert(!i9100_bt_probe_step(p,1));
}
static void read_only_identity(void)
{
 for(unsigned split=0;split<9U;split++){
  struct i9100_bt_probe p;probe_setup(&p);assert(!i9100_bt_probe_start(&p));
  const uint16_t commands[5]={0x1001,0x0c14,0x1009,0x1005,0x1003};
  for(unsigned n=0;n<5;n++){assert((model.tx[1]|((uint16_t)model.tx[2]<<8))==commands[n]&&model.tx_bytes==4);probe_ack(&p,split);}
  assert(p.status.completed&&!p.status.fault&&p.status.valid_fields==31&&model.commands==5);
  assert(p.status.version.manufacturer==15&&p.status.version.hci_revision==4&&p.status.address_unusable==1);
  assert(!memcmp(p.status.name_raw+1,"BCM4330B1",9)&&p.status.address_raw[4]==0xb1);
  unsigned before=model.commands;assert(!i9100_bt_probe_step(&p,4)&&model.commands==before);
  assert(!i9100_bt_probe_stop(&p)&&p.status.closed&&p.status.completed&&p.status.valid_fields==31);
  struct i9100_bt_probe_status s;assert(!i9100_bt_probe_status(&p,&s)&&s.completed&&!s.pending_opcode);
 }
}
static void probe_failures(void)
{
 struct i9100_bt_probe p;probe_setup(&p);assert(!i9100_bt_probe_start(&p));
 uint8_t status[]={4,0x0f,4,0,1,1,0x10};queue(status,sizeof(status));model.ns++;
 assert(i9100_bt_probe_step(&p,1)==I9100_BT_FRAME&&p.status.fault&&!p.status.valid_fields&&model.commands==1);assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);assert(!i9100_bt_probe_start(&p));uint8_t wrong[]={4,0x0e,4,99,3,0x0c,0};queue(wrong,sizeof(wrong));model.ns++;
 assert(i9100_bt_probe_step(&p,1)==I9100_BT_UNMATCHED&&p.status.fault&&!p.h4.credits&&model.commands==1);assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);assert(!i9100_bt_probe_start(&p));model.ns+=1000000001ULL;
 assert(i9100_bt_probe_step(&p,1)==I9100_BT_TIMEOUT&&p.status.fault&&model.commands==1);assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);model.partial=1;assert(i9100_bt_probe_start(&p)==I9100_BT_IO&&p.status.fault);model.partial=0;assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);model.link_bad=1;unsigned before=model.commands;assert(i9100_bt_probe_start(&p)==I9100_BT_BOOT_OWNER&&model.commands==before);model.link_bad=0;assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);assert(!i9100_bt_probe_start(&p));model.quiesce_fail=1;
 assert(i9100_bt_probe_stop(&p)==I9100_BT_IO&&!p.status.closed);model.quiesce_fail=0;assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);struct i9100_bt_probe copied=p;assert(i9100_bt_probe_start(&copied)==I9100_BT_BOOT_ARGUMENT&&!model.commands);
 assert(i9100_bt_probe_status(&p,(struct i9100_bt_probe_status *)&p)==I9100_BT_BOOT_ARGUMENT);assert(!i9100_bt_probe_stop(&p));
 probe_setup(&p);assert(!i9100_bt_probe_start(&p));uint8_t version_error[]={4,0x0e,4,1,1,0x10,1};queue(version_error,sizeof(version_error));model.ns++;
 assert(i9100_bt_probe_step(&p,1)==I9100_BT_CONTROLLER&&p.status.last_event_bytes==6&&!p.status.valid_fields);assert(!i9100_bt_probe_stop(&p));
}
int main(void)
{read_only_identity();probe_failures();puts("PASS ROM probe: exactly5 read commands/no reset or HCD, original raw version/name/default-BDADDR/buffer/features, fragmentation, complete-vs-status/late/partial/stale/owner faults and retained cleanup");return 0;}
