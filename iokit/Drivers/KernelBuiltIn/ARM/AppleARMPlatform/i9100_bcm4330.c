/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm4330.h"
#include "i9100_bcm_backplane_private.h"
#include <string.h>
static uint16_t le16(const uint8_t *b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t *b,uint32_t v){for(unsigned n=0;n<4;n++)b[n]=(uint8_t)(v>>(n*8U));}
static void secret_clear(void *buffer,unsigned n){volatile uint8_t *p=(volatile uint8_t *)buffer;while(n--)*p++=0;}
static const uint32_t bp_ports[3]={0x1000a,0x1000b,0x1000c};
static int bp_window_write(struct i9100_bcm4330 *b,unsigned n,uint8_t value)
{
 uint8_t actual=0;
 if(b->io.cmd52(b->io.cookie,1,1,bp_ports[n],&value)||
    b->io.cmd52(b->io.cookie,0,1,bp_ports[n],&actual)||actual!=value)return I9100_BCM_IO;
 return 0;
}
int i9100_bcm_bp_restore_locked(struct i9100_bcm4330 *b)
{
 if(!b->window_restore_pending)return 0;
 int e=0;
 for(unsigned n=0;n<b->window_touched;n++)if(bp_window_write(b,n,b->window_saved[n]))e=I9100_BCM_IO;
 if(!e)b->window_restore_pending=b->window_touched=0;
 else b->fault=1;
 return e;
}
static int bp_transfer(struct i9100_bcm4330 *b,int write,uint32_t address,uint8_t *data,uint32_t bytes)
{
 if(b->window_restore_pending)return I9100_BCM_IO;
 uint8_t next[3]={(uint8_t)((address>>8)&128U),(uint8_t)(address>>16),(uint8_t)(address>>24)};
 for(unsigned n=0;n<3;n++)if(b->io.cmd52(b->io.cookie,0,1,bp_ports[n],&b->window_saved[n]))return I9100_BCM_IO;
 b->window_restore_pending=1;
 int e=0;
 for(unsigned n=0;n<3;n++){
  b->window_touched=(uint8_t)(n+1U);
  if(bp_window_write(b,n,next[n])){e=I9100_BCM_IO;break;}
 }
 if(!e&&b->io.cmd53(b->io.cookie,write,1,(address&0x7fffU)|0x8000U,1,data,bytes))e=I9100_BCM_IO;
 if(i9100_bcm_bp_restore_locked(b))e=I9100_BCM_IO;
 return e;
}
static int bp_word(struct i9100_bcm4330 *b,int write,uint32_t address,uint32_t *value)
{
 if(!b||!b->initialized||!value||(address&3U)||address<0x18000000U||address>=0x18200000U)
  return I9100_BCM_ARGUMENT;
 uint8_t data[4]={0};if(write)put32(data,*value);
 int e=bp_transfer(b,write,address,data,4);if(!e&&!write)*value=le32(data);return e;
}
int i9100_bcm_bp_memory_locked(struct i9100_bcm4330 *b,int write,uint32_t address,uint8_t *data,uint32_t bytes)
{
 if(!b||!b->initialized||!data||(write!=0&&write!=1)||!bytes||bytes>512U||
    (bytes&3U)||(address&3U)||(uint64_t)address+bytes>4U*1024U*1024U||
    (address&0x7fffU)+bytes>0x8000U)return I9100_BCM_ARGUMENT;
 return bp_transfer(b,write,address,data,bytes);
}
int i9100_bcm_bp_read_locked(struct i9100_bcm4330 *b,uint32_t address,uint32_t *value)
{return bp_word(b,0,address,value);}
int i9100_bcm_bp_write_locked(struct i9100_bcm4330 *b,uint32_t address,uint32_t value)
{return bp_word(b,1,address,&value);}
static int bp_read(struct i9100_bcm4330 *b,uint32_t address,uint32_t *value)
{return i9100_bcm_bp_read_locked(b,address,value);}
int i9100_bcm_init(struct i9100_bcm4330 *b,const struct i9100_bcm_io *io)
{
 if(!b||b->initialized||!io||!io->claim||!io->release||!io->cmd52||!io->cmd53||!io->send_frame||!io->now_ns||!io->delay_us)
  return I9100_BCM_ARGUMENT;
 memset(b,0,sizeof(*b));b->io=*io;b->initialized=1;return 0;
}
int i9100_bcm_enable(struct i9100_bcm4330 *b,int enabled,uint16_t vendor,uint16_t device)
{
 if(!b||!b->initialized||(enabled!=0&&enabled!=1))return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=0;
 if(!enabled){
  if(!b->enabled){e=I9100_BCM_DISABLED;goto done;}
  if(b->chip_holds||b->boot_owner||b->control_owner||b->rx_owner){e=I9100_BCM_BUSY;goto done;}
  if(i9100_bcm_bp_restore_locked(b)){e=I9100_BCM_IO;goto done;}
  uint8_t saved=b->saved_clock;
  if(b->io.cmd52(b->io.cookie,1,1,0x1000e,&saved)){e=I9100_BCM_IO;b->fault=1;}
  else b->enabled=b->protocol_ready=b->fault=b->pending=0;
  goto done;
 }
 if(b->enabled||b->fault||b->window_restore_pending){e=I9100_BCM_BUSY;goto done;}
 if(vendor!=0x02d0U||(device!=0x4330U&&device!=0x4329U)){e=I9100_BCM_ID;goto done;}
 if(b->io.cmd52(b->io.cookie,0,1,0x1000e,&b->saved_clock)){e=I9100_BCM_IO;goto done;}
 b->enabled=1; /* retain cleanup ownership if ALP request/restore fails */
 uint8_t request=(uint8_t)((b->saved_clock&0x3fU)|8U);
 if(b->io.cmd52(b->io.cookie,1,1,0x1000e,&request)){e=I9100_BCM_IO;goto restore;}
 uint64_t started=b->io.now_ns(b->io.cookie);unsigned ready=0;
 for(unsigned n=0;n<1000U;n++){
  uint8_t clock=0;if(b->io.cmd52(b->io.cookie,0,1,0x1000e,&clock)){e=I9100_BCM_IO;break;}
  if(clock&64U){ready=1;break;}
  uint64_t now=b->io.now_ns(b->io.cookie);
  if(now<started||now-started>100000000ULL){e=I9100_BCM_TIMEOUT;break;}
  if(b->io.delay_us(b->io.cookie,100)){e=I9100_BCM_IO;break;}
 }
 if(!e&&!ready)e=I9100_BCM_TIMEOUT;
 if(!e)e=bp_read(b,0x18000000U,&b->chip_id);
 if(!e&&(b->chip_id&0xffffU)!=0x4330U)e=I9100_BCM_ID;
 if(!e){b->enabled=1;goto done;}
restore:
 request=b->saved_clock;
 if(i9100_bcm_bp_restore_locked(b)||b->io.cmd52(b->io.cookie,1,1,0x1000e,&request))b->fault=1;
 else b->enabled=0;
done:
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_backplane_read(struct i9100_bcm4330 *b,uint32_t address,uint32_t *value)
{
 if(!b||!b->initialized||!value||address<0x18000000U||address>=0x18100000U||(address&3U))return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=!b->enabled?I9100_BCM_DISABLED:b->fault?I9100_BCM_IO:bp_read(b,address,value);
 if(e==I9100_BCM_IO)b->fault=1;b->io.release(b->io.cookie);return e;
}
int i9100_bcm_protocol_start(struct i9100_bcm4330 *b,unsigned version,unsigned credit)
{
 if(!b||!b->initialized||version!=4U||credit>127U)return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=0;if(!b->enabled)e=I9100_BCM_DISABLED;else if(b->fault||b->protocol_ready||b->chip_holds||b->boot_owner||b->control_owner||b->rx_owner||b->protocol_generation==UINT32_MAX)e=I9100_BCM_BUSY;
 else {b->protocol_generation++;b->protocol_ready=1;b->data_paused=0;b->tx_sequence=b->rx_sequence=b->rx_seen=b->flow_mask=0;b->tx_max=(uint8_t)credit;}
 b->io.release(b->io.cookie);return e;
}
static int send(struct i9100_bcm4330 *b,unsigned channel,const uint8_t *payload,unsigned bytes,unsigned priority)
{
 if(!b->enabled||!b->protocol_ready)return I9100_BCM_DISABLED;
 if(b->fault)return I9100_BCM_IO;
 unsigned credit=(uint8_t)(b->tx_max-b->tx_sequence);
 if((credit&128U)||credit<=(channel==2U?1U:0U)||(channel==2U&&(b->data_paused||(b->flow_mask&(1U<<priority)))))return I9100_BCM_CREDIT;
 uint8_t frame[2048];unsigned header=channel==2U?16U:12U,length=header+bytes,padded=(length+3U)&~3U;
 if(padded>sizeof(frame))return I9100_BCM_ARGUMENT;
 memset(frame,0,padded);frame[0]=(uint8_t)length;frame[1]=(uint8_t)(length>>8);
 uint16_t complement=(uint16_t)~length;frame[2]=(uint8_t)complement;frame[3]=(uint8_t)(complement>>8);
 frame[4]=b->tx_sequence;frame[5]=(uint8_t)channel;frame[7]=12U;
 if(channel==2U){frame[12]=0x20;frame[13]=(uint8_t)priority;}
 memcpy(frame+header,payload,bytes);
 int failed=b->io.send_frame(b->io.cookie,frame,padded);secret_clear(frame,padded);
 if(failed){b->fault=1;return I9100_BCM_IO;}
 b->tx_sequence++;return 0;
}
static int send_data_for(struct i9100_bcm4330 *b,void *owner,const uint8_t *ethernet,unsigned bytes,unsigned priority)
{
 if(!b||!b->initialized||!ethernet||bytes<14U||bytes>1518U||priority>7U)return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=b->control_owner!=owner?I9100_BCM_BUSY:send(b,2,ethernet,bytes,priority);b->io.release(b->io.cookie);return e;
}
int i9100_bcm_send_data(struct i9100_bcm4330 *b,const uint8_t *ethernet,unsigned bytes,unsigned priority)
{return send_data_for(b,0,ethernet,bytes,priority);}
int i9100_bcm_send_data_owned(struct i9100_bcm4330 *b,void *owner,const uint8_t *ethernet,unsigned bytes,unsigned priority)
{return owner?send_data_for(b,owner,ethernet,bytes,priority):I9100_BCM_ARGUMENT;}
static int ioctl_for(struct i9100_bcm4330 *b,void *owner,uint32_t command,int set,const uint8_t *input,unsigned bytes,uint16_t *submitted)
{
 if(!b||!b->initialized||(set!=0&&set!=1)||bytes>1536U||(bytes&&!input))return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=0;if(b->pending||b->control_owner!=owner){e=I9100_BCM_BUSY;goto done;}
 if(b->next_request==UINT16_MAX){e=I9100_BCM_BUSY;goto done;}
 uint8_t payload[1552];uint16_t id=(uint16_t)(b->next_request+1U);
 memset(payload,0,16);put32(payload,command);put32(payload+4,bytes);put32(payload+8,((uint32_t)id<<16)|(set?2U:0U));
 if(bytes)memcpy(payload+16,input,bytes);e=send(b,0,payload,bytes+16U,0);secret_clear(payload,bytes+16U);
 if(!e){b->next_request=b->request_id=id;b->request_command=command;b->request_length=bytes;
  b->request_started=b->io.now_ns(b->io.cookie);b->pending=1;b->firmware_status=0;if(submitted)*submitted=id;}
done:
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_ioctl(struct i9100_bcm4330 *b,uint32_t command,int set,const uint8_t *input,unsigned bytes)
{return ioctl_for(b,0,command,set,input,bytes,0);}
int i9100_bcm_ioctl_owned(struct i9100_bcm4330 *b,void *owner,uint32_t command,int set,const uint8_t *input,unsigned bytes,uint16_t *submitted)
{return owner&&submitted?ioctl_for(b,owner,command,set,input,bytes,submitted):I9100_BCM_ARGUMENT;}
int i9100_bcm_control_bind(struct i9100_bcm4330 *b,void *owner,uint32_t generation)
{
 if(!b||!b->initialized||!owner||!generation)return I9100_BCM_ARGUMENT;if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=!b->enabled||!b->protocol_ready?I9100_BCM_DISABLED:
  generation!=b->protocol_generation||generation<=b->control_generation?I9100_BCM_ARGUMENT:
  b->control_owner||b->pending||b->fault?I9100_BCM_BUSY:0;
 if(!e){b->control_owner=owner;b->control_generation=generation;}b->io.release(b->io.cookie);return e;
}
int i9100_bcm_control_unbind(struct i9100_bcm4330 *b,void *owner,int cancel)
{
 if(!b||!b->initialized||!owner||(cancel!=0&&cancel!=1))return I9100_BCM_ARGUMENT;if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=b->control_owner!=owner||b->rx_owner?I9100_BCM_BUSY:b->pending&&!cancel?I9100_BCM_BUSY:0;
 if(!e){if(cancel){b->pending=0;b->fault=1;}b->control_owner=0;}
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_control_check(struct i9100_bcm4330 *b,void *owner,uint32_t generation)
{
 if(!b||!b->initialized||!owner||!generation)return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=b->control_owner!=owner||b->protocol_generation!=generation?I9100_BCM_ARGUMENT:
  !b->enabled||!b->protocol_ready?I9100_BCM_DISABLED:b->fault?I9100_BCM_IO:0;
 b->io.release(b->io.cookie);return e;
}
static int receive_for(struct i9100_bcm4330 *b,void *owner,const uint8_t *frame,unsigned bytes,struct i9100_bcm_frame *out)
{
 if(!b||!b->initialized||!frame||!out||bytes<12U||bytes>2048U)return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=0;
 if(b->rx_owner!=owner){e=I9100_BCM_BUSY;goto done;}
 if(!b->enabled||!b->protocol_ready){e=I9100_BCM_DISABLED;goto done;}if(b->fault){e=I9100_BCM_IO;goto done;}
 uint16_t length=le16(frame),inverse=le16(frame+2);unsigned channel=frame[5]&15U,offset=frame[7];
 if((uint16_t)(length^inverse)!=0xffffU||length<12U||length>bytes||offset<12U||offset>length||
    (frame[5]&0xf0U)||(frame[10]&&frame[10]!=4U)||frame[11]){e=I9100_BCM_FRAME;goto fail;}
 if(channel>2U){e=I9100_BCM_UNSUPPORTED;goto fail;}
 if(b->rx_seen&&frame[4]!=b->rx_sequence){e=I9100_BCM_FRAME;goto fail;}
 unsigned credit=(uint8_t)(frame[9]-b->tx_sequence);if(credit&128U){e=I9100_BCM_CREDIT;goto fail;}
 struct i9100_bcm_frame parsed;memset(&parsed,0,sizeof(parsed));parsed.channel=(uint8_t)channel;parsed.generation=b->protocol_generation;
 parsed.payload=frame+offset;parsed.bytes=length-offset;
 /* Vendor deliver path calls dhd_prot_hdrpull for both EVENT and DATA. */
 if(channel==1U||channel==2U){
  if(parsed.bytes<18U||(parsed.payload[0]>>4)!=2U||(parsed.payload[1]&~7U)||parsed.payload[2]){e=I9100_BCM_FRAME;goto fail;}
  unsigned extra=(unsigned)parsed.payload[3]*4U;
  if(extra>parsed.bytes-18U){e=I9100_BCM_FRAME;goto fail;}
  parsed.priority=parsed.payload[1]&7U;parsed.payload+=4U+extra;parsed.bytes-=4U+extra;
 }else if(channel==0U){
  if(parsed.bytes<16U){e=I9100_BCM_FRAME;goto fail;}
  uint64_t now=b->io.now_ns(b->io.cookie);
  if(b->pending&&(now<b->request_started||now-b->request_started>500000000ULL)){e=I9100_BCM_TIMEOUT;goto fail;}
  uint32_t flags=le32(parsed.payload+8),command=le32(parsed.payload),size=le32(parsed.payload+4);
  if(!b->pending||(flags>>16)!=b->request_id||command!=b->request_command||
     (flags&0xfffcU)){e=I9100_BCM_FRAME;goto fail;}
  uint32_t status=le32(parsed.payload+12);
  if(!(flags&1U)&&!status&&(size>parsed.bytes-16U||size>b->request_length)){e=I9100_BCM_FRAME;goto fail;}
  b->firmware_status=status?(int32_t)status:(flags&1U)?I9100_BCM_FIRMWARE:0;
  if(b->firmware_status)e=I9100_BCM_FIRMWARE;
  parsed.request_id=b->request_id;parsed.command=command;parsed.status=status?(int32_t)status:(flags&1U)?I9100_BCM_FIRMWARE:0;
  parsed.needed_bytes=size;parsed.payload+=16;parsed.bytes=parsed.status?0U:size;
  if(parsed.status)parsed.payload=0;b->pending=0;
 }
 b->tx_max=frame[9];b->flow_mask=frame[8];b->rx_seen=1;b->rx_sequence=(uint8_t)(frame[4]+1U);*out=parsed;goto done;
fail:
 b->fault=1;
done:
 b->io.release(b->io.cookie);return e;
}
int i9100_bcm_receive(struct i9100_bcm4330 *b,const uint8_t *frame,unsigned bytes,struct i9100_bcm_frame *out)
{return receive_for(b,0,frame,bytes,out);}
int i9100_bcm_receive_owned(struct i9100_bcm4330 *b,void *owner,const uint8_t *frame,unsigned bytes,struct i9100_bcm_frame *out)
{return owner?receive_for(b,owner,frame,bytes,out):I9100_BCM_ARGUMENT;}
int i9100_bcm_poll_request(struct i9100_bcm4330 *b)
{
 if(!b||!b->initialized)return I9100_BCM_ARGUMENT;
 if(b->io.claim(b->io.cookie))return I9100_BCM_BUSY;
 int e=0;if(!b->enabled||!b->protocol_ready)e=I9100_BCM_DISABLED;
 else if(b->fault)e=I9100_BCM_IO;
 else if(b->pending){uint64_t now=b->io.now_ns(b->io.cookie);
  if(now<b->request_started||now-b->request_started>500000000ULL){b->fault=1;e=I9100_BCM_TIMEOUT;}else e=I9100_BCM_BUSY;}
 else if(b->firmware_status)e=I9100_BCM_FIRMWARE;
 b->io.release(b->io.cookie);return e;
}
