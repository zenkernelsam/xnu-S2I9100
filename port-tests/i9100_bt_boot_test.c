/* SPDX-License-Identifier: BSD-2-Clause; actual H4/bootstrap with controller model. */
#include "i9100_bt_boot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
struct bt_model {
 uint64_t ns;unsigned locked,commands,reads,delivered,quiesces,link_bad,write_fail,partial,read_fail,quiesce_fail,hash_fail,zero_credit;
 uint8_t tx[259],rx[16384];unsigned tx_bytes,head,tail;
};
static struct bt_model model;
static uint8_t hcd[]={0x2e,0xfc,0,0x4c,0xfc,5,0,0,0x20,0,0xab,0x4e,0xfc,4,1,0,0x20,0};
static struct i9100_bt_hcd_record records[3];
static struct i9100_bt_boot_profile profile;
static int hash(void *cookie,const void *p,uint32_t n,uint8_t digest[32])
{(void)cookie;if(model.hash_fail)return -1;assert(SHA256(p,n,digest));return 0;}
static int lock(void *p){struct bt_model *m=p;if(m->locked)return -1;m->locked=1;return 0;}
static void unlock(void *p){struct bt_model *m=p;assert(m->locked);m->locked=0;}
static uint64_t now(void *p){return ((struct bt_model *)p)->ns;}
static int quiesce(void *p){struct bt_model *m=p;assert(m->locked);m->quiesces++;return m->quiesce_fail?-1:0;}
static int verify(void *p,uint32_t generation){assert(generation==1);return ((struct bt_model *)p)->link_bad?-1:0;}
static int write(void *p,const uint8_t *bytes,unsigned n,unsigned *sent)
{struct bt_model *m=p;assert(m->locked&&n<=259);memcpy(m->tx,bytes,n);m->tx_bytes=n;m->commands++;*sent=m->partial?n-1U:n;return m->write_fail?-1:0;}
static int read(void *p,uint8_t *bytes,unsigned cap,unsigned *got)
{struct bt_model *m=p;assert(!m->locked);m->reads++;*got=0;if(m->read_fail)return -1;
 unsigned amount=m->tail-m->head;if(amount>cap)amount=cap;memcpy(bytes,m->rx+m->head,amount);m->head+=amount;*got=amount;return 0;}
static int receive(void *p,const struct i9100_bt_frame *frame)
{struct bt_model *m=p;assert(!m->locked&&frame->length&&frame->bytes);m->delivered++;return 0;}
static struct i9100_bt_boot_io io={{&model,lock,unlock,write,quiesce,now},&model,read,verify,hash,receive,0,0};
static void setup(struct i9100_bt_boot *b)
{
 memset(&model,0,sizeof(model));model.ns=1000000;memset(&profile,0,sizeof(profile));memset(b,0,sizeof(*b));
 profile.board_data_reviewed=1;profile.ram_base=0x00200000;profile.ram_bytes=0x10000;profile.launch_address=0x00200001;profile.timeout_ms=30000;
 profile.rom=(struct i9100_bt_version){6,6,1,15,0x4103};profile.patched=(struct i9100_bt_version){6,6,2,15,0x4103};
 const uint8_t bd[6]={1,2,3,4,5,6};memcpy(profile.factory_bdaddr_hci,bd,6);assert(!hash(0,hcd,sizeof(hcd),profile.hcd_sha256));
 uint32_t count=0;assert(!i9100_bt_hcd_audit(hcd,sizeof(hcd),&profile,hash,0,records,3,&count)&&count==3);
 assert(!i9100_bt_boot_init(b,1,&io,&profile,hcd,sizeof(hcd),records,3,1));
}
static void queue(const uint8_t *bytes,unsigned n)
{assert(model.tail+n<=sizeof(model.rx));memcpy(model.rx+model.tail,bytes,n);model.tail+=n;}
static void ack(struct i9100_bt_boot *b,unsigned status,unsigned credits,unsigned fragment)
{
 uint16_t op=(uint16_t)(model.tx[1]|((uint16_t)model.tx[2]<<8));uint8_t bytes[32]={4,0x0e,4,(uint8_t)credits,(uint8_t)op,(uint8_t)(op>>8),(uint8_t)status};unsigned length=7;
 if(op==0x1001U){const struct i9100_bt_version *v=b->status.phase==I9100_BT_BOOT_ROM_VERSION?&profile.rom:&profile.patched;
  bytes[2]=12;bytes[7]=v->hci_version;bytes[8]=(uint8_t)v->hci_revision;bytes[9]=(uint8_t)(v->hci_revision>>8);bytes[10]=v->lmp_version;
  bytes[11]=(uint8_t)v->manufacturer;bytes[12]=(uint8_t)(v->manufacturer>>8);bytes[13]=(uint8_t)v->lmp_subversion;bytes[14]=(uint8_t)(v->lmp_subversion>>8);length=15;
 }else if(op==0x1009U){bytes[2]=10;memcpy(bytes+7,profile.factory_bdaddr_hci,6);length=13;
 }else if(op==0x1005U){bytes[2]=11;bytes[7]=0xfd;bytes[8]=3;bytes[9]=64;bytes[10]=8;length=14;
 }else if(op==0x1003U){bytes[2]=12;bytes[7]=0xff;length=15;}
 if(fragment&&fragment<length){queue(bytes,fragment);model.ns++;assert(!i9100_bt_boot_step(b,1));queue(bytes+fragment,length-fragment);}
 else queue(bytes,length);
 model.ns++;assert(i9100_bt_boot_step(b,1)==0||b->status.fault);
}
static void settle(struct i9100_bt_boot *b)
{unsigned before=model.commands;model.ns=b->status.delay_until_ns-1;assert(!i9100_bt_boot_step(b,1)&&model.commands==before);
 model.ns++;assert(!i9100_bt_boot_step(b,1));}
static void bring_up(struct i9100_bt_boot *b,unsigned fragment)
{
 assert(!i9100_bt_boot_start(b));ack(b,0,1,fragment);assert(b->status.phase==I9100_BT_BOOT_RESET_DELAY);settle(b);
 assert(b->status.phase==I9100_BT_BOOT_ROM_VERSION);ack(b,0,1,fragment);assert(b->status.phase==I9100_BT_BOOT_MINIDRV);
 ack(b,0,1,fragment);assert(b->status.phase==I9100_BT_BOOT_MINIDRV_DELAY&&b->status.confirmed_records==1);settle(b);
 assert(b->status.phase==I9100_BT_BOOT_RECORD&&b->status.pending_opcode==0xfc4c);ack(b,0,1,fragment);
 assert(b->status.pending_opcode==0xfc4e&&b->status.launch_possible);ack(b,0,1,fragment);assert(b->status.phase==I9100_BT_BOOT_LAUNCH_DELAY);settle(b);
 ack(b,0,1,fragment);assert(b->status.phase==I9100_BT_BOOT_POST_RESET_DELAY);settle(b);
 ack(b,0,1,fragment);ack(b,0,1,fragment);ack(b,0,1,fragment);ack(b,0,1,fragment);ack(b,0,1,fragment);
 assert(b->status.ready&&!b->status.fault&&b->status.confirmed_records==3&&b->status.acl_mtu==1021&&b->status.acl_packets==8);
 assert(b->status.mutation_possible&&b->status.launch_possible&&!memcmp(b->status.address,profile.factory_bdaddr_hci,6));
}
static void normal(void)
{
 for(unsigned split=0;split<7;split++){
  struct i9100_bt_boot b;setup(&b);bring_up(&b,split);
  unsigned before=model.commands;assert(i9100_bt_boot_command(&b,0xfc01,0,0)==I9100_BT_BOOT_ARGUMENT&&model.commands==before);
  assert(!i9100_bt_boot_command(&b,0x1001,0,0));ack(&b,0,1,0);assert(model.delivered==1&&b.status.ready);
  assert(!i9100_bt_boot_command(&b,0x1001,0,0));ack(&b,1,1,0);
  assert(b.status.ready&&!b.status.fault&&model.delivered==2&&b.status.controller_errors==1);
  uint8_t event[]={4,0xff,1,0xab};queue(event,4);model.ns++;assert(!i9100_bt_boot_step(&b,1)&&model.delivered==3);
  unsigned calls=model.commands;assert(i9100_bt_boot_command(&b,0x0c35,0,0)==I9100_BT_BOOT_ARGUMENT&&model.commands==calls);
  assert(!i9100_bt_boot_stop(&b)&&b.status.closed&&b.status.mutation_possible&&b.status.launch_possible);
 }
}
static void faults(void)
{
 struct i9100_bt_boot b;setup(&b);assert(!i9100_bt_boot_start(&b));model.ns+=1000000001ULL;
 unsigned before=model.commands;ack(&b,0,1,0);assert(b.status.fault&&b.status.error==I9100_BT_TIMEOUT&&model.commands==before);assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));uint8_t status[]={4,0x0f,4,0,1,3,0x0c};queue(status,7);model.ns++;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_FRAME&&b.status.fault&&!b.status.mutation_possible);assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));uint8_t wrong[]={4,0x0e,4,99,9,0x10,0};queue(wrong,7);model.ns++;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_UNMATCHED&&b.status.fault&&b.h4.credits==0);assert(!i9100_bt_boot_stop(&b));
 setup(&b);model.partial=1;assert(i9100_bt_boot_start(&b)==I9100_BT_IO&&b.status.fault&&model.commands==1);assert(!i9100_bt_boot_stop(&b));
 setup(&b);model.link_bad=1;before=model.commands;assert(i9100_bt_boot_start(&b)==I9100_BT_BOOT_OWNER&&model.commands==before);model.link_bad=0;assert(!i9100_bt_boot_stop(&b));
 setup(&b);queue((uint8_t[]){0xab},1);assert(i9100_bt_boot_start(&b)==I9100_BT_FRAME&&!model.commands);assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));for(unsigned n=0;n<4095;n++)assert(!i9100_bt_boot_step(&b,1));
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_BOOT_CLOCK&&b.status.fault);assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));ack(&b,0,0,0);settle(&b);assert(b.deferred_opcode==0x1001U&&!b.h4.pending);
 uint8_t credit[]={4,0x0e,3,1,0,0};queue(credit,6);model.ns++;assert(!i9100_bt_boot_step(&b,1)&&b.h4.pending&&b.status.phase==I9100_BT_BOOT_ROM_VERSION);assert(!i9100_bt_boot_stop(&b));
 setup(&b);struct i9100_bt_boot copy=b;assert(i9100_bt_boot_start(&copy)==I9100_BT_BOOT_ARGUMENT&&!model.commands);
 assert(i9100_bt_boot_status(&b,(struct i9100_bt_boot_status *)&b)==I9100_BT_BOOT_ARGUMENT);assert(!i9100_bt_boot_stop(&b));
 setup(&b);model.quiesce_fail=1;assert(i9100_bt_boot_stop(&b)==I9100_BT_IO&&!b.status.closed);model.quiesce_fail=0;assert(!i9100_bt_boot_stop(&b));
}
static void file_checks(void)
{
 struct i9100_bt_boot b;setup(&b);uint32_t count=99;uint8_t changed[sizeof(hcd)];memcpy(changed,hcd,sizeof(hcd));changed[10]^=1;
 assert(i9100_bt_hcd_audit(changed,sizeof(changed),&profile,hash,0,records,3,&count)==I9100_BT_BOOT_HASH&&count==0);
 changed[10]^=1;changed[8]=0x30;assert(i9100_bt_hcd_plan(changed,sizeof(changed),&profile,&count)==I9100_BT_BOOT_LAYOUT);
 assert(i9100_bt_hcd_plan(hcd,sizeof(hcd)-1U,&profile,&count)==I9100_BT_BOOT_LAYOUT);
 unsigned before=model.commands;profile.board_data_reviewed=0;assert(i9100_bt_hcd_plan(hcd,sizeof(hcd),&profile,&count)==I9100_BT_BOOT_ARGUMENT&&model.commands==before);
 profile.board_data_reviewed=1;
 assert(i9100_bt_hcd_plan(hcd,sizeof(hcd),&profile,(uint32_t *)&profile)==I9100_BT_BOOT_ARGUMENT);
 assert(i9100_bt_hcd_audit(hcd,sizeof(hcd),&profile,hash,0,(struct i9100_bt_hcd_record *)&profile,3,&count)==I9100_BT_BOOT_ARGUMENT);
 assert(!i9100_bt_boot_stop(&b));
}
static void changed_firmware_and_controller(void)
{
 struct i9100_bt_boot b;setup(&b);assert(!i9100_bt_boot_start(&b));ack(&b,0,1,0);settle(&b);
 profile.rom.hci_revision=7;ack(&b,0,1,0);assert(b.status.error==I9100_BT_BOOT_ID&&b.status.fault&&!b.status.mutation_possible);assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));ack(&b,0,1,0);settle(&b);ack(&b,0,1,0);ack(&b,0,1,0);
 unsigned calls=model.commands;hcd[10]^=1;model.ns=b.status.delay_until_ns;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_BOOT_HASH&&b.status.fault&&model.commands==calls&&!b.status.launch_possible);
 hcd[10]^=1;assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));ack(&b,0,1,0);settle(&b);ack(&b,0,1,0);ack(&b,0,1,0);
 records[1].offset++;calls=model.commands;model.ns=b.status.delay_until_ns;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_BOOT_LAYOUT&&model.commands==calls);records[1].offset--;assert(!i9100_bt_boot_stop(&b));
 setup(&b);assert(!i9100_bt_boot_start(&b));ack(&b,0,0,0);settle(&b);model.ns+=1000000001ULL;
 assert(i9100_bt_boot_step(&b,1)==I9100_BT_TIMEOUT&&b.status.fault);assert(!i9100_bt_boot_stop(&b));
}
int main(void)
{normal();faults();file_checks();changed_firmware_and_controller();puts("PASS actual Broadcom H4/bootstrap: exact command-complete/version/address,100/50/250ms waits,HCD whole/per-record SHA and ranges,fragmentation/credits,late/status/wrong/partial/clock fault quarantine,no replay/unsafe reset");return 0;}
