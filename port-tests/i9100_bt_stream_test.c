/* SPDX-License-Identifier: BSD-2-Clause; execute the actual stream assembly. */
#include "i9100_bt_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct i9100_bt_stream stream;
static int client,other;
static struct {
 uint64_t ns;unsigned commands,acls,bytes,frames,generation;
 int busy,rx_busy,verify_error,partial,reenter;
 uint8_t received[I9100_BT_STREAM_CAP];
} model;
static int verify(void *cookie,uint32_t generation){assert(cookie==&model);return model.verify_error||generation!=1U;}
static uint64_t now(void *cookie){assert(cookie==&model);return model.ns;}
static int command(void *cookie,uint16_t op,const uint8_t *p,unsigned bytes)
{
 assert(cookie==&model&&(op==0x1001U||op==0x0c03U)&&!bytes&&p);if(model.busy)return I9100_BTS_AGAIN;
 if(model.reenter)assert(i9100_bt_stream_close(&stream,&client)==I9100_BTS_BUSY);
 model.commands++;return model.partial?I9100_BTS_IO:0;
}
static int link(void *cookie,uint16_t handle,struct i9100_bt_acl_link *out)
{assert(cookie==&model);if(handle!=1U)return I9100_BTS_OWNER;memset(out,0,sizeof(*out));out->generation=model.generation;out->handle=handle;return 0;}
static int acl(void *cookie,uint16_t handle,uint32_t generation,unsigned boundary,const uint8_t *p,unsigned bytes)
{
 assert(cookie==&model&&handle==1U&&boundary==2U&&p&&bytes);if(generation!=model.generation)return I9100_BTS_OWNER;
 if(model.busy)return I9100_BTS_AGAIN;model.acls++;return model.partial?I9100_BTS_IO:0;
}
static int receive(void *cookie,const uint8_t *p,unsigned bytes)
{assert(cookie==&model&&p&&bytes<=sizeof(model.received));if(model.rx_busy)return I9100_BTS_AGAIN;
 memcpy(model.received+model.bytes,p,bytes);model.bytes+=bytes;model.frames++;return 0;}
static struct i9100_bt_stream_status status(void)
{struct i9100_bt_stream_status s;assert(!i9100_bt_stream_status(&stream,&s));return s;}
static void setup(void)
{memset(&stream,0,sizeof(stream));memset(&model,0,sizeof(model));model.ns=1;model.generation=7;
 struct i9100_bt_stream_io io={&model,verify,now,command,link,acl,receive};
 assert(i9100_bt_stream_init(&stream,0,&io,1,115200)==I9100_BTS_DISABLED&&!stream.self);
 assert(!i9100_bt_stream_init(&stream,1,&io,1,115200));assert(!i9100_bt_stream_open(&stream,&client));}
static void step(void){model.ns++;assert(!i9100_bt_stream_step(&stream,4));}
int main(void)
{
 static const uint8_t cmd[]={1,1,0x10,0};
 static const uint8_t acl_packet[]={2,1,0x20,4,0,1,2,3,4};
 for(unsigned split=1;split<sizeof(cmd);split++){
  setup();assert(!i9100_bt_stream_write(&stream,&client,cmd,split));step();assert(!model.commands);
  assert(!i9100_bt_stream_write(&stream,&client,cmd+split,sizeof(cmd)-split));step();
  assert(model.commands==1U&&status().tx_accepted==4U&&status().tx_submitted==4U&&!status().tx_queued);
  step();assert(model.commands==1U);assert(!i9100_bt_stream_close(&stream,&client));
 }
 for(unsigned split=1;split<sizeof(acl_packet);split++){
  setup();assert(!i9100_bt_stream_write(&stream,&client,acl_packet,split));step();assert(!model.acls);
  assert(!i9100_bt_stream_write(&stream,&client,acl_packet+split,sizeof(acl_packet)-split));step();assert(model.acls==1U);
 }
 setup();assert(i9100_bt_stream_open(&stream,&other)==I9100_BTS_BUSY);
 assert(i9100_bt_stream_write(&stream,&other,cmd,sizeof(cmd))==I9100_BTS_OWNER);
 struct i9100_bt_stream copy=stream;assert(i9100_bt_stream_step(&copy,1)==I9100_BTS_ARGUMENT);
 assert(i9100_bt_stream_write(&stream,&client,stream.rx,4)==I9100_BTS_ARGUMENT);
 uint8_t raw[44],before[44];assert(!i9100_bt_stream_get_attributes(&stream,&client,raw,44));memcpy(before,raw,44);
 raw[36]^=1;assert(i9100_bt_stream_set_attributes(&stream,&client,raw,44)==I9100_BTS_UNSUPPORTED);
 assert(!i9100_bt_stream_get_attributes(&stream,&client,raw,44)&&!memcmp(raw,before,44));
 raw[8]^=0x10;assert(i9100_bt_stream_set_attributes(&stream,&client,raw,44)==I9100_BTS_UNSUPPORTED);
 assert(!i9100_bt_stream_set_attributes(&stream,&client,before,44));
 setup();uint8_t two[8];memcpy(two,cmd,4);memcpy(two+4,cmd,4);model.busy=1;
 assert(!i9100_bt_stream_write(&stream,&client,two,8));step();assert(!model.commands&&status().tx_queued==8);
 model.busy=0;model.reenter=1;step();assert(model.commands==2U&&!status().tx_queued);
 for(unsigned bad=0;bad<3;bad++){
  setup();uint8_t invalid[]={1,3,0x0c,0};if(bad==1){invalid[1]=0x2e;invalid[2]=0xfc;}if(bad==2)invalid[0]=3;
  assert(i9100_bt_stream_write(&stream,&client,invalid,4)==I9100_BTS_UNSUPPORTED&&status().fault&&!model.commands);
  assert(i9100_bt_stream_write(&stream,&client,cmd,4)==I9100_BTS_FAULT);
 }
 setup();uint8_t batch[8];memcpy(batch,cmd,4);memcpy(batch+4,cmd,4);batch[5]=3;batch[6]=0x0c;
 assert(i9100_bt_stream_write(&stream,&client,batch,8)==I9100_BTS_UNSUPPORTED&&!model.commands&&status().tx_accepted==0);
 setup();assert(!i9100_bt_stream_write(&stream,&client,acl_packet,5));model.generation++;
 assert(!i9100_bt_stream_write(&stream,&client,acl_packet+5,4));model.ns++;
 assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_OWNER&&status().fault&&!model.acls);
 setup();model.busy=1;assert(!i9100_bt_stream_write(&stream,&client,cmd,4));step();model.ns+=1000000001ULL;
 assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_TIMEOUT&&!model.commands);
 setup();model.partial=1;assert(!i9100_bt_stream_write(&stream,&client,cmd,4));model.ns++;
 assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_IO&&status().tx_submitted==0&&model.commands==1);
 assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_FAULT&&model.commands==1);
 assert(!i9100_bt_stream_close(&stream,&client)&&status().tx_discarded==4);
 assert(i9100_bt_stream_open(&stream,&other)==I9100_BTS_DISABLED);
 setup();static const uint8_t event[]={0xff,2,0xab,0xcd};struct i9100_bt_frame frame={4,event,4};
 model.rx_busy=1;assert(!i9100_bt_stream_receive(&stream,&frame));step();assert(!model.bytes&&status().rx_queued==5);
 assert(!i9100_bt_stream_receive(&stream,&frame));model.rx_busy=0;step();
 assert(model.bytes==10U&&model.received[0]==4U&&!memcmp(model.received+1,event,4)&&model.received[5]==4U&&status().rx_delivered==10U);
 setup();uint8_t big[4100]={1,0x20,0,0x10};memset(big+4,0xaa,sizeof(big)-4);
 frame=(struct i9100_bt_frame){2,big,4100};model.rx_busy=1;
 assert(!i9100_bt_stream_receive(&stream,&frame));assert(!i9100_bt_stream_receive(&stream,&frame));
 assert(i9100_bt_stream_receive(&stream,&frame)==I9100_BTS_IO&&status().rx_queued==8202U&&status().fault);
 assert(!i9100_bt_stream_close(&stream,&client)&&status().rx_discarded==8202U);
 setup();model.verify_error=1;assert(i9100_bt_stream_write(&stream,&client,cmd,4)==I9100_BTS_OWNER&&!model.commands);
 assert(!i9100_bt_stream_close(&stream,&client));
 setup();model.ns=0;assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_CLOCK);
 setup();unsigned attempts=0;while(!i9100_bt_stream_step(&stream,1))assert(++attempts<4096U);assert(status().error==I9100_BTS_CLOCK);
 setup();uint8_t many[132];for(unsigned n=0;n<33;n++)memcpy(many+4*n,cmd,4);
 assert(i9100_bt_stream_write(&stream,&client,many,sizeof(many))==I9100_BTS_AGAIN&&!status().tx_accepted&&!stream.records);
 setup();stream.status.owned=0;stream.client=0;
 assert(!i9100_bt_stream_allow_host_reset(&stream,1));assert(!i9100_bt_stream_open(&stream,&client));
 uint8_t reset[]={1,3,0x0c,0};assert(!i9100_bt_stream_write(&stream,&client,reset,2));step();assert(!model.commands);
 assert(!i9100_bt_stream_write(&stream,&client,reset+2,2));step();assert(model.commands==1U&&status().paused&&!status().tx_queued);
 assert(i9100_bt_stream_write(&stream,&client,cmd,4)==I9100_BTS_AGAIN);step();assert(model.commands==1U);
 assert(!i9100_bt_stream_pause(&stream,0));
 uint8_t complete[]={0x0e,4,1,3,0x0c,0};frame=(struct i9100_bt_frame){4,complete,6};assert(!i9100_bt_stream_receive(&stream,&frame));step();
 assert(!status().host_reset_pending);assert(!i9100_bt_stream_write(&stream,&client,cmd,4));step();assert(model.commands==2U);
 setup();stream.status.owned=0;stream.client=0;
 assert(!i9100_bt_stream_allow_host_reset(&stream,1));assert(!i9100_bt_stream_open(&stream,&client));
 assert(!i9100_bt_stream_write(&stream,&client,reset,4));step();assert(status().paused&&model.commands==1U);
 model.ns+=1900000001ULL;assert(i9100_bt_stream_step(&stream,1)==I9100_BTS_TIMEOUT&&status().fault&&model.commands==1U);
 setup();stream.status.owned=0;stream.client=0;
 assert(!i9100_bt_stream_allow_host_reset(&stream,1));assert(!i9100_bt_stream_open(&stream,&client));model.busy=1;
 assert(!i9100_bt_stream_write(&stream,&client,reset,4));uint8_t unrelated[]={0xff,1,0xab};frame=(struct i9100_bt_frame){4,unrelated,3};
 assert(!i9100_bt_stream_receive(&stream,&frame));step();assert(status().host_reset_pending&&!model.commands&&model.bytes==4U);
 assert(i9100_bt_stream_write(&stream,&client,cmd,4)==I9100_BTS_AGAIN);
 puts("PASS actual H4 stream:fragmented/coalesced TX,managed ACL generation,atomic malformed rejection,44B termios,no synthetic reply,socket backpressure,bounded queues,partial-write no replay,close/reentry/owner/timeout/clock");
 return 0;
}
