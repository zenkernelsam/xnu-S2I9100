/* SPDX-License-Identifier: BSD-2-Clause
 * Independent wire producer. Production F2+CDC and WLAN code are linked;
 * no fake successful ioctl/controller functions. Only card IO is a stand-in.
 * All SSIDs, MACs and PMKs here are synthetic, never real credentials. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_wlan_control.h"
#include "i9100_bcm_f2.h"

static struct i9100_sdio card;
static struct i9100_bcm_f2 f2;
static struct i9100_bcm4330 bus;
static struct i9100_wlan_control ctl;
static struct model {
 unsigned bus_locked,control_locked,tx_count,tx_bytes,rx_bytes,rx_cursor,tx_fail,rx_fail;
 uint64_t ns;
 uint8_t clock,window[3],tx[4096],rx[4096],output[2048],rx_sequence;
} m;
static const uint8_t ap[6]={2,1,2,3,4,5},other_ap[6]={2,6,7,8,9,10};
static const uint8_t old_mask[16]={0x80,0x80,0x20,0x10,0x08,0x08,0x02,0x01,0xff,1,2,3,4,5,6,7};
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4U;n++)p[n]=(uint8_t)(v>>(8U*n));}
static void net16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void net32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4U;n++)p[n]=(uint8_t)(v>>(24U-8U*n));}
static uint32_t get32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int bus_lock(void *cookie){assert(cookie==&m);if(m.bus_locked)return -1;m.bus_locked=1;return 0;}
static void bus_unlock(void *cookie){assert(cookie==&m&&m.bus_locked);m.bus_locked=0;}
static int control_lock(void *cookie){assert(cookie==&m);if(m.control_locked)return -1;m.control_locked=1;return 0;}
static void control_unlock(void *cookie){assert(cookie==&m&&m.control_locked);m.control_locked=0;}
static uint64_t now(void *cookie){assert(cookie==&m);return m.ns;}
static int delay(void *cookie,uint32_t us){assert(cookie==&m);m.ns+=(uint64_t)us*1000U;return 0;}
int i9100_sdio_cmd52(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,uint8_t *v)
{
 assert(s==&card&&m.bus_locked);if(s->fault)return I9100_SDIO_IO;
 if(fn==0U){assert(!write);switch(address){case 2:case 3:*v=6;break;case 0x210:*v=0;break;case 0x211:*v=2;break;default:assert(0);}return 0;}
 assert(fn==1U);uint8_t *reg=address==0x1000eU?&m.clock:
  address==0x1000aU?&m.window[0]:address==0x1000bU?&m.window[1]:address==0x1000cU?&m.window[2]:0;
 assert(reg);if(write)*reg=address==0x1000eU?(*v&63U)|64U:*v;else *v=*reg;return 0;
}
int i9100_sdio_cmd53(struct i9100_sdio *s,int write,unsigned fn,uint32_t address,int increment,uint8_t *p,uint32_t n)
{
 assert(s==&card&&m.bus_locked);if(s->fault)return I9100_SDIO_IO;
 if(fn==1U){assert(!write&&increment&&address==0x8000U&&n==4U&&m.window[1]==0&&m.window[2]==0x18U);put32(p,0x00104330U);return 0;}
 assert(fn==2U&&!increment&&address==0x8000U&&n<=2048U&&!(n&3U)&&(n<=512U||!(n&511U)));
 if(write){assert(m.control_locked);memcpy(m.tx,p,n);m.tx_bytes=n;m.tx_count++;if(m.tx_fail){s->fault=1;return I9100_SDIO_IO;}}
 else{if(m.rx_fail){s->fault=1;return I9100_SDIO_IO;}assert(m.rx_cursor+n<=m.rx_bytes);memcpy(p,m.rx+m.rx_cursor,n);m.rx_cursor+=n;}
 return 0;
}
static struct i9100_wlan_config config(unsigned security)
{
 struct i9100_wlan_config c;memset(&c,0,sizeof(c));c.ssid_bytes=6;memcpy(c.ssid,"S2-lab",6);memcpy(c.bssid,ap,6);
 c.security=security;c.pmk_bytes=security?32U:0U;for(unsigned n=0;n<32U;n++)c.pmk[n]=(uint8_t)(0x80U+n);return c;
}
static void init(unsigned security)
{
 memset(&m,0,sizeof(m));memset(&bus,0,sizeof(bus));memset(&f2,0,sizeof(f2));memset(&ctl,0,sizeof(ctl));memset(&card,0,sizeof(card));
 m.ns=100000000000ULL;m.clock=64;m.window[0]=0x80;m.window[1]=3;m.window[2]=2;
 card.initialized=card.active=1;card.functions=2;card.block_size[1]=64;card.block_size[2]=512;
 struct i9100_f2_io fio={&m,bus_lock,bus_unlock,now,delay};assert(!i9100_bcm_f2_init(&f2,&card,&fio));
 struct i9100_bcm_io bio;assert(!i9100_bcm_f2_get_io(&f2,&bio)&&!i9100_bcm_init(&bus,&bio));
 assert(!i9100_bcm_enable(&bus,1,0x2d0,0x4330));assert(!i9100_bcm_f2_enable(&f2,1,1));assert(!i9100_bcm_protocol_start(&bus,4,1));
 struct i9100_wlan_io wio={&m,control_lock,control_unlock,now};
 assert(i9100_wlan_control_init(&ctl,0,&bus,&wio,1)==I9100_WLAN_DISABLED&&!ctl.initialized&&!m.tx_count);
 assert(i9100_wlan_control_init(&ctl,1,&bus,&wio,2)==I9100_BCM_ARGUMENT&&!ctl.initialized);
 assert(!i9100_wlan_control_init(&ctl,1,&bus,&wio,1)&&bus.control_owner==&ctl);
 struct i9100_wlan_config c=config(security);assert(!i9100_wlan_join(&ctl,&c));assert(c.pmk[0]==0x80U&&!m.tx_count);
}
static void header(unsigned logical,unsigned channel)
{
 memset(m.rx,0,sizeof(m.rx));put16(m.rx,logical);put16(m.rx+2,(uint16_t)~logical);
 m.rx[4]=m.rx_sequence++;m.rx[5]=(uint8_t)channel;m.rx[7]=12;m.rx[9]=(uint8_t)(bus.tx_sequence+32U);
 unsigned rest=logical>32U?logical-32U:0U;rest=rest>512U?(rest+511U)&~511U:(rest+3U)&~3U;
 m.rx_bytes=32U+rest;m.rx_cursor=0;
}
static int consume(struct i9100_bcm_frame *out)
{
 unsigned bytes=0;assert(!i9100_bcm_f2_receive(&f2,m.output,sizeof(m.output),&bytes));
 int e=i9100_bcm_receive(&bus,m.output,bytes,out);return e;
}
static int reply(uint32_t command,uint16_t id,uint32_t declared,int32_t status,const uint8_t *body,unsigned n)
{
 header(28U+n,0);put32(m.rx+12,command);put32(m.rx+16,declared);put32(m.rx+20,((uint32_t)id<<16)|(status?1U:0U));put32(m.rx+24,(uint32_t)status);
 if(n)memcpy(m.rx+28,body,n);struct i9100_bcm_frame f;int e=consume(&f);
 if(e&&e!=I9100_BCM_FIRMWARE)return e;return i9100_wlan_receive(&ctl,&f);
}
static int ack(int32_t status,const uint8_t *p,unsigned n)
{return reply(ctl.request_command,ctl.request_id,n,status,p,n);}
static void event_bytes(uint8_t *p,unsigned version,unsigned type,unsigned flags,unsigned status,unsigned reason,const uint8_t *mac)
{
 memset(p,0,80);net16(p+12,0x886c);net16(p+14,0x8001);net16(p+16,58);p[20]=0x10;p[21]=0x18;net16(p+22,1);
 net16(p+24,version);net16(p+26,flags);net32(p+28,type);net32(p+32,status);net32(p+36,reason);memcpy(p+48,mac,6);memcpy(p+54,"wlan0",5);
}
static int event(unsigned type,unsigned flags,unsigned status,unsigned reason,const uint8_t *mac)
{
 header(88,1);m.rx[12]=0x20;event_bytes(m.rx+16,2,type,flags,status,reason,mac);struct i9100_bcm_frame f;
 assert(!consume(&f));return i9100_wlan_receive(&ctl,&f);
}
static int data(void)
{
 header(80,2);m.rx[12]=0x20;memcpy(m.rx+16,ap,6);struct i9100_bcm_frame f;assert(!consume(&f));return i9100_wlan_receive(&ctl,&f);
}
static void assert_tx(uint32_t command,unsigned bytes,int set)
{
 assert(ctl.pending&&bus.pending&&m.tx_count&&get32(m.tx+12)==command&&get32(m.tx+16)==bytes);
 assert(get32(m.tx+20)==(((uint32_t)ctl.request_id<<16)|(set?2U:0U)));
 for(unsigned n=0;n<sizeof(f2.scratch);n++)assert(!f2.scratch[n]);
 for(unsigned n=12;n<sizeof(f2.last_header);n++)assert(!f2.last_header[n]);
}
static void send_step(unsigned step,unsigned security)
{
 if(step==7U&&!security){assert(!i9100_wlan_service(&ctl)&&!ctl.pending);return;}
 assert(!i9100_wlan_service(&ctl));
 static const unsigned commands[10]={262,263,20,22,134,165,263,268,2,26},lengths[10]={16,27,4,4,4,4,12,68,0,48};
 assert_tx(commands[step],lengths[step],step!=0U);
 const uint8_t *p=m.tx+28;
 switch(step){
 case 0:assert(!memcmp(p,"event_msgs\0",11));break;
 case 1:{uint8_t expected[16];memcpy(expected,old_mask,16);expected[0]|=0x61;expected[1]|=0x18;expected[2]|=1;expected[5]|=0x40;
  assert(!memcmp(p,"event_msgs\0",11)&&!memcmp(p+11,expected,16));break;}
 case 2:assert(get32(p)==1);break;case 3:assert(get32(p)==0);break;
 case 4:assert(get32(p)==(security?4U:0U));break;case 5:assert(get32(p)==(security?0x80U:0U));break;
 case 6:assert(!memcmp(p,"sup_wpa\0",8)&&get32(p+8)==(security?1U:0U));break;
 case 7:assert(p[0]==32&&!p[1]&&!p[2]&&!p[3]);for(unsigned n=0;n<32U;n++)assert(p[4+n]==0x80U+n);
  for(unsigned n=36;n<68U;n++)assert(!p[n]);break;
 case 8:break;case 9:assert(get32(p)==6&&!memcmp(p+4,"S2-lab",6)&&!memcmp(p+36,ap,6));
  for(unsigned n=42;n<48U;n++)assert(!p[n]);break;default:assert(0);
 }
}
static void configure(unsigned security)
{
 for(unsigned step=0;step<10U;step++){
  send_step(step,security);if(step==7U&&!security)continue;
  if(step==0U)assert(!ack(0,old_mask,16));else assert(!ack(0,0,0));
  if(step==7U)for(unsigned n=0;n<32U;n++)assert(!ctl.config.pmk[n]);
 }
 assert(ctl.state==I9100_WLAN_JOINING&&ctl.join_issued&&ctl.event_mask_owned&&!ctl.pending);
}
static void verify(void)
{
 assert(!i9100_wlan_service(&ctl));assert_tx(23,6,0);assert(!ack(0,ap,6));assert(ctl.state==I9100_WLAN_VERIFYING);
 assert(!i9100_wlan_service(&ctl));assert_tx(25,36,0);uint8_t ssid[36]={0};put32(ssid,6);memcpy(ssid+4,"S2-lab",6);assert(!ack(0,ssid,36));
 assert(ctl.state==I9100_WLAN_UP&&!memcmp(ctl.bssid,ap,6));
}
static void link(unsigned security)
{init(security);configure(security);assert(!event(0,0,0,0,ap)&&!event(16,1,0,0,ap));if(security)assert(!event(46,0,6,0,ap));verify();}
static void close_fault(void)
{
 assert(ctl.state==I9100_WLAN_FAULT&&bus.control_owner==&ctl);
 for(unsigned n=0;n<32U;n++)assert(!ctl.config.pmk[n]);unsigned count=m.tx_count;
 assert(i9100_wlan_service(&ctl)==ctl.error&&m.tx_count==count);assert(!i9100_wlan_control_close(&ctl)&&bus.fault&&!bus.control_owner);
}
static void decoder_tests(void)
{
 uint8_t p[80];struct i9100_wlan_event e;event_bytes(p,2,16,1,0x01020304,0x55667788,ap);net32(p+40,0x11223344);net32(p+44,8);
 p[70]=3;p[71]=4;memcpy(p+72,"PAYLOAD!",8);
 assert(!i9100_wlan_event_decode(p,80,&e)&&e.version==2&&e.type==16&&e.flags==1&&e.status==0x01020304&&e.reason==0x55667788&&
  e.auth_type==0x11223344&&e.bytes==8&&e.ifidx==3&&e.bsscfgidx==4&&!memcmp(e.data,"PAYLOAD!",8));
 for(unsigned n=0;n<80U;n++)assert(i9100_wlan_event_decode(p,n,&e)!=0);
 for(unsigned field=0;field<6U;field++){event_bytes(p,2,0,0,0,0,ap);const unsigned offsets[6]={12,14,18,20,21,22};p[offsets[field]]^=1;assert(i9100_wlan_event_decode(p,72,&e)==I9100_WLAN_EVENT);}
 event_bytes(p,3,0,0,0,0,ap);assert(i9100_wlan_event_decode(p,72,&e)==I9100_WLAN_UNSUPPORTED);
 event_bytes(p,1,0,0,0,0,ap);assert(!i9100_wlan_event_decode(p,70,&e)&&e.version==1&&e.data==p+70);
 event_bytes(p,2,0,0,0,0,ap);net32(p+44,UINT32_MAX);assert(i9100_wlan_event_decode(p,72,&e)==I9100_WLAN_EVENT);
 event_bytes(p,2,0,0,0,0,ap);net16(p+16,81);assert(i9100_wlan_event_decode(p,72,&e)==I9100_WLAN_EVENT);
}
static void association_tests(void)
{
 static const unsigned orders[6][3]={{0,16,46},{0,46,16},{16,0,46},{16,46,0},{46,0,16},{46,16,0}};
 uint8_t ethernet[64]={0};
 for(unsigned test=0;test<6U;test++){
  init(1);configure(1);assert(i9100_wlan_control_close(&ctl)==I9100_WLAN_BUSY);
  for(unsigned n=0;n<3U;n++){
   assert(i9100_wlan_send(&ctl,ethernet,64,0)==I9100_WLAN_NOT_LINKED&&data()==I9100_WLAN_NOT_LINKED);
   unsigned type=orders[test][n];assert(!event(type,type==16U?1U:0U,type==46U?6U:0U,0,ap));
  }
  verify();assert(data()==1&&!i9100_wlan_send(&ctl,ethernet,64,3)&&ctl.tx_frames==1&&m.tx[12]==0x20&&m.tx[13]==3);
  assert(i9100_bcm_send_data(&bus,ethernet,64,0)==I9100_BCM_BUSY&&i9100_bcm_ioctl(&bus,2,1,0,0)==I9100_BCM_BUSY);
  assert(i9100_bcm_enable(&bus,0,0,0)==I9100_BCM_BUSY);
 }
 init(0);configure(0);assert(!event(16,1,0,0,ap)&&!event(0,0,0,0,ap));verify();assert(data()==1);
 /* Firmware events may arrive before the SET_SSID control ACK. */
 init(1);for(unsigned s=0;s<9U;s++){send_step(s,1);assert(!ack(0,s?0:old_mask,s?0:16));}
 send_step(9,1);assert(!event(46,0,6,0,ap)&&!event(16,1,0,0,ap)&&!event(0,0,0,0,ap));assert(!ack(0,0,0));verify();
 /* Explicit OPEN only permits unsupported internal-supplicant disable. */
 init(0);for(unsigned s=0;s<6U;s++){send_step(s,0);assert(!ack(0,s?0:old_mask,s?0:16));}
 send_step(6,0);assert(!reply(263,ctl.request_id,4096,-23,0,0)&&!bus.fault&&ctl.step==7);
 init(1);for(unsigned s=0;s<6U;s++){send_step(s,1);assert(!ack(0,s?0:old_mask,s?0:16));}
 send_step(6,1);assert(ack(-23,0,0)==I9100_WLAN_FIRMWARE&&ctl.config.security==1);close_fault();
}
static void loss_tests(void)
{
 for(unsigned state=0;state<3U;state++)for(unsigned kind=0;kind<6U;kind++){
  init(1);configure(1);assert(!event(0,0,0,0,ap)&&!event(16,1,0,0,ap)&&!event(46,0,6,0,ap));
  if(state==1U){assert(!i9100_wlan_service(&ctl));assert_tx(23,6,0);}else if(state==2U)verify();
  static const unsigned types[6]={16,5,6,11,12,46};unsigned type=types[kind];
  int e=event(type,0,type==46U?7U:0U,0,ap);assert(e==(type==46U?I9100_WLAN_FIRMWARE:I9100_WLAN_NOT_LINKED));
  uint8_t ethernet[64]={0};assert(i9100_wlan_send(&ctl,ethernet,64,0)!=0);close_fault();
 }
 init(1);configure(1);assert(event(46,0,6,0,other_ap)==I9100_WLAN_EVENT&&!ctl.keyed);close_fault();
 init(1);configure(1);assert(!event(46,0,6,0,ap));assert(event(16,1,0,0,other_ap)==I9100_WLAN_EVENT);close_fault();
 init(1);configure(1);assert(!event(0,0,0,0,ap)&&!event(16,1,0,0,ap)&&!event(46,0,6,0,ap));assert(!i9100_wlan_service(&ctl));
 assert(ack(0,other_ap,6)==I9100_WLAN_EVENT);close_fault();
 init(1);configure(1);assert(!event(0,0,0,0,ap)&&!event(16,1,0,0,ap)&&!event(46,0,6,0,ap));assert(!i9100_wlan_service(&ctl)&&!ack(0,ap,6));
 assert(!i9100_wlan_service(&ctl));uint8_t wrong[36]={0};put32(wrong,6);memcpy(wrong+4,"WRONG!",6);assert(ack(0,wrong,36)==I9100_WLAN_EVENT);close_fault();
 link(1);assert(!event(46,0,5,0,ap)&&ctl.state==I9100_WLAN_JOINING&&!ctl.keyed&&data()==I9100_WLAN_NOT_LINKED);
 assert(!event(46,0,6,0,ap));verify();
}
static void disconnect_tests(void)
{
 for(unsigned first=0;first<2U;first++){
  link(1);assert(!i9100_wlan_disconnect(&ctl)&&data()==I9100_WLAN_NOT_LINKED);assert(!i9100_wlan_service(&ctl));assert_tx(52,0,1);
  unsigned count=m.tx_count;assert(i9100_wlan_service(&ctl)==I9100_WLAN_BUSY&&m.tx_count==count);
  if(first)assert(!event(16,0,0,0,ap));assert(!ack(0,0,0));
  if(!first){for(unsigned n=0;n<10U;n++)assert(!i9100_wlan_service(&ctl)&&m.tx_count==count);assert(!event(16,0,0,0,ap));}
  assert(ctl.state==I9100_WLAN_RESTORING&&!i9100_wlan_service(&ctl));assert_tx(263,27,1);assert(!memcmp(m.tx+39,old_mask,16));
  assert(!ack(0,0,0)&&ctl.state==I9100_WLAN_IDLE&&!ctl.event_mask_owned);struct i9100_wlan_config c=config(1);
  assert(i9100_wlan_join(&ctl,&c)==I9100_WLAN_BUSY);assert(!i9100_wlan_control_close(&ctl)&&!bus.control_owner&&!bus.fault);
  struct i9100_wlan_io wio={&m,control_lock,control_unlock,now};assert(i9100_wlan_control_init(&ctl,1,&bus,&wio,1)==I9100_BCM_ARGUMENT);
 }
 link(0);assert(!i9100_wlan_disconnect(&ctl)&&!i9100_wlan_service(&ctl)&&!ack(0,0,0));unsigned count=m.tx_count;m.ns+=5000000001ULL;
 assert(i9100_wlan_service(&ctl)==I9100_WLAN_TIMEOUT&&m.tx_count==count);close_fault();
 link(0);assert(!i9100_wlan_disconnect(&ctl)&&!i9100_wlan_service(&ctl));assert(!event(16,0,0,0,ap)&&!ack(0,0,0));m.tx_fail=1;
 assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO&&ctl.event_mask_owned);close_fault();
}
static void failure_tests(void)
{
 for(unsigned bad=0;bad<10U;bad++){
  init(1);for(unsigned s=0;s<bad;s++){send_step(s,1);assert(!ack(0,s?0:old_mask,s?0:16));}
  send_step(bad,1);assert(ack(-14,0,0)==I9100_WLAN_FIRMWARE);close_fault();
  init(1);for(unsigned s=0;s<bad;s++){send_step(s,1);assert(!ack(0,s?0:old_mask,s?0:16));}
  m.tx_fail=1;assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO&&bus.fault&&card.fault);close_fault();
 }
 init(1);send_step(0,1);unsigned count=m.tx_count;assert(i9100_wlan_control_close(&ctl)==I9100_WLAN_BUSY);m.ns+=500000001ULL;
 assert(i9100_wlan_service(&ctl)==I9100_WLAN_TIMEOUT&&m.tx_count==count&&bus.pending);close_fault();
 init(1);configure(1);m.ns+=15000000001ULL;assert(i9100_wlan_service(&ctl)==I9100_WLAN_TIMEOUT);close_fault();
 init(1);send_step(0,1);m.ns--;assert(i9100_wlan_service(&ctl)==I9100_WLAN_TIMEOUT);close_fault();
 init(1);send_step(0,1);m.ns+=500000001ULL;assert(ack(0,old_mask,16)==I9100_BCM_TIMEOUT&&bus.fault);
 assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);configure(1);assert(!event(0,0,0,0,ap)&&!event(16,1,0,0,ap)&&!event(46,0,6,0,ap));m.ns=ctl.started+14999999000ULL;
 assert(!i9100_wlan_service(&ctl)&&!ack(0,ap,6)&&!i9100_wlan_service(&ctl));m.ns+=2000U;
 uint8_t late_ssid[36]={0};put32(late_ssid,6);memcpy(late_ssid+4,"S2-lab",6);
 assert(ack(0,late_ssid,36)==I9100_WLAN_TIMEOUT&&ctl.state==I9100_WLAN_FAULT);close_fault();
 init(1);configure(1);m.ns=ctl.started+15000000001ULL;assert(event(0,0,0,0,ap)==I9100_WLAN_TIMEOUT);close_fault();
 init(1);send_step(0,1);uint8_t too_large[17]={0};assert(ack(0,too_large,17)==I9100_BCM_FRAME&&bus.pending&&bus.fault);
 assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);send_step(0,1);assert(reply(999,ctl.request_id,0,0,0,0)==I9100_BCM_FRAME);assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);send_step(0,1);assert(reply(262,ctl.request_id+1U,0,0,0,0)==I9100_BCM_FRAME);assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);configure(1);struct i9100_wlan_event ev;uint8_t p[80];event_bytes(p,2,16,1,0,0,ap);assert(!i9100_wlan_event_decode(p,72,&ev));
 unsigned ec=ctl.event_count;assert(i9100_wlan_event(&ctl,&ev,2)==I9100_WLAN_EVENT&&ctl.event_count==ec&&!ctl.link_seen);
 ev.ifidx=1;assert(i9100_wlan_event(&ctl,&ev,1)==I9100_WLAN_UNSUPPORTED&&!ctl.link_seen);ev.ifidx=0;ev.bsscfgidx=1;
 assert(i9100_wlan_event(&ctl,&ev,1)==I9100_WLAN_UNSUPPORTED);ev.bsscfgidx=0;ev.version=1;assert(i9100_wlan_event(&ctl,&ev,1)==I9100_WLAN_UNSUPPORTED);
 ev.version=2;ev.flags=0x11;assert(i9100_wlan_event(&ctl,&ev,1)==I9100_WLAN_UNSUPPORTED);
 struct i9100_wlan_control copy=ctl;assert(i9100_wlan_service(&copy)==I9100_WLAN_ARGUMENT);
 header(88,1);m.rx[12]=0x20;event_bytes(m.rx+16,2,16,1,0,0,ap);m.rx[28]^=1;struct i9100_bcm_frame f;assert(!consume(&f));
 assert(i9100_wlan_receive(&ctl,&f)==I9100_WLAN_EVENT);close_fault();
 /* EVENT uses the same BDC v2 pull as DATA in the pinned vendor deliver path. */
 init(1);configure(1);header(92,1);m.rx[12]=0x20;m.rx[15]=1;event_bytes(m.rx+20,2,16,1,0,0,ap);
 assert(!consume(&f)&&f.bytes==72U&&f.payload==m.output+20&&!i9100_wlan_receive(&ctl,&f)&&ctl.link_seen);
 init(1);configure(1);header(88,1);m.rx[12]=0x20;m.rx[14]=1;event_bytes(m.rx+16,2,16,1,0,0,ap);
 assert(consume(&f)==I9100_BCM_FRAME&&bus.fault);assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);configure(1);header(84,1);event_bytes(m.rx+12,2,16,1,0,0,ap);
 assert(consume(&f)==I9100_BCM_FRAME&&bus.fault);assert(i9100_wlan_service(&ctl)==I9100_WLAN_IO);close_fault();
 init(1);send_step(0,1);m.rx_fail=1;unsigned n=99;assert(i9100_bcm_f2_receive(&f2,m.output,sizeof(m.output),&n)==I9100_F2_IO&&!n&&card.fault);
 /* Bus may not yet have consumed a malformed packet; worker must stop on F2 fault. */
}
int main(void)
{
 decoder_tests();association_tests();loss_tests();disconnect_tests();failure_tests();
 puts("PASS event endian/v1/v2/length/interface/generation; actual F2+CDC command bytes/capacity/needed length; OPEN/WPA2 no downgrade/PMK wipe; all event orders/ACK not UP/BSSID+SSID verification/TX+RX gates; deauth/rekey/timeout/all command faults; DISASSOC once/mask restore/owner quarantine; synthetic card/FW, no radio");return 0;
}
