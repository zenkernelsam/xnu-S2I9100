/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bt_stream.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static uint32_t u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint64_t add(uint64_t a,uint64_t b){return UINT64_MAX-a<b?UINT64_MAX:a+b;}
static int overlap(const void *a,size_t n,const void *b,size_t m)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>UINTPTR_MAX-n||y>UINTPTR_MAX-m||(x+n>y&&y+m>x);}
static int valid(const struct i9100_bt_stream *s)
{return s&&s->self==(uintptr_t)s&&s->status.initialized&&s->records<=I9100_BT_STREAM_PACKETS&&s->status.tx_queued<=I9100_BT_STREAM_CAP&&s->status.rx_queued<=I9100_BT_STREAM_CAP;}
static int fail(struct i9100_bt_stream *s,int error)
{s->status.fault=1;s->status.closing=1;if(!s->status.error)s->status.error=error;return error;}
static int enter(struct i9100_bt_stream *s,const void *client,int client_required)
{
 if(!valid(s))return I9100_BTS_ARGUMENT;
 if(s->entered)return I9100_BTS_BUSY;
 if(client_required&&(!client||s->client!=(uintptr_t)client||!s->status.owned))return I9100_BTS_OWNER;
 if(!s->status.enabled||s->status.closed)return I9100_BTS_DISABLED;
 if(s->status.fault)return I9100_BTS_FAULT;
 if(s->status.closing)return I9100_BTS_BUSY;
 s->entered=1;
 if(s->io.verify(s->io.cookie,s->status.generation)){s->entered=0;return fail(s,I9100_BTS_OWNER);}
 uint64_t now=s->io.now_ns(s->io.cookie);
 if(now<s->status.last_ns){s->entered=0;return fail(s,I9100_BTS_CLOCK);}
 if(now==s->status.last_ns){if(++s->stalled>=4096U){s->entered=0;return fail(s,I9100_BTS_CLOCK);}}
 else s->stalled=0;
 s->status.last_ns=now;
 if(s->status.host_reset_pending&&now-s->status.host_reset_accepted_ns>1900000000ULL){s->entered=0;return fail(s,I9100_BTS_TIMEOUT);}
 return 0;
}
static int leave(struct i9100_bt_stream *s,int error){s->entered=0;return error;}
static int command_allowed(uint16_t op)
{return op&&(op>>10)&&(op>>10)!=0x3fU&&op!=0x0c03U&&op!=0x0c35U;}
/* Validate available headers without consuming or submitting any packet. */
static int packet(const uint8_t *p,unsigned bytes,unsigned *length,int reset_enabled)
{
 if(!bytes){*length=0;return 0;}
 unsigned header=p[0]==1U?4U:p[0]==2U?5U:0U;
 if(!header)return I9100_BTS_UNSUPPORTED;
 if(bytes<header){*length=header;return 0;}
 if(p[0]==1U){if(u16(p+1)==0x0c03U&&reset_enabled){if(p[3])return I9100_BTS_UNSUPPORTED;}
  else if(!command_allowed(u16(p+1)))return I9100_BTS_UNSUPPORTED;*length=4U+p[3];}
 else {if(!u16(p+3)||u16(p+3)>4096U||(u16(p+1)&0xc000U))return I9100_BTS_FRAME;*length=5U+u16(p+3);}
 return 0;
}
static int attributes(const uint8_t *p,unsigned bytes,uint32_t baud)
{
 if(bytes!=I9100_BT_TERMIOS_BYTES||u32(p+36)!=baud||u32(p+40)!=baud||
    (u32(p)&~4U)||u32(p+4)||u32(p+8)!=0x38b00U||u32(p+12)||p[32]!=1U||p[33])return I9100_BTS_UNSUPPORTED;
 return 0;
}
int i9100_bt_stream_init(struct i9100_bt_stream *s,int enabled,const struct i9100_bt_stream_io *io,uint32_t generation,uint32_t baud)
{
 if(!s)return I9100_BTS_ARGUMENT;if(enabled!=1)return I9100_BTS_DISABLED;
 if(s->status.initialized)return I9100_BTS_BUSY;
 if(!io||!io->verify||!io->now_ns||!io->command||!io->acl_link||!io->acl_send||!io->receive||
    !generation||baud!=115200U||overlap(s,sizeof(*s),io,sizeof(*io)))return I9100_BTS_ARGUMENT;
 memset(s,0,sizeof(*s));s->self=(uintptr_t)s;s->io=*io;s->status.initialized=s->status.enabled=1;
 s->status.generation=generation;s->status.baud=baud;s->status.last_ns=io->now_ns(io->cookie);
 put32(s->attributes,4);put32(s->attributes+8,0x38b00);s->attributes[32]=1;
 put32(s->attributes+36,baud);put32(s->attributes+40,baud);return 0;
}
int i9100_bt_stream_open(struct i9100_bt_stream *s,const void *client)
{
 if(!client||!s||overlap(s,sizeof(*s),client,1))return I9100_BTS_ARGUMENT;
 int e=enter(s,0,0);if(e)return e;
 if(s->status.owned||s->client)e=I9100_BTS_BUSY;else{s->client=(uintptr_t)client;s->status.owned=1;}
 return leave(s,e);
}
int i9100_bt_stream_allow_host_reset(struct i9100_bt_stream *s,int enabled)
{
 if(!valid(s)||(enabled!=0&&enabled!=1))return I9100_BTS_ARGUMENT;
 if(s->entered||s->status.owned||s->client||s->status.closed||s->status.fault)return I9100_BTS_BUSY;
 s->status.host_reset_enabled=(uint32_t)enabled;return 0;
}
int i9100_bt_stream_pause(struct i9100_bt_stream *s,int paused)
{
 if(!valid(s)||(paused!=0&&paused!=1))return I9100_BTS_ARGUMENT;
 if(s->entered||s->status.closed||s->status.fault)return I9100_BTS_BUSY;
 s->status.paused=(uint32_t)paused;return 0;
}
int i9100_bt_stream_write(struct i9100_bt_stream *s,const void *client,const uint8_t *p,unsigned bytes)
{
 if(!s||!p||!bytes||bytes>I9100_BT_STREAM_CAP||overlap(s,sizeof(*s),p,bytes))return I9100_BTS_ARGUMENT;
 int e=enter(s,client,1);if(e)return e;
 if(s->status.paused||s->status.host_reset_pending)return leave(s,I9100_BTS_AGAIN);
 if(bytes>I9100_BT_STREAM_CAP-s->status.tx_queued)return leave(s,I9100_BTS_AGAIN);
 unsigned previous=s->status.tx_queued,old_records=s->records;memcpy(s->tx+previous,p,bytes);unsigned total=previous+bytes,index=0;
 for(unsigned at=0;at<total;){unsigned length=0;e=packet(s->tx+at,total-at,&length,s->status.host_reset_enabled);if(e)break;
  unsigned header=s->tx[at]==1U?4U:5U;if(total-at<header)break;
  if(s->tx[at]==1U&&u16(s->tx+at+1)==0x0c03U&&(at||total!=4U||s->status.rx_queued)){e=I9100_BTS_BUSY;break;}
  if(index>=s->records){
   if(s->records==I9100_BT_STREAM_PACKETS){e=I9100_BTS_AGAIN;break;}
   s->tx_record[index].bytes=length;s->tx_record[index].generation=0;s->tx_record[index].handle=0;
   if(s->tx[at]==2U){struct i9100_bt_acl_link link={0};uint16_t handle=u16(s->tx+at+1)&0xfffU;
    e=s->io.acl_link(s->io.cookie,handle,&link);if(e)break;
    s->tx_record[index].handle=handle;s->tx_record[index].generation=link.generation;}
   s->records++;
  }else if(s->tx_record[index].bytes!=length){e=I9100_BTS_FRAME;break;}
  index++;
  if(length>total-at)break;at+=length;}
 if(e){memset(s->tx+previous,0,bytes);memset(s->tx_record+old_records,0,(I9100_BT_STREAM_PACKETS-old_records)*sizeof(s->tx_record[0]));s->records=old_records;
  return leave(s,(e==I9100_BTS_AGAIN||e==I9100_BTS_BUSY)?e:fail(s,e));}
 if(!previous)s->tx_wait_ns=s->status.last_ns;
 if(total==4U&&s->tx[0]==1U&&u16(s->tx+1)==0x0c03U){s->status.host_reset_pending=1;
  s->status.host_reset_accepted_ns=s->tx_wait_ns;}
 s->status.tx_queued=total;s->status.tx_accepted=add(s->status.tx_accepted,bytes);return leave(s,0);
}
int i9100_bt_stream_receive(struct i9100_bt_stream *s,const struct i9100_bt_frame *f)
{
 if(!s||!f||!f->bytes||!f->length||f->length>4100U||overlap(s,sizeof(*s),f,sizeof(*f))||
    overlap(s,sizeof(*s),f->bytes,f->length))return I9100_BTS_ARGUMENT;
 int e=enter(s,0,0);if(e)return e;
 if(!s->status.owned)return leave(s,I9100_BTS_OWNER);
 if((f->type==4U&&(f->length<2U||f->length!=2U+f->bytes[1]))||
    (f->type==2U&&(f->length<4U||f->length!=4U+u16(f->bytes+2)))||
    (f->type!=2U&&f->type!=4U))return leave(s,fail(s,I9100_BTS_FRAME));
 unsigned bytes=f->length+1U;
 if(bytes>I9100_BT_STREAM_CAP-s->status.rx_queued)return leave(s,fail(s,I9100_BTS_IO));
 if(!s->status.rx_queued)s->rx_wait_ns=s->status.last_ns;
 s->rx[s->status.rx_queued]=f->type;memcpy(s->rx+s->status.rx_queued+1U,f->bytes,f->length);
 if(s->status.host_reset_pending&&f->type==4U&&f->length==6U&&f->bytes[0]==0x0eU&&u16(f->bytes+3)==0x0c03U)
  s->reset_reply_queued=1U;
 s->status.rx_queued+=bytes;s->status.rx_accepted=add(s->status.rx_accepted,bytes);s->status.rx_packets=add(s->status.rx_packets,1);
 return leave(s,0);
}
static int consume_tx(struct i9100_bt_stream *s)
{
 unsigned length=0;int e=packet(s->tx,s->status.tx_queued,&length,s->status.host_reset_enabled);if(e)return fail(s,e);
 if(length>s->status.tx_queued)return I9100_BTS_AGAIN;
 if(!s->records||s->tx_record[0].bytes!=length)return fail(s,I9100_BTS_FRAME);
 int reset=s->tx[0]==1U&&u16(s->tx+1)==0x0c03U;
 if(s->tx[0]==1U)e=s->io.command(s->io.cookie,u16(s->tx+1),s->tx+4,s->tx[3]);
 else e=s->io.acl_send(s->io.cookie,s->tx_record[0].handle,s->tx_record[0].generation,(u16(s->tx+1)>>12)&3U,s->tx+5,length-5U);
 if(e==I9100_BTS_AGAIN)return e;if(e)return fail(s,e);
 s->status.tx_queued-=length;memmove(s->tx,s->tx+length,s->status.tx_queued);
 s->records--;memmove(s->tx_record,s->tx_record+1,s->records*sizeof(s->tx_record[0]));memset(s->tx_record+s->records,0,sizeof(s->tx_record[0]));
 memset(s->tx+s->status.tx_queued,0,length);s->status.tx_submitted=add(s->status.tx_submitted,length);s->status.tx_packets=add(s->status.tx_packets,1);
 if(reset)s->status.paused=1;
 if(s->status.tx_queued)s->tx_wait_ns=s->status.last_ns;return 0;
}
int i9100_bt_stream_step(struct i9100_bt_stream *s,unsigned budget)
{
 if(!budget||budget>4U)return I9100_BTS_ARGUMENT;int e=enter(s,0,0);if(e)return e;
 if(!s->status.owned)return leave(s,I9100_BTS_OWNER);
 if(s->status.paused)return leave(s,0);
 if((s->status.tx_queued&&s->status.last_ns-s->tx_wait_ns>1000000000ULL)||
    (s->status.rx_queued&&s->status.last_ns-s->rx_wait_ns>1000000000ULL))return leave(s,fail(s,I9100_BTS_TIMEOUT));
 if(s->status.rx_queued){e=s->io.receive(s->io.cookie,s->rx,s->status.rx_queued);
  if(e&&e!=I9100_BTS_AGAIN)return leave(s,fail(s,e));
  if(!e){s->status.rx_delivered=add(s->status.rx_delivered,s->status.rx_queued);memset(s->rx,0,s->status.rx_queued);s->status.rx_queued=0;
   if(s->reset_reply_queued){s->status.host_reset_pending=0;s->reset_reply_queued=0;}}}
 for(unsigned n=0;n<budget&&s->status.tx_queued;n++){e=consume_tx(s);if(e==I9100_BTS_AGAIN)break;if(e)return leave(s,e);
  if(s->status.paused)break;}
 return leave(s,0);
}
int i9100_bt_stream_get_attributes(struct i9100_bt_stream *s,const void *client,uint8_t *p,unsigned bytes)
{
 if(!s||!p||bytes!=I9100_BT_TERMIOS_BYTES||overlap(s,sizeof(*s),p,bytes))return I9100_BTS_ARGUMENT;
 int e=enter(s,client,1);if(e)return e;memcpy(p,s->attributes,bytes);return leave(s,0);
}
int i9100_bt_stream_set_attributes(struct i9100_bt_stream *s,const void *client,const uint8_t *p,unsigned bytes)
{
 if(!s||!p||bytes!=I9100_BT_TERMIOS_BYTES||overlap(s,sizeof(*s),p,bytes))return I9100_BTS_ARGUMENT;
 int e=enter(s,client,1);if(e)return e;e=attributes(p,bytes,s->status.baud);
 if(!e)memcpy(s->attributes,p,bytes);return leave(s,e);
}
int i9100_bt_stream_close(struct i9100_bt_stream *s,const void *client)
{
 if(!valid(s)||s->client!=(uintptr_t)client||(!client&&s->status.owned))return I9100_BTS_OWNER;
 if(s->entered)return I9100_BTS_BUSY;
 if(s->status.closed)return 0;
 s->status.tx_discarded=add(s->status.tx_discarded,s->status.tx_queued);s->status.rx_discarded=add(s->status.rx_discarded,s->status.rx_queued);
 memset(s->tx,0,sizeof(s->tx));memset(s->rx,0,sizeof(s->rx));s->status.tx_queued=s->status.rx_queued=0;
 memset(s->tx_record,0,sizeof(s->tx_record));s->records=0;
 s->status.host_reset_pending=s->reset_reply_queued=0;
 s->status.owned=s->status.enabled=0;s->status.closing=s->status.closed=1;return 0;
}
int i9100_bt_stream_abort(struct i9100_bt_stream *s,int reason)
{if(!valid(s)||!reason)return I9100_BTS_ARGUMENT;if(s->entered)return I9100_BTS_BUSY;return fail(s,reason);}
int i9100_bt_stream_status(const struct i9100_bt_stream *s,struct i9100_bt_stream_status *out)
{if(!valid(s)||!out||overlap(s,sizeof(*s),out,sizeof(*out)))return I9100_BTS_ARGUMENT;*out=s->status;return 0;}
