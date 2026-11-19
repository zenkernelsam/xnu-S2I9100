/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_sensor_hid_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t word(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(void)
{
 struct i9100_sensor_sample s={123456789ULL,1,1,8,{1000000,-500000,250000}};
 struct i9100_sensor_hid_packet p;
 assert(sizeof(p)==60&&!i9100_sensor_hid_encode(&s,41152263,71,&p));
 assert(word(p.bytes)==41152263&&word(p.bytes+12)==1&&word(p.bytes+16)==44&&word(p.bytes+20)==13);
 assert(word(p.bytes+40)==71&&word(p.bytes+48)==65536&&word(p.bytes+52)==(uint32_t)-32768&&word(p.bytes+56)==16384);
 assert(!i9100_sensor_hid_encode(&s,0x1122334455667788ULL,0x2233445566778899ULL,&p));
 assert(word(p.bytes)==0x55667788U&&word(p.bytes+4)==0x11223344U&&word(p.bytes+40)==0x66778899U&&word(p.bytes+44)==0x22334455U);
 s.kind=2;s.xyz[0]=180000000;s.xyz[1]=-90000000;s.xyz[2]=45000000;
 assert(!i9100_sensor_hid_encode(&s,41152263,72,&p)&&word(p.bytes+20)==20);
 assert(word(p.bytes+48)==205887&&word(p.bytes+52)==(uint32_t)-102943&&word(p.bytes+56)==51471);
 s.kind=3;s.status=1;s.xyz[0]=30000;s.xyz[1]=-60000;s.xyz[2]=90000;
 assert(!i9100_sensor_hid_encode(&s,41152263,73,&p)&&word(p.bytes+20)==21&&word(p.bytes+48)==1966080);
 memset(&p,0xa5,sizeof(p));struct i9100_sensor_hid_packet before=p;s.xyz[0]=2147483647;
 assert(i9100_sensor_hid_encode(&s,1,1,&p)==I9100_SH_RANGE&&!memcmp(&p,&before,sizeof(p)));
 s.xyz[0]=0;s.status=3;assert(i9100_sensor_hid_encode(&s,1,1,&p)==I9100_SH_ARGUMENT);
 s.status=1;assert(i9100_sensor_hid_encode(&s,0,1,&p)==I9100_SH_ARGUMENT);
 assert(i9100_sensor_hid_encode(&s,1,0,&p)==I9100_SH_ARGUMENT);
 assert(i9100_sensor_hid_encode(&s,1,1,(struct i9100_sensor_hid_packet *)&s)==I9100_SH_ARGUMENT);
 uint32_t type,page,usage;for(unsigned k=1;k<=3;k++){
  assert(!i9100_sensor_hid_usage(k,&type,&page,&usage)&&page==0xff00U);
  assert(type==(k==1?13U:k==2?20U:21U)&&usage==(k==1?3U:k==2?9U:10U));
 }
 assert(i9100_sensor_hid_usage(1,&type,&type,&usage)==I9100_SH_ARGUMENT);
 puts("PASS iOS6 sensor wire: exact60/44 bytes, signed fixed16.16 g/radians-per-second/microtesla, explicit absolute time/sender, no partial/range/alias output, original usage pairs");return 0;
}
