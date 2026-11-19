/* SPDX-License-Identifier: BSD-2-Clause; real event/credit/send module. */
#include "i9100_bt_acl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct acl_model {unsigned sends,partial,fail,owner_bad;uint8_t packet[4101];unsigned bytes;};
static struct acl_model model;
static int verify(void *p){return ((struct acl_model *)p)->owner_bad?-1:0;}
static int send(void *p,const uint8_t *data,unsigned bytes,unsigned *sent)
{struct acl_model *m=p;m->sends++;memcpy(m->packet,data,bytes);m->bytes=bytes;*sent=m->partial?bytes-1:bytes;return m->fail?-1:0;}
static void setup(struct i9100_bt_acl *a,int le)
{
 memset(a,0,sizeof(*a));memset(&model,0,sizeof(model));struct i9100_bt_boot_status boot;memset(&boot,0,sizeof(boot));
 boot.ready=1;boot.acl_mtu=1021;boot.acl_packets=8;
 if(le){boot.features[4]=0x40;boot.le_buffers_valid=1;if(le==2){boot.le_mtu=27;boot.le_packets=4;}}
 struct i9100_bt_acl_io io={&model,verify,send};assert(!i9100_bt_acl_init(a,1,&io,&boot));
}
static int event(struct i9100_bt_acl *a,uint8_t *data,unsigned bytes)
{struct i9100_bt_frame frame={4,data,bytes};return i9100_bt_acl_receive(a,&frame);}
static void connection(struct i9100_bt_acl *a,uint16_t handle,int le)
{
 uint8_t data[21]={0x03,11,0,(uint8_t)handle,(uint8_t)(handle>>8),1,2,3,4,5,6,1,0};
 if(le){uint8_t ble[21]={0x3e,19,1,0,(uint8_t)handle,(uint8_t)(handle>>8),0,0,1,2,3,4,5,6,6,0,0,0,42,0,0};assert(!event(a,ble,21));}
 else assert(!event(a,data,13));
}
static void normal_and_batches(void)
{
 struct i9100_bt_acl a;setup(&a,2);connection(&a,1,0);connection(&a,2,1);
 struct i9100_bt_acl_link one,two;assert(!i9100_bt_acl_link(&a,1,&one)&&!i9100_bt_acl_link(&a,2,&two));
 assert(one.pool==0&&two.pool==1&&one.generation!=two.generation);uint8_t payload[1021];memset(payload,0xab,sizeof(payload));
 for(unsigned n=0;n<8;n++)assert(!i9100_bt_acl_send(&a,1,one.generation,2,payload,1021));
 for(unsigned n=0;n<4;n++)assert(!i9100_bt_acl_send(&a,2,two.generation,0,payload,27));
 unsigned before=model.sends;assert(i9100_bt_acl_send(&a,1,one.generation,1,payload,3)==I9100_BA_CREDIT&&model.sends==before);
 assert(model.bytes==32&&model.packet[0]==2&&model.packet[1]==2&&model.packet[3]==27);
 assert(i9100_bt_acl_send(&a,2,two.generation,2,payload,27)==I9100_BA_ARGUMENT&&model.sends==before);
 uint8_t incoming[104]={2,0x20,100,0};memset(incoming+4,0xab,100);
 struct i9100_bt_frame inbound={2,incoming,sizeof(incoming)};
 assert(!i9100_bt_acl_receive(&a,&inbound)&&a.status.received==1); /* host receive cap differs from LE TX MTU27 */
 uint8_t complete[]={0x13,9,2,1,0,4,0,2,0,2,0};assert(!event(&a,complete,sizeof(complete)));
 assert(a.status.available[0]==4&&a.status.available[1]==2&&a.status.completed==6&&a.status.outstanding[0]==4&&a.status.outstanding[1]==2);
 uint8_t disconn[]={5,4,0,1,0,0x13};assert(!event(&a,disconn,sizeof(disconn))&&a.status.available[0]==8&&a.status.discarded==4);
 connection(&a,1,0);struct i9100_bt_acl_link fresh;assert(!i9100_bt_acl_link(&a,1,&fresh)&&fresh.generation!=one.generation);
 assert(i9100_bt_acl_send(&a,1,one.generation,2,payload,8)==I9100_BA_OWNER&&model.sends==before);
 assert(!i9100_bt_acl_send(&a,1,fresh.generation,2,payload,8));
 uint8_t acl[]={1,0x20,3,0,9,8,7};struct i9100_bt_frame frame={2,acl,sizeof(acl)};
 assert(!i9100_bt_acl_receive(&a,&frame)&&a.status.received==2);
 assert(!i9100_bt_acl_stop(&a)&&a.status.closed&&a.status.outstanding[0]==1);
}
static void shared_le_pool(void)
{
 struct i9100_bt_acl a;setup(&a,1);connection(&a,1,0);connection(&a,2,1);
 struct i9100_bt_acl_link link;assert(!i9100_bt_acl_link(&a,2,&link)&&link.pool==0);uint8_t payload=7;
 assert(!i9100_bt_acl_send(&a,2,link.generation,0,&payload,1)&&a.status.available[0]==7);
 uint8_t unknown[]={0x13,5,1,99,0,99,0};assert(!event(&a,unknown,7)&&a.status.available[0]==7&&a.status.ignored_completions==1);
 assert(!i9100_bt_acl_stop(&a));
}
static void faults(void)
{
 struct i9100_bt_acl a;setup(&a,0);connection(&a,1,0);struct i9100_bt_acl_link link;assert(!i9100_bt_acl_link(&a,1,&link));uint8_t payload[64]={0};
 assert(!i9100_bt_acl_send(&a,1,link.generation,2,payload,64));
 uint8_t bad_batch[]={0x13,9,2,1,0,1,0,1,0,1,0};
 assert(event(&a,bad_batch,11)==I9100_BA_FRAME&&a.status.available[0]==7&&a.links[0].outstanding==1&&!a.status.completed);
 setup(&a,0);connection(&a,1,0);assert(!i9100_bt_acl_link(&a,1,&link));assert(!i9100_bt_acl_send(&a,1,link.generation,2,payload,64));
 uint8_t too_many[]={0x13,5,1,1,0,2,0};assert(event(&a,too_many,7)==I9100_BA_FRAME&&a.status.available[0]==7&&!a.status.completed);
 setup(&a,0);connection(&a,1,0);connection(&a,2,0);struct i9100_bt_acl_link two;
 assert(!i9100_bt_acl_link(&a,1,&link)&&!i9100_bt_acl_link(&a,2,&two));
 assert(!i9100_bt_acl_send(&a,1,link.generation,2,payload,64)&&!i9100_bt_acl_send(&a,2,two.generation,2,payload,64));
 uint8_t mixed[]={0x13,9,2,1,0,1,0,2,0,2,0};
 assert(event(&a,mixed,11)==I9100_BA_FRAME&&a.status.available[0]==6&&a.links[0].outstanding==1&&a.links[1].outstanding==1&&!a.status.completed);
 setup(&a,0);connection(&a,1,0);assert(!i9100_bt_acl_link(&a,1,&link));model.partial=1;
 assert(i9100_bt_acl_send(&a,1,link.generation,2,payload,64)==I9100_BA_IO&&a.status.outstanding[0]==1&&a.status.available[0]==7);
 unsigned before=model.sends;assert(i9100_bt_acl_send(&a,1,link.generation,2,payload,64)==I9100_BA_FAULT&&model.sends==before);
 assert(!i9100_bt_acl_stop(&a)&&a.status.outstanding[0]==1);
 setup(&a,0);connection(&a,1,0);assert(!i9100_bt_acl_link(&a,1,&link));model.owner_bad=1;
 assert(i9100_bt_acl_send(&a,1,link.generation,2,payload,64)==I9100_BA_OWNER&&!model.sends);assert(!i9100_bt_acl_stop(&a));
 setup(&a,0);struct i9100_bt_acl copy=a;assert(i9100_bt_acl_send(&copy,1,1,0,payload,4)==I9100_BA_ARGUMENT);
 assert(i9100_bt_acl_status(&a,(struct i9100_bt_acl_status *)&a)==I9100_BA_ARGUMENT);
 setup(&a,0);for(unsigned n=0;n<I9100_BT_ACL_LINKS;n++)connection(&a,(uint16_t)n,0);
 uint8_t excess[]={3,11,0,32,0,1,2,3,4,5,6,1,0};
 assert(event(&a,excess,13)==I9100_BA_LIMIT&&a.status.links==32&&a.status.available[0]==8);
 memset(&a,0,sizeof(a));struct i9100_bt_boot_status invalid;memset(&invalid,0,sizeof(invalid));
 invalid.ready=1;invalid.acl_mtu=1021;invalid.acl_packets=8;invalid.le_mtu=27;invalid.le_packets=4;
 struct i9100_bt_acl_io io={&model,verify,send};
 assert(i9100_bt_acl_init(&a,1,&io,&invalid)==I9100_BA_ARGUMENT&&!a.status.initialized);
 invalid.le_buffers_valid=1;invalid.le_mtu=0;
 assert(i9100_bt_acl_init(&a,1,&io,&invalid)==I9100_BA_ARGUMENT&&!a.status.initialized);
}
int main(void)
{normal_and_batches();shared_le_pool();faults();puts("PASS actual HCI ACL events/packets: real connection generations,BR-LE independent/shared pools,completed/disconnect credits,atomic batch rejection,unknown no-credit,exact H4 bytes,partial IO retained/no replay,owner/defaultOFF");return 0;}
