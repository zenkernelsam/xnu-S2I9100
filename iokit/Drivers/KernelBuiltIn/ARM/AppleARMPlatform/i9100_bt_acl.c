/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_acl.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn||(x+an>y&&y+bn>x);}
static int valid(const struct i9100_bt_acl *a){return a&&a->status.initialized&&a->self==(uintptr_t)a;}
static int fault(struct i9100_bt_acl *a,int e)
{a->status.fault=1;if(!a->status.error)a->status.error=e;return e;}
static int live(struct i9100_bt_acl *a)
{
 if(!valid(a))return I9100_BA_ARGUMENT;if(!a->status.enabled||a->status.closed)return I9100_BA_DISABLED;
 if(a->status.fault)return I9100_BA_FAULT;if(a->io.verify(a->io.cookie))return fault(a,I9100_BA_OWNER);return 0;
}
static void count(uint64_t *p){if(*p!=UINT64_MAX)(*p)++;}
static void add(uint64_t *p,uint32_t n){*p=UINT64_MAX-*p<n?UINT64_MAX:*p+n;}
static int find(const struct i9100_bt_acl *a,uint16_t handle)
{for(unsigned n=0;n<I9100_BT_ACL_LINKS;n++)if(a->links[n].active&&a->links[n].handle==handle)return (int)n;return -1;}
static int consistency(const struct i9100_bt_acl *a)
{
 uint32_t outstanding[2]={0},links=0;
 for(unsigned n=0;n<I9100_BT_ACL_LINKS;n++)if(a->links[n].active){
  const struct i9100_bt_acl_link *l=&a->links[n];if(l->pool>1U||l->handle>0x0effU||!l->generation)return 0;
  if(outstanding[l->pool]>UINT32_MAX-l->outstanding)return 0;outstanding[l->pool]+=l->outstanding;links++;
 }
 return links==a->status.links&&outstanding[0]==a->status.outstanding[0]&&outstanding[1]==a->status.outstanding[1]&&
  a->status.available[0]<=a->status.packets[0]&&a->status.available[1]<=a->status.packets[1]&&
  a->status.available[0]+outstanding[0]==a->status.packets[0]&&a->status.available[1]+outstanding[1]==a->status.packets[1];
}
int i9100_bt_acl_init(struct i9100_bt_acl *a,int enabled,const struct i9100_bt_acl_io *io,const struct i9100_bt_boot_status *boot)
{
 if(!a||a->status.initialized)return I9100_BA_ARGUMENT;if(enabled!=1)return I9100_BA_DISABLED;
 if(!io||!io->verify||!io->send||!boot||!boot->ready||boot->fault||boot->closed||!boot->acl_mtu||boot->acl_mtu>4096U||
    !boot->acl_packets||boot->acl_packets>65535U||!boot->protocol_epoch||overlap(a,sizeof(*a),io,sizeof(*io))||overlap(a,sizeof(*a),boot,sizeof(*boot)))return I9100_BA_ARGUMENT;
 if(boot->le_buffers_valid>1U||((boot->features[4]&0x40U)&&!boot->le_buffers_valid)||
    (!boot->le_buffers_valid&&(boot->le_mtu||boot->le_packets)))return I9100_BA_ARGUMENT;
 if(boot->le_buffers_valid&&((!boot->le_mtu)!=(!boot->le_packets)||boot->le_mtu>4096U||boot->le_packets>255U))return I9100_BA_ARGUMENT;
 if(io->verify(io->cookie))return I9100_BA_OWNER;
 memset(a,0,sizeof(*a));a->io=*io;a->self=(uintptr_t)a;a->status.initialized=a->status.enabled=1;
 a->status.protocol_epoch=boot->protocol_epoch;
 a->status.mtu[0]=boot->acl_mtu;a->status.packets[0]=a->status.available[0]=boot->acl_packets;
 if(boot->le_packets){a->status.mtu[1]=boot->le_mtu;a->status.packets[1]=a->status.available[1]=boot->le_packets;}
 a->le_buffers_valid=boot->le_buffers_valid;return 0;
}
static int connect(struct i9100_bt_acl *a,uint16_t handle,unsigned type,unsigned address_type,const uint8_t *address)
{
 if(handle>0x0effU||find(a,handle)>=0)return fault(a,I9100_BA_FRAME);
 if(type==0x80U&&!a->le_buffers_valid)return fault(a,I9100_BA_FRAME);
 unsigned slot=0;while(slot<I9100_BT_ACL_LINKS&&a->links[slot].active)slot++;
 if(slot==I9100_BT_ACL_LINKS||a->status.generation==UINT32_MAX)return fault(a,I9100_BA_LIMIT);
 struct i9100_bt_acl_link link;memset(&link,0,sizeof(link));link.active=1;link.handle=handle;link.type=(uint8_t)type;
 link.address_type=(uint8_t)address_type;link.generation=++a->status.generation;
 link.pool=type==0x80U&&a->status.packets[1]?1U:0U;memcpy(link.address,address,6);a->links[slot]=link;a->status.links++;return 0;
}
static int completed(struct i9100_bt_acl *a,const uint8_t *p,unsigned bytes)
{
 if(bytes<3U||bytes>257U||p[2]>63U||bytes!=3U+p[2]*4U)return fault(a,I9100_BA_FRAME);
 uint32_t release[2]={0},slots[63],counts[63],ignored=0;unsigned entries=p[2];
 for(unsigned n=0;n<entries;n++){
  uint16_t handle=u16(p+3U+n*4U);uint32_t number=u16(p+5U+n*4U);
  if(handle>0x0effU)return fault(a,I9100_BA_FRAME);
  for(unsigned previous=0;previous<n;previous++)if(u16(p+3U+previous*4U)==handle)return fault(a,I9100_BA_FRAME);
  int slot=find(a,handle);slots[n]=slot<0?I9100_BT_ACL_LINKS:(uint32_t)slot;counts[n]=number;
  if(slot<0){ignored++;continue;}
  const struct i9100_bt_acl_link *link=&a->links[slot];
  if(number>link->outstanding)return fault(a,I9100_BA_FRAME);
  release[link->pool]+=number;
 }
 for(unsigned pool=0;pool<2;pool++)if(release[pool]>a->status.outstanding[pool]||
  release[pool]>a->status.packets[pool]-a->status.available[pool])return fault(a,I9100_BA_FRAME);
 for(unsigned n=0;n<entries;n++)if(slots[n]<I9100_BT_ACL_LINKS){
  struct i9100_bt_acl_link *link=&a->links[slots[n]];link->outstanding-=counts[n];
  a->status.outstanding[link->pool]-=counts[n];a->status.available[link->pool]+=counts[n];
  add(&a->status.completed,counts[n]);
 }
 for(unsigned n=0;n<ignored;n++)count(&a->status.ignored_completions);return 0;
}
int i9100_bt_acl_receive(struct i9100_bt_acl *a,const struct i9100_bt_frame *f)
{
 int e=live(a);if(e)return e;
 if(!f||!f->bytes||!f->length||overlap(a,sizeof(*a),f,sizeof(*f))||overlap(a,sizeof(*a),f->bytes,f->length))return I9100_BA_ARGUMENT;
 if(!consistency(a))return fault(a,I9100_BA_OWNER);
 if(f->type==2U){
  if(f->length<4U||f->length>4100U||u16(f->bytes+2)+4U!=f->length)return fault(a,I9100_BA_FRAME);
  uint16_t hf=u16(f->bytes);int slot=find(a,hf&0xfffU);
  if((hf>>14)||slot<0)return fault(a,I9100_BA_FRAME);
  /* Read Buffer Size reports controller HOST->CONTROLLER buffers. It is
   * not the host's receive MTU and must not truncate a valid incoming PDU. */
  count(&a->status.received);return 0;
 }
 if(f->type!=4U)return 0;
 const uint8_t *p=f->bytes;unsigned bytes=f->length;if(bytes<2U||bytes>257U||bytes!=2U+p[1])return fault(a,I9100_BA_FRAME);
 if(p[0]==0x03U){
  if(bytes!=13U)return fault(a,I9100_BA_FRAME);if(p[2]||p[11]!=1U)return 0;
  return connect(a,u16(p+3),1,0,p+5);
 }
 if(p[0]==0x3eU&&bytes>=3U&&(p[2]==1U||p[2]==10U)){
  if((p[2]==1U&&bytes!=21U)||(p[2]==10U&&bytes!=33U))return fault(a,I9100_BA_FRAME);
  if(p[3])return 0;if(p[6]>1U||p[7]>3U)return fault(a,I9100_BA_FRAME);
  return connect(a,u16(p+4),0x80U,p[7],p+8);
 }
 if(p[0]==0x13U)return completed(a,p,bytes);
 if(p[0]==0x05U){
  if(bytes!=6U)return fault(a,I9100_BA_FRAME);if(p[2])return 0;uint16_t handle=u16(p+3);if(handle>0x0effU)return fault(a,I9100_BA_FRAME);
  int slot=find(a,handle);if(slot<0)return 0;struct i9100_bt_acl_link *link=&a->links[slot];
  a->status.available[link->pool]+=link->outstanding;a->status.outstanding[link->pool]-=link->outstanding;
  add(&a->status.discarded,link->outstanding);link->active=0;link->outstanding=0;a->status.links--;return 0;
 }
 return 0;
}
int i9100_bt_acl_link(struct i9100_bt_acl *a,uint16_t handle,struct i9100_bt_acl_link *out)
{
 int e=live(a);if(e)return e;if(!out||overlap(a,sizeof(*a),out,sizeof(*out)))return I9100_BA_ARGUMENT;
 int slot=find(a,handle);if(slot<0)return I9100_BA_OWNER;*out=a->links[slot];return 0;
}
int i9100_bt_acl_send(struct i9100_bt_acl *a,uint16_t handle,uint32_t generation,unsigned pb,const uint8_t *payload,unsigned bytes)
{
 int e=live(a);if(e)return e;if(!payload||!bytes||bytes>4096U||pb>3U||!generation||overlap(a,sizeof(*a),payload,bytes))return I9100_BA_ARGUMENT;
 if(!consistency(a))return fault(a,I9100_BA_OWNER);int slot=find(a,handle);if(slot<0||a->links[slot].generation!=generation)return I9100_BA_OWNER;
 struct i9100_bt_acl_link *link=&a->links[slot];unsigned pool=link->pool;
 if(link->type==0x80U&&pb>1U)return I9100_BA_ARGUMENT; /* LE host packets have no auto-flush start flag. */
 if(bytes>a->status.mtu[pool])return I9100_BA_ARGUMENT;if(!a->status.available[pool])return I9100_BA_CREDIT;
 uint8_t packet[4101];packet[0]=2;uint16_t flags=(uint16_t)(handle|(pb<<12));
 packet[1]=(uint8_t)flags;packet[2]=(uint8_t)(flags>>8);packet[3]=(uint8_t)bytes;packet[4]=(uint8_t)(bytes>>8);memcpy(packet+5,payload,bytes);
 a->status.available[pool]--;a->status.outstanding[pool]++;link->outstanding++;count(&a->status.submitted);
 unsigned sent=0;if(a->io.send(a->io.cookie,packet,bytes+5U,&sent)||sent!=bytes+5U)return fault(a,I9100_BA_IO);return 0;
}
int i9100_bt_acl_stop(struct i9100_bt_acl *a)
{if(!valid(a))return I9100_BA_ARGUMENT;a->status.enabled=0;a->status.closed=1;return 0;}
int i9100_bt_acl_requalify(struct i9100_bt_acl *a,const struct i9100_bt_boot_status *boot)
{
 int e=live(a);if(e)return e;
 if(!boot||overlap(a,sizeof(*a),boot,sizeof(*boot)))return I9100_BA_ARGUMENT;
 if(!consistency(a))return fault(a,I9100_BA_OWNER);
 if(a->status.links||a->status.outstanding[0]||a->status.outstanding[1]||a->status.protocol_epoch==UINT32_MAX||
    boot->protocol_epoch!=a->status.protocol_epoch+1U||!boot->host_reset_confirmed||boot->host_reset_active||
    (boot->host_reset_valid_fields&47U)!=47U||boot->host_reset_complete_raw[0]!=0x0eU||
    boot->host_reset_complete_raw[1]!=4U||u16(boot->host_reset_complete_raw+3)!=0x0c03U||boot->host_reset_complete_raw[5]||
    ((boot->features[4]&0x40U)&&!(boot->host_reset_valid_fields&16U)))return I9100_BA_OWNER;
 struct i9100_bt_acl checked;memset(&checked,0,sizeof(checked));
 e=i9100_bt_acl_init(&checked,1,&a->io,boot);if(e)return e;
 memcpy(a->status.mtu,checked.status.mtu,sizeof(a->status.mtu));
 memcpy(a->status.packets,checked.status.packets,sizeof(a->status.packets));
 memcpy(a->status.available,checked.status.available,sizeof(a->status.available));
 a->le_buffers_valid=checked.le_buffers_valid;a->status.protocol_epoch=boot->protocol_epoch;
 return 0;
}
int i9100_bt_acl_status(const struct i9100_bt_acl *a,struct i9100_bt_acl_status *out)
{if(!valid(a)||!out||overlap(a,sizeof(*a),out,sizeof(*out)))return I9100_BA_ARGUMENT;*out=a->status;return 0;}
