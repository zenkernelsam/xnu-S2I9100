/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_wlan_control.h"
#include <string.h>
static uint16_t be16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t get_le32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void le32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4U;n++)p[n]=(uint8_t)(v>>(8U*n));}
static void wipe(void *buffer,unsigned n){volatile uint8_t *p=buffer;while(n--)*p++=0;}
static int valid_mac(const uint8_t *p){uint8_t v=0;for(unsigned n=0;n<6U;n++)v|=p[n];return v&&!(p[0]&1U);}
static int any_mac(const uint8_t *p){uint8_t v=0;for(unsigned n=0;n<6U;n++)v|=p[n];return v!=0;}
static int overlap(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a>UINTPTR_MAX-an||b>UINTPTR_MAX-bn||(a+an>b&&b+bn>a);}
int i9100_wlan_event_decode(const uint8_t *p,unsigned n,struct i9100_wlan_event *out)
{
 if(!p||!out||n<70U||n>2048U||overlap((uintptr_t)p,n,(uintptr_t)out,sizeof(*out)))return I9100_WLAN_ARGUMENT;
 if(be16(p+12)!=0x886cU||be16(p+14)!=0x8001U||p[18]||p[19]||p[20]!=0x10U||p[21]!=0x18U||be16(p+22)!=1U)
  return I9100_WLAN_EVENT;
 /* Host vendor parser trusts event datalen, not an undocumented alternate
  * interpretation of bcmeth.length. Bound that field to this packet too. */
 uint16_t carrier=be16(p+16);if(carrier<8U||carrier>n)return I9100_WLAN_EVENT;
 struct i9100_wlan_event e;memset(&e,0,sizeof(e));e.version=be16(p+24);
 unsigned header=e.version==1U?46U:e.version==2U?48U:0U;
 if(!header||24U+header>n)return I9100_WLAN_UNSUPPORTED;
 e.flags=be16(p+26);e.type=be32(p+28);e.status=be32(p+32);e.reason=be32(p+36);e.auth_type=be32(p+40);e.bytes=be32(p+44);
 if(e.bytes>n-24U-header)return I9100_WLAN_EVENT;
 memcpy(e.address,p+48,6);memcpy(e.ifname,p+54,16);e.ifname[16]=0;
 if(e.version==2U){e.ifidx=p[70];e.bsscfgidx=p[71];}
 e.data=p+24U+header;*out=e;return 0;
}
static int take(struct i9100_wlan_control *c)
{return !c||!c->initialized||!c->enabled||c->self!=(uintptr_t)c?I9100_WLAN_ARGUMENT:c->io.lock(c->io.cookie)?I9100_WLAN_BUSY:0;}
static void drop(struct i9100_wlan_control *c){c->io.unlock(c->io.cookie);}
static int fail(struct i9100_wlan_control *c,int e)
{
 c->state=I9100_WLAN_FAULT;c->error=e;c->link_seen=c->keyed=0;
 memset(c->bssid,0,6);wipe(c->config.pmk,sizeof(c->config.pmk));return e;
}
static int ready(struct i9100_wlan_control *c)
{
 int e=i9100_bcm_control_check(c->bus,c,c->generation);
 return e==I9100_BCM_BUSY?I9100_WLAN_BUSY:e?fail(c,I9100_WLAN_IO):0;
}
static int deadline(struct i9100_wlan_control *c)
{
 uint64_t now=c->io.now_ns(c->io.cookie);
 if(c->pending&&(now<c->request_started||now-c->request_started>500000000ULL))return fail(c,I9100_WLAN_TIMEOUT);
 uint64_t span=c->state==I9100_WLAN_CONFIGURING||c->state==I9100_WLAN_JOINING||c->state==I9100_WLAN_VERIFYING?15000000000ULL:
  c->state==I9100_WLAN_DISCONNECTING||c->state==I9100_WLAN_RESTORING?5000000000ULL:0;
 return span&&(now<c->started||now-c->started>span)?fail(c,I9100_WLAN_TIMEOUT):0;
}
int i9100_wlan_control_init(struct i9100_wlan_control *c,int enabled,struct i9100_bcm4330 *b,const struct i9100_wlan_io *io,uint32_t generation)
{
 if(!c||c->initialized||(enabled!=0&&enabled!=1))return I9100_WLAN_ARGUMENT;if(!enabled)return I9100_WLAN_DISABLED;
 if(!b||!b->initialized||!io||!io->lock||!io->unlock||!io->now_ns||!generation||
    overlap((uintptr_t)c,sizeof(*c),(uintptr_t)b,sizeof(*b))||overlap((uintptr_t)c,sizeof(*c),(uintptr_t)io,sizeof(*io)))return I9100_WLAN_ARGUMENT;
 if(io->lock(io->cookie))return I9100_WLAN_BUSY;
 int e=i9100_bcm_control_bind(b,c,generation);
 if(!e){memset(c,0,sizeof(*c));c->bus=b;c->io=*io;c->self=(uintptr_t)c;c->initialized=c->enabled=1;c->generation=generation;}
 io->unlock(io->cookie);return e;
}
int i9100_wlan_join(struct i9100_wlan_control *c,const struct i9100_wlan_config *config)
{
 if(!config||!config->ssid_bytes||config->ssid_bytes>32U||config->security>I9100_WLAN_WPA2_PSK_CCMP||
    (config->security==I9100_WLAN_OPEN?config->pmk_bytes!=0U:config->pmk_bytes!=32U)||
    (any_mac(config->bssid)&&!valid_mac(config->bssid)))return I9100_WLAN_ARGUMENT;
 if(!c||overlap((uintptr_t)config,sizeof(*config),(uintptr_t)c,sizeof(*c)))return I9100_WLAN_ARGUMENT;
 int e=take(c);if(e)return e;if(c->state!=I9100_WLAN_IDLE||c->pending||c->join_attempted){drop(c);return I9100_WLAN_BUSY;}
 c->config=*config;c->state=I9100_WLAN_CONFIGURING;c->step=0;c->started=c->io.now_ns(c->io.cookie);
 c->join_attempted=1;c->join_issued=c->ssid_ok=c->link_seen=c->keyed=c->verification_step=0;c->error=c->firmware_status=0;
 if(!config->security)wipe(c->config.pmk,32);
 memset(c->bssid,0,6);memset(c->event_address,0,6);memset(c->key_address,0,6);drop(c);return 0;
}
static int issue(struct i9100_wlan_control *c,uint32_t command,int set,const uint8_t *p,unsigned n)
{
 uint16_t id=0;int e=i9100_bcm_ioctl_owned(c->bus,c,command,set,p,n,&id);
 if(e==I9100_BCM_BUSY||e==I9100_BCM_CREDIT)return I9100_WLAN_BUSY;
 if(e)return fail(c,I9100_WLAN_IO);
 c->pending=1;c->request_id=id;c->request_command=command;c->request_capacity=n;c->request_started=c->io.now_ns(c->io.cookie);return 0;
}
int i9100_wlan_service(struct i9100_wlan_control *c)
{
 int e=take(c);if(e)return e;
 if(c->state==I9100_WLAN_FAULT){drop(c);return c->error;}
 e=ready(c);if(e){drop(c);return e;}
 e=deadline(c);if(e){drop(c);return e;}
 if(c->pending){drop(c);return I9100_WLAN_BUSY;}
 uint8_t buffer[80];memset(buffer,0,sizeof(buffer));
 if(c->state==I9100_WLAN_CONFIGURING){
  switch(c->step){
   case 0:memcpy(buffer,"event_msgs",11);e=issue(c,262,0,buffer,16);break;
   case 1:memcpy(buffer,"event_msgs",11);memcpy(buffer+11,c->event_mask,16);e=issue(c,263,1,buffer,27);
    if(!e)c->event_mask_owned=1;break;
   case 2:le32(buffer,1);e=issue(c,20,1,buffer,4);break;
   case 3:e=issue(c,22,1,buffer,4);break;
   case 4:le32(buffer,c->config.security?4U:0U);e=issue(c,134,1,buffer,4);break;
   case 5:le32(buffer,c->config.security?0x80U:0U);e=issue(c,165,1,buffer,4);break;
   case 6:memcpy(buffer,"sup_wpa",8);le32(buffer+8,c->config.security?1U:0U);e=issue(c,263,1,buffer,12);break;
   case 7:if(!c->config.security){c->step=8;break;}buffer[0]=32;memcpy(buffer+4,c->config.pmk,32);e=issue(c,268,1,buffer,68);break;
   case 8:e=issue(c,2,1,buffer,0);break;
   case 9:le32(buffer,c->config.ssid_bytes);memcpy(buffer+4,c->config.ssid,c->config.ssid_bytes);memcpy(buffer+36,c->config.bssid,6);
    e=issue(c,26,1,buffer,48);if(!e)c->join_issued=1;break;
   default:e=fail(c,I9100_WLAN_ARGUMENT);break;
  }
 }else if(c->state==I9100_WLAN_JOINING&&c->ssid_ok&&c->link_seen&&(!c->config.security||c->keyed)){
  c->state=I9100_WLAN_VERIFYING;c->verification_step=0;e=issue(c,23,0,buffer,6);
 }else if(c->state==I9100_WLAN_VERIFYING){
  e=c->verification_step?issue(c,25,0,buffer,36):issue(c,23,0,buffer,6);
 }else if(c->state==I9100_WLAN_DISCONNECTING&&c->step==0U)e=issue(c,52,1,buffer,0);
 else if(c->state==I9100_WLAN_RESTORING){memcpy(buffer,"event_msgs",11);memcpy(buffer+11,c->saved_event_mask,16);e=issue(c,263,1,buffer,27);}
 wipe(buffer,sizeof(buffer));drop(c);return e;
}
int i9100_wlan_reply(struct i9100_wlan_control *c,const struct i9100_bcm_frame *frame)
{
 if(!frame||frame->channel!=0U||!frame->request_id)return I9100_WLAN_ARGUMENT;
 int e=take(c);if(e)return e;
 if(frame->generation!=c->generation||!c->pending||frame->request_id!=c->request_id||frame->command!=c->request_command){drop(c);return I9100_WLAN_EVENT;}
 if(c->state==I9100_WLAN_FAULT){drop(c);return c->error;}
 e=deadline(c);if(e){drop(c);return e;}
 c->pending=0;c->firmware_status=frame->status;
 if(!frame->status&&(frame->bytes>c->request_capacity||(frame->bytes&&!frame->payload))){e=fail(c,I9100_WLAN_EVENT);drop(c);return e;}
 if(frame->status&&!(c->state==I9100_WLAN_CONFIGURING&&c->step==6U&&!c->config.security&&frame->status==-23)){
  e=fail(c,I9100_WLAN_FIRMWARE);drop(c);return e;
 }
 if(c->state==I9100_WLAN_CONFIGURING){
  if(c->step==0U){
   if(frame->bytes!=16U||!frame->payload)e=fail(c,I9100_WLAN_EVENT);
   else{memcpy(c->saved_event_mask,frame->payload,16);memcpy(c->event_mask,frame->payload,16);const unsigned events[]={0,5,6,11,12,16,46};
    for(unsigned n=0;n<sizeof(events)/sizeof(events[0]);n++)c->event_mask[events[n]/8]|=(uint8_t)(1U<<(events[n]%8));}
  }
  if(c->step==7U)wipe(c->config.pmk,32);
  if(!e){c->step++;if(c->step==10U)c->state=I9100_WLAN_JOINING;}
 }else if(c->state==I9100_WLAN_VERIFYING){
  if(!c->ssid_ok||!c->link_seen||(c->config.security&&(!c->keyed||memcmp(c->key_address,c->event_address,6))))e=fail(c,I9100_WLAN_NOT_LINKED);
  else if(c->verification_step==0U){
   if(frame->bytes!=6U||!frame->payload||!valid_mac(frame->payload)||
      (any_mac(c->config.bssid)&&memcmp(frame->payload,c->config.bssid,6))||memcmp(frame->payload,c->event_address,6))e=fail(c,I9100_WLAN_EVENT);
   else{memcpy(c->bssid,frame->payload,6);c->verification_step=1;}
  }else if(frame->bytes!=36U||!frame->payload||get_le32(frame->payload)!=c->config.ssid_bytes||
           memcmp(frame->payload+4,c->config.ssid,c->config.ssid_bytes))e=fail(c,I9100_WLAN_EVENT);
  else c->state=I9100_WLAN_UP;
 }else if(c->state==I9100_WLAN_DISCONNECTING){c->step=1;if(!c->link_seen)c->state=I9100_WLAN_RESTORING;}
 else if(c->state==I9100_WLAN_RESTORING){c->event_mask_owned=0;c->state=I9100_WLAN_IDLE;c->join_issued=c->ssid_ok=c->keyed=0;}
 else e=fail(c,I9100_WLAN_EVENT);
 drop(c);return e;
}
int i9100_wlan_event(struct i9100_wlan_control *c,const struct i9100_wlan_event *event,uint32_t generation)
{
 if(!event)return I9100_WLAN_ARGUMENT;int e=take(c);if(e)return e;
 if(generation!=c->generation){drop(c);return I9100_WLAN_EVENT;}
 if(event->version!=2U||event->ifidx||event->bsscfgidx||(event->flags&0x18U)){drop(c);return I9100_WLAN_UNSUPPORTED;}
 if(c->state==I9100_WLAN_FAULT){drop(c);return c->error;}
 e=deadline(c);if(e){drop(c);return e;}
 c->event_count++;c->last_event=event->type;c->last_event_status=event->status;c->last_reason=event->reason;
 if(!c->join_issued||c->state==I9100_WLAN_IDLE){drop(c);return 0;}
 int loss=(event->type==16U&&!(event->flags&1U))||event->type==5U||event->type==6U||event->type==11U||event->type==12U;
 if(loss){
  c->link_seen=c->keyed=0;c->link_losses++;memset(c->bssid,0,6);
  if(c->state==I9100_WLAN_DISCONNECTING){if(c->step==1U)c->state=I9100_WLAN_RESTORING;}
  else if(c->state!=I9100_WLAN_RESTORING)e=fail(c,I9100_WLAN_NOT_LINKED);
 }else if(event->type==0U){if(event->status)e=fail(c,I9100_WLAN_FIRMWARE);else c->ssid_ok=1;}
 else if(event->type==16U){
  if(c->state==I9100_WLAN_DISCONNECTING||c->state==I9100_WLAN_RESTORING||event->status||!valid_mac(event->address)||
     (any_mac(c->config.bssid)&&memcmp(event->address,c->config.bssid,6))||
     (c->link_seen&&memcmp(event->address,c->event_address,6))||
     (c->keyed&&memcmp(event->address,c->key_address,6)))e=fail(c,I9100_WLAN_EVENT);
  else{memcpy(c->event_address,event->address,6);c->link_seen=1;}
 }else if(event->type==46U&&c->config.security){
  if(event->status==7U||event->status>11U||event->reason)e=fail(c,I9100_WLAN_FIRMWARE);
  else if(!valid_mac(event->address)||(any_mac(c->config.bssid)&&memcmp(event->address,c->config.bssid,6))||
          (c->link_seen&&memcmp(event->address,c->event_address,6)))e=fail(c,I9100_WLAN_EVENT);
  else if(event->status==6U){memcpy(c->key_address,event->address,6);c->keyed=1;}
  else{
   c->keyed=0;
   /* A new key exchange must finish and be reverified before data resumes. */
   if(c->state==I9100_WLAN_UP){c->state=I9100_WLAN_JOINING;c->started=c->io.now_ns(c->io.cookie);}
   else if(c->state==I9100_WLAN_VERIFYING)e=fail(c,I9100_WLAN_NOT_LINKED);
  }
 }
 drop(c);return e;
}
int i9100_wlan_send(struct i9100_wlan_control *c,const uint8_t *p,unsigned n,unsigned priority)
{
 int e=take(c);if(e)return e;
 e=ready(c);
 if(!e)e=c->state!=I9100_WLAN_UP?I9100_WLAN_NOT_LINKED:i9100_bcm_send_data_owned(c->bus,c,p,n,priority);
 if(!e&&c->tx_frames!=UINT32_MAX)c->tx_frames++;
 if(e==I9100_BCM_IO||e==I9100_BCM_DISABLED)e=fail(c,I9100_WLAN_IO);
 drop(c);return e;
}
int i9100_wlan_data_allowed(struct i9100_wlan_control *c,const struct i9100_bcm_frame *frame)
{
 if(!frame||frame->channel!=2U||!frame->payload||frame->bytes<14U||frame->bytes>1518U)return I9100_WLAN_ARGUMENT;
 int e=take(c);if(e)return e;
 e=frame->generation!=c->generation?I9100_WLAN_EVENT:ready(c);
 if(!e&&c->state!=I9100_WLAN_UP)e=I9100_WLAN_NOT_LINKED;
 drop(c);return e;
}
int i9100_wlan_receive(struct i9100_wlan_control *c,const struct i9100_bcm_frame *frame)
{
 if(!frame)return I9100_WLAN_ARGUMENT;
 if(frame->channel==0U)return i9100_wlan_reply(c,frame);
 if(frame->channel==2U){int e=i9100_wlan_data_allowed(c,frame);return e?e:1;}
 int e=take(c);if(e)return e;
 if(frame->generation!=c->generation){drop(c);return I9100_WLAN_EVENT;}
 struct i9100_wlan_event event;
 e=frame->channel!=1U?I9100_WLAN_UNSUPPORTED:i9100_wlan_event_decode(frame->payload,frame->bytes,&event);
 if(e){e=fail(c,e);drop(c);return e;}
 drop(c);return i9100_wlan_event(c,&event,frame->generation);
}
int i9100_wlan_disconnect(struct i9100_wlan_control *c)
{
 int e=take(c);if(e)return e;
 if(c->state!=I9100_WLAN_UP||c->pending){drop(c);return I9100_WLAN_BUSY;}
 c->state=I9100_WLAN_DISCONNECTING;c->step=0;c->started=c->io.now_ns(c->io.cookie);wipe(c->config.pmk,32);drop(c);return 0;
}
int i9100_wlan_control_close(struct i9100_wlan_control *c)
{
 int e=take(c);if(e)return e;
 if((c->state!=I9100_WLAN_IDLE&&c->state!=I9100_WLAN_FAULT)||
    (c->state==I9100_WLAN_IDLE&&(c->pending||c->event_mask_owned))){drop(c);return I9100_WLAN_BUSY;}
 e=i9100_bcm_control_unbind(c->bus,c,c->state==I9100_WLAN_FAULT);
 if(!e){wipe(&c->config,sizeof(c->config));c->enabled=0;c->initialized=0;}
 drop(c);return e;
}
