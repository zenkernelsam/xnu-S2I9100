/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_modem_sipc4.h"
#include <string.h>
static uint16_t le16(const uint8_t *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t *b,uint32_t value){for(unsigned n=0;n<4;n++)b[n]=(uint8_t)(value>>(n*8));}
int i9100_sipc4_init(struct i9100_sipc4 *p,enum i9100_sipc_format format,unsigned version,int aligned)
{
 if(!p||p->initialized||format<1||format>3||(version!=40U&&version!=41U&&version!=42U)||(aligned!=0&&aligned!=1))return I9100_SIPC_ARGUMENT;
 memset(p,0,sizeof(*p));p->initialized=1;p->format=format;p->version=version;p->aligned=aligned;p->header=format==1?3U:6U;return 0;
}
int i9100_sipc4_enable(struct i9100_sipc4 *p,int enabled)
{
 if(!p||!p->initialized||(enabled!=0&&enabled!=1))return I9100_SIPC_ARGUMENT;
 if(enabled&&(p->fault||p->used||p->fragment_active))return I9100_SIPC_FRAME;
 p->enabled=enabled;return 0;
}
static int finish(struct i9100_sipc4 *p,struct i9100_sipc_frame *out)
{
 unsigned length=p->format==1U?le16(p->frame+1):le32(p->frame+1);
 if(p->version==42U&&p->format==1U)length&=0x3fffU;
 if(p->frame[length+1U]!=0x7eU)return I9100_SIPC_FRAME;
 struct i9100_sipc_frame parsed;memset(&parsed,0,sizeof(parsed));parsed.format=(uint8_t)p->format;
 parsed.bytes=p->frame+1U+p->header;parsed.length=length-p->header;
 if(p->format==I9100_SIPC_RAW){parsed.channel=p->frame[5];parsed.control=p->frame[6];if(parsed.channel>31U)return I9100_SIPC_FRAME;}
 else if(p->format==I9100_SIPC_RFS){parsed.command=p->frame[5];parsed.channel=p->frame[6];}
 else{
  parsed.control=p->frame[3];unsigned id=parsed.control&127U,more=parsed.control&128U;
  if(more||(p->fragment_active&&id==p->fragment_id)){
   if(p->fragment_active&&id!=p->fragment_id)return I9100_SIPC_FRAGMENT;
   if(!p->fragment_active){
    if(parsed.length<7U)return I9100_SIPC_FRAGMENT;
    unsigned target=le16(parsed.bytes);
    if(target<parsed.length||target>sizeof(p->fragments))return I9100_SIPC_OVERFLOW;
    p->fragment_target=target;p->fragment_id=(uint8_t)id;p->fragment_active=1;p->fragment_bytes=0;
   }
   if(parsed.length>p->fragment_target-p->fragment_bytes)return I9100_SIPC_OVERFLOW;
   memcpy(p->fragments+p->fragment_bytes,parsed.bytes,parsed.length);p->fragment_bytes+=parsed.length;
   if(more){if(p->fragment_bytes==p->fragment_target)return I9100_SIPC_FRAGMENT;return 0;}
   if(p->fragment_bytes!=p->fragment_target)return I9100_SIPC_FRAGMENT;
   parsed.bytes=p->fragments;parsed.length=p->fragment_bytes;p->fragment_active=0;p->fragment_bytes=p->fragment_target=0;
  }
 }
 *out=parsed;return 1;
}
int i9100_sipc4_feed(struct i9100_sipc4 *p,const uint8_t *input,unsigned length,unsigned *consumed,struct i9100_sipc_frame *out)
{
 if(consumed)*consumed=0;
 if(!p||!p->initialized||!input||!length||length>65536U||!consumed||!out)return I9100_SIPC_ARGUMENT;
 if(!p->enabled)return I9100_SIPC_DISABLED;if(p->fault)return I9100_SIPC_FRAME;
 if(p->needed&&p->used==p->needed)p->used=p->needed=0;
 while(*consumed<length){
  if(p->used>=sizeof(p->frame)){p->fault=1;return I9100_SIPC_OVERFLOW;}
  uint8_t byte=input[(*consumed)++];p->frame[p->used++]=byte;
  if(p->used==1U&&byte!=0x7fU){p->fault=1;return I9100_SIPC_FRAME;}
  if(p->used==p->header+1U){
   uint32_t frame_length=p->format==1U?le16(p->frame+1):le32(p->frame+1);
   if(p->version==42U&&p->format==1U)frame_length&=0x3fffU;
   if(frame_length<p->header||frame_length>4096U){p->fault=1;return I9100_SIPC_OVERFLOW;}
   p->needed=frame_length+2U;if(p->aligned)p->needed=(p->needed+3U)&~3U;
  }
  if(p->needed&&p->used==p->needed){
   int e=finish(p,out);if(e<0)p->fault=1;return e;
  }
 }
 return 0;
}
int i9100_sipc4_encode(enum i9100_sipc_format format,unsigned channel,unsigned control,const uint8_t *payload,unsigned length,int aligned,uint8_t *out,unsigned capacity,unsigned *used)
{
 if(used)*used=0;
 if(!used||!out||!payload||!length||format<1||format>3||channel>255U||control>255U||
    (format==I9100_SIPC_RAW&&channel>31U)||(aligned!=0&&aligned!=1))return I9100_SIPC_ARGUMENT;
 unsigned header=format==1?3U:6U;
 if(length>4096U-header)return I9100_SIPC_OVERFLOW;
 unsigned total=length+header+2U;if(aligned)total=(total+3U)&~3U;
 if(total>capacity)return I9100_SIPC_OVERFLOW;
 memset(out,0,total);out[0]=0x7f;
 if(format==1){out[1]=(uint8_t)(length+3U);out[2]=(uint8_t)((length+3U)>>8);out[3]=(uint8_t)control;}
 else{put32(out+1,length+6U);out[5]=(uint8_t)(format==I9100_SIPC_RFS?control:channel);out[6]=(uint8_t)(format==I9100_SIPC_RFS?channel:control);}
 memcpy(out+1U+header,payload,length);out[length+header+1U]=0x7e;*used=total;return 0;
}
int i9100_xmm6260_profile(const uint8_t *dev,unsigned dev_bytes,const uint8_t *config,unsigned bytes,struct i9100_xmm_profile *out)
{
 if(!dev||!config||!out||dev_bytes!=18U||dev[0]!=18U||dev[1]!=1U||bytes<9U||bytes>4096U||config[0]!=9U||config[1]!=2U||le16(config+2)!=bytes||!config[5])return I9100_SIPC_ARGUMENT;
 if(le16(dev+8)!=0x1519U||le16(dev+10)!=0x0020U)return I9100_SIPC_IDENTITY;
 struct interface {uint8_t seen,cls,sub,protocol,slave,union_seen,in,out,declared,actual;uint16_t in_mps,out_mps;} interfaces[16];
 memset(interfaces,0,sizeof(interfaces));unsigned current=16,distinct=0;uint32_t endpoints=0;
 for(unsigned pos=9U;pos<bytes;){
  unsigned size=config[pos];if(size<2U||size>bytes-pos)return I9100_SIPC_FRAME;
  unsigned type=config[pos+1U];const uint8_t *d=config+pos;
  if(type==4U){
   if(size!=9U||d[2]>=16U||d[3])return I9100_SIPC_FRAME;current=d[2];
   if(interfaces[current].seen)return I9100_SIPC_FRAME;
   interfaces[current].seen=1;interfaces[current].declared=d[4];interfaces[current].cls=d[5];interfaces[current].sub=d[6];interfaces[current].protocol=d[7];distinct++;
  }else if(type==0x24U){
   if(current>=16U||size<3U)return I9100_SIPC_FRAME;
   if(d[2]==6U){if(size!=5U||d[3]!=current||d[4]>=16U||interfaces[current].union_seen)return I9100_SIPC_FRAME;
    interfaces[current].union_seen=1;interfaces[current].slave=d[4];}
  }else if(type==5U){
   if(current>=16U||size!=7U||!(d[2]&15U)||(d[2]&0x70U))return I9100_SIPC_FRAME;
   unsigned index=(d[2]&15U)+((d[2]&128U)?16U:0U);if(endpoints&(1U<<index))return I9100_SIPC_FRAME;endpoints|=1U<<index;
   interfaces[current].actual++;
   if((d[3]&3U)==2U){uint16_t mps=le16(d+4);if(mps!=64U&&mps!=512U)return I9100_SIPC_FRAME;
    if(d[2]&128U){if(interfaces[current].in)return I9100_SIPC_FRAME;interfaces[current].in=d[2];interfaces[current].in_mps=mps;}
    else{if(interfaces[current].out)return I9100_SIPC_FRAME;interfaces[current].out=d[2];interfaces[current].out_mps=mps;}}
  }
  pos+=size;
 }
 if(distinct!=config[4])return I9100_SIPC_FRAME;
 for(unsigned n=0;n<16U;n++)if(interfaces[n].seen&&interfaces[n].actual!=interfaces[n].declared)return I9100_SIPC_FRAME;
 struct i9100_xmm_profile p;memset(&p,0,sizeof(p));p.vid=0x1519;p.pid=0x0020;p.configuration=config[5];
 for(unsigned role=0;role<3;role++){
  unsigned ctrl=role*2U,slave=ctrl+1U;struct interface *a=&interfaces[ctrl],*b=&interfaces[slave];
  if(!a->seen||a->cls!=2U||a->sub!=2U||a->protocol!=1U||!a->union_seen||a->slave!=slave||a->in||a->out||
     !b->seen||b->cls!=0x0aU||b->declared!=2U||!b->in||!b->out||b->in_mps!=b->out_mps)return I9100_SIPC_FRAME;
  p.pipes[role].control_interface=(uint8_t)ctrl;p.pipes[role].data_interface=(uint8_t)slave;
  p.pipes[role].in_endpoint=b->in;p.pipes[role].out_endpoint=b->out;p.pipes[role].max_packet=b->in_mps;
 }
 *out=p;return 0;
}
