/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i9100_modem_sipc4.h"
static struct i9100_sipc4 parser;
static uint8_t packet[4100];
static void feed_test(unsigned format,unsigned aligned)
{
 const uint8_t body[]={0x7f,0x7e,0,1,2,3,4,5,0xff};unsigned used=0;
 assert(!i9100_sipc4_encode(format,10,0,body,sizeof(body),aligned,packet,sizeof(packet),&used));
 for(unsigned split=1;split<used;split++){
  memset(&parser,0,sizeof(parser));assert(!i9100_sipc4_init(&parser,format,40,aligned));
  struct i9100_sipc_frame result;unsigned consumed;
  assert(i9100_sipc4_feed(&parser,packet,used,&consumed,&result)==I9100_SIPC_DISABLED&&!consumed);
  assert(!i9100_sipc4_enable(&parser,1));assert(!i9100_sipc4_feed(&parser,packet,split,&consumed,&result)&&consumed==split);
  assert(i9100_sipc4_feed(&parser,packet+split,used-split,&consumed,&result)==1&&consumed==used-split);
  assert(result.length==sizeof(body)&&!memcmp(result.bytes,body,sizeof(body)));
  if(format!=1)assert(result.channel==10);
 }
}
static void fragments(void)
{
 uint8_t message[15]={15,0,1,2,3,4,5,6,7,8,9,10,11,12,13};unsigned used,consumed;
 memset(&parser,0,sizeof(parser));assert(!i9100_sipc4_init(&parser,I9100_SIPC_FMT,42,1));assert(!i9100_sipc4_enable(&parser,1));
 struct i9100_sipc_frame result;
 assert(!i9100_sipc4_encode(1,0,0x85,message,8,1,packet,sizeof(packet),&used));
 assert(!i9100_sipc4_feed(&parser,packet,used,&consumed,&result)&&parser.fragment_active);
 assert(!i9100_sipc4_encode(1,0,5,message+8,7,1,packet,sizeof(packet),&used));
 assert(i9100_sipc4_feed(&parser,packet,used,&consumed,&result)==1&&!parser.fragment_active);
 assert(result.length==15&&!memcmp(result.bytes,message,15));
 assert(!i9100_sipc4_encode(1,0,0x85,message,8,1,packet,sizeof(packet),&used));
 assert(!i9100_sipc4_feed(&parser,packet,used,&consumed,&result));
 assert(!i9100_sipc4_encode(1,0,0x86,message,8,1,packet,sizeof(packet),&used));
 assert(i9100_sipc4_feed(&parser,packet,used,&consumed,&result)==I9100_SIPC_FRAGMENT&&parser.fault);
}
static void faults(void)
{
 const uint8_t small[]={1,2};unsigned used,consumed;struct i9100_sipc_frame result;
 memset(&parser,0,sizeof(parser));assert(!i9100_sipc4_init(&parser,2,40,0));assert(!i9100_sipc4_enable(&parser,1));
 assert(!i9100_sipc4_encode(2,31,0,small,2,0,packet,sizeof(packet),&used));packet[used-1]=0;
 assert(i9100_sipc4_feed(&parser,packet,used,&consumed,&result)==I9100_SIPC_FRAME);
 memset(&parser,0,sizeof(parser));assert(!i9100_sipc4_init(&parser,2,40,0));assert(!i9100_sipc4_enable(&parser,1));
 uint8_t big[]={0x7f,255,255,255,255,0,0};
 assert(i9100_sipc4_feed(&parser,big,sizeof(big),&consumed,&result)==I9100_SIPC_OVERFLOW&&parser.fault);
 assert(i9100_sipc4_encode(2,32,0,small,2,0,packet,sizeof(packet),&used)==I9100_SIPC_ARGUMENT&&!used);
 assert(i9100_sipc4_encode(1,0,0,small,4094,0,packet,sizeof(packet),&used)==I9100_SIPC_OVERFLOW&&!used);
}
static void usb_profile(void)
{
 uint8_t device[18]={18,1,0,2,0,0,0,64,0x19,0x15,0x20,0,0,1,0,0,0,1};
 uint8_t config[128]={9,2,0,0,6,1,0,0x80,25};unsigned cursor=9;
 for(unsigned role=0;role<3;role++){
  uint8_t ctl[]={9,4,(uint8_t)(role*2),0,0,2,2,1,0};
  uint8_t uni[]={5,0x24,6,(uint8_t)(role*2),(uint8_t)(role*2+1)};
  uint8_t data[]={9,4,(uint8_t)(role*2+1),0,2,0x0a,0,0,0};
  uint8_t in[]={7,5,(uint8_t)(0x81+role),2,0,2,0};uint8_t out[]={7,5,(uint8_t)(1+role),2,0,2,0};
  memcpy(config+cursor,ctl,9);cursor+=9;memcpy(config+cursor,uni,5);cursor+=5;
  memcpy(config+cursor,data,9);cursor+=9;memcpy(config+cursor,in,7);cursor+=7;memcpy(config+cursor,out,7);cursor+=7;
 }
 config[2]=(uint8_t)cursor;config[3]=(uint8_t)(cursor>>8);struct i9100_xmm_profile profile;
 assert(!i9100_xmm6260_profile(device,18,config,cursor,&profile)&&profile.vid==0x1519&&profile.pipes[2].max_packet==512);
 assert(profile.pipes[0].data_interface==1&&profile.pipes[1].data_interface==3&&profile.pipes[2].data_interface==5);
 device[8]=0x8b;device[9]=0x05;device[10]=0x41;
 assert(i9100_xmm6260_profile(device,18,config,cursor,&profile)==I9100_SIPC_IDENTITY);
 device[8]=0x19;device[9]=0x15;device[10]=0x20;config[9+9+4]=7;
 assert(i9100_xmm6260_profile(device,18,config,cursor,&profile)==I9100_SIPC_FRAME);
 config[9+9+4]=1;config[9+9+5+4]=0;
 assert(i9100_xmm6260_profile(device,18,config,cursor,&profile)==I9100_SIPC_FRAME);
}
int main(void)
{for(unsigned format=1;format<=3;format++)for(unsigned align=0;align<2;align++)feed_test(format,align);fragments();faults();usb_profile();puts("PASS SIPC4 marker-in-payload/all fragments/padding/FMT reassembly/length caps; XMM6260 CDC-union/role/endpoint count and boot-device rejection");return 0;}
