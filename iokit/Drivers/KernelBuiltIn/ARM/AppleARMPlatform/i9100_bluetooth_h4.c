/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bluetooth_h4.h"
#include <string.h>
static uint16_t le16(const uint8_t *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static int valid(const struct i9100_bt_h4 *b){return b&&b->initialized&&b->self==(uintptr_t)b;}
static int overlaps(const void *a,unsigned an,const void *c,unsigned cn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)c;return x>UINTPTR_MAX-an||y>UINTPTR_MAX-cn||(x+an>y&&y+cn>x);}
int i9100_bt_init(struct i9100_bt_h4 *b,const struct i9100_bt_io *io)
{
 if(!b||b->initialized||!io||!io->lock||!io->unlock||!io->write||!io->quiesce||!io->now_ns)return I9100_BT_ARGUMENT;
 memset(b,0,sizeof(*b));b->io=*io;b->self=(uintptr_t)b;b->initialized=1;return 0;
}
int i9100_bt_enable(struct i9100_bt_h4 *b,int enabled,uint32_t generation)
{
 if(!valid(b)||(enabled!=0&&enabled!=1))return I9100_BT_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_BT_BUSY;
 int e=0;
 if(enabled){
  if(b->enabled||!generation||generation<=b->link_generation){e=I9100_BT_BUSY;goto done;}
  b->link_generation=generation;b->enabled=1;b->fault=b->pending=b->used=b->needed=b->header=0;
  b->credits=1;b->last_status=b->last_completion=0;
 }else if(!b->enabled)e=I9100_BT_DISABLED;
 else if(b->io.quiesce(b->io.cookie)){b->fault=1;e=I9100_BT_IO;}
 else{b->enabled=0;if(b->pending||b->used)b->fault=1;}
done:
 b->io.unlock(b->io.cookie);return e;
}
int i9100_bt_command(struct i9100_bt_h4 *b,uint16_t opcode,const uint8_t *payload,unsigned length)
{
 if(!valid(b)||!opcode||length>255U||(length&&(!payload||overlaps(b,sizeof(*b),payload,length))))return I9100_BT_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_BT_BUSY;
 int e=0;
 if(!b->enabled){e=I9100_BT_DISABLED;goto done;}
 if(b->fault){e=I9100_BT_IO;goto done;}
 if(b->pending||!b->credits){e=I9100_BT_BUSY;goto done;}
 uint8_t packet[259]={1,(uint8_t)opcode,(uint8_t)(opcode>>8),(uint8_t)length};
 if(length)memcpy(packet+4,payload,length);
 unsigned sent=0;
 b->credits--;b->pending=1;b->opcode=opcode;b->command_started=b->io.now_ns(b->io.cookie);
 b->last_completion=b->last_status=0;
 if(b->io.write(b->io.cookie,packet,length+4U,&sent)||sent!=length+4U){b->fault=1;e=I9100_BT_IO;}
done:
 b->io.unlock(b->io.cookie);return e;
}
static int event(struct i9100_bt_h4 *b)
{
 if(b->frame[0]!=4U)return 0;
 uint8_t code=b->frame[1],length=b->frame[2];
 if(code!=0x0eU&&code!=0x0fU)return 0;
 if((code==0x0eU&&length<3U)||(code==0x0fU&&length!=4U))return I9100_BT_FRAME;
 unsigned base=code==0x0eU?4U:5U;uint16_t opcode=le16(b->frame+base);
 uint8_t credits=b->frame[code==0x0eU?3U:4U];
 if(code==0x0eU&&!opcode){if(length!=3U)return I9100_BT_FRAME;b->credits=credits;return 0;}
 if(code==0x0eU&&length<4U)return I9100_BT_FRAME;
 uint8_t status=b->frame[code==0x0eU?6U:3U];
 if(!b->pending||opcode!=b->opcode)return I9100_BT_UNMATCHED;
 uint64_t now=b->io.now_ns(b->io.cookie);
 if(now<b->command_started||now-b->command_started>1000000000ULL){b->fault=1;return I9100_BT_TIMEOUT;}
 b->credits=credits;
 b->pending=0;b->last_opcode=opcode;b->last_status=status;b->last_completion=code;
 return status?I9100_BT_CONTROLLER:0;
}
int i9100_bt_acl_packet(struct i9100_bt_h4 *b,const uint8_t *packet,unsigned bytes)
{
 if(!valid(b)||!packet||bytes<6U||bytes>4101U||overlaps(b,sizeof(*b),packet,bytes)||
    packet[0]!=2U||le16(packet+3)+5U!=bytes||(le16(packet+1)>>14)||
    (le16(packet+1)&0xfffU)>0x0effU)return I9100_BT_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_BT_BUSY;
 int e=0;
 if(!b->enabled)e=I9100_BT_DISABLED;
 else if(b->fault)e=I9100_BT_IO;
 else {unsigned sent=0;
  if(b->io.write(b->io.cookie,packet,bytes,&sent)||sent!=bytes){b->fault=1;e=I9100_BT_IO;}}
 b->io.unlock(b->io.cookie);return e;
}
int i9100_bt_feed(struct i9100_bt_h4 *b,const uint8_t *input,unsigned length,unsigned *consumed,struct i9100_bt_frame *out)
{
 if(!valid(b)||!input||!length||length>65536U||!consumed||!out||
    overlaps(b,sizeof(*b),input,length)||overlaps(b,sizeof(*b),consumed,sizeof(*consumed))||
    overlaps(b,sizeof(*b),out,sizeof(*out))||overlaps(input,length,consumed,sizeof(*consumed))||
    overlaps(input,length,out,sizeof(*out))||overlaps(consumed,sizeof(*consumed),out,sizeof(*out)))return I9100_BT_ARGUMENT;
 *consumed=0;memset(out,0,sizeof(*out));
 if(b->io.lock(b->io.cookie))return I9100_BT_BUSY;
 int e=0;
 if(!b->enabled){e=I9100_BT_DISABLED;goto done;}if(b->fault){e=I9100_BT_IO;goto done;}
 /* A complete prior view is consumed by the caller before this next feed. */
 if(b->needed&&b->used==b->needed)b->used=b->needed=b->header=0;
 while(*consumed<length){
  if(b->used>=sizeof(b->frame)){e=I9100_BT_FRAME;goto fail;}
  uint8_t value=input[(*consumed)++];b->frame[b->used++]=value;
  if(b->used==1U){
   if(value==4U)b->header=3;else if(value==2U)b->header=5;else if(value==3U)b->header=4;
   else{e=I9100_BT_FRAME;goto fail;}
  }
  if(b->used==b->header){
   unsigned payload=b->frame[0]==2U?le16(b->frame+3):b->frame[b->header-1U];
   if(payload>4096U||payload>sizeof(b->frame)-b->header){e=I9100_BT_FRAME;goto fail;}
   b->needed=b->header+payload;
  }
  if(b->needed&&b->used==b->needed){
   e=event(b);if(e==I9100_BT_FRAME||e==I9100_BT_TIMEOUT)goto fail;
   out->type=b->frame[0];out->bytes=b->frame+1;out->length=b->used-1U;if(b->packets!=UINT32_MAX)b->packets++;
   if(!e)e=1;goto done;
  }
 }
 goto done;
fail:
 b->fault=1;
done:
 b->io.unlock(b->io.cookie);return e;
}
int i9100_bt_poll(struct i9100_bt_h4 *b)
{
 if(!valid(b))return I9100_BT_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_BT_BUSY;
 int e=0;
 if(!b->enabled)e=I9100_BT_DISABLED;else if(b->fault)e=I9100_BT_IO;
 else if(b->pending){uint64_t now=b->io.now_ns(b->io.cookie);
  if(now<b->command_started||now-b->command_started>1000000000ULL){e=I9100_BT_TIMEOUT;b->fault=1;}else e=I9100_BT_BUSY;}
 else if(b->last_status)e=I9100_BT_CONTROLLER;
 b->io.unlock(b->io.cookie);return e;
}
int i9100_bt_hcd_validate(const uint8_t *file,uint32_t bytes,uint32_t base,uint32_t span,uint32_t *records)
{
 if(records)*records=0;
 if(!file||!records||!bytes||bytes>1024U*1024U||!span||span>16U*1024U*1024U||(uint64_t)base+span>0x100000000ULL)return I9100_BT_ARGUMENT;
 uint32_t cursor=0,count=0;int launched=0;
 while(cursor<bytes){
  if(bytes-cursor<3U)return I9100_BT_FRAME;
  uint16_t opcode=le16(file+cursor);unsigned length=file[cursor+2U];cursor+=3;
  if(length>bytes-cursor||launched)return I9100_BT_FRAME;
  if(opcode==0xfc2eU){if(length||count)return I9100_BT_FRAME;}
  else if(opcode==0xfc4cU||opcode==0xfc4eU){
   if(length<4U||(opcode==0xfc4eU&&length!=4U))return I9100_BT_FRAME;
   uint32_t address=le32(file+cursor);unsigned amount=opcode==0xfc4cU?length-4U:1U;
   if(opcode==0xfc4eU)address&=~1U;
   if(!amount||address<base||(uint64_t)address+amount>(uint64_t)base+span)return I9100_BT_FRAME;
   if(opcode==0xfc4eU)launched=1;
  }else return I9100_BT_FRAME;
  cursor+=length;if(++count>65536U)return I9100_BT_FRAME;
 }
 if(!launched)return I9100_BT_FRAME;*records=count;return 0;
}
