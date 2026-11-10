#include <cstdio>
#include <cassert>
#include <cstring>
#include <stdint.h>
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoTouchWire.h"
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_home.h"
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS6I9100HIDWire.h"
#ifndef I9100_TEST_QUEUE_HEADER
#define I9100_TEST_QUEUE_HEADER "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoTouchQueueProtocol.h"
#endif
#include I9100_TEST_QUEUE_HEADER
static void barrier(void *) {}
static void copy(void *,void *out,const void *in,uint32_t size) {std::memcpy(out,in,size);}

int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 I9100HomeState oldClock,newClock;i9100_home_reset(&oldClock);i9100_home_reset(&newClock);
 /* rtclock.c scales the 24MHz hardware to ns for mach_absolute_time.
  * Exercise the production API's ns inputs, not the raw hardware ticks. */
 const unsigned buttons[]={0,1,1,0,0};const uint64_t ticks[]={1000000000,1020000000,1040000000,1240000000,1260000000};
 for(unsigned n=0;n<5;n++){
  i9100_home_sample(&oldClock,buttons[n],ticks[n]);
  i9100_home_sample_period(&newClock,buttons[n],ticks[n],20000000);
 }
 assert(oldClock.count==2 && newClock.count==2 && newClock.stable==0);
 I9100HomePacket home;IOS6HomePacket home6;LeoHIDPacket touch;IOS6TouchPacket touch6;
 if(!i9100_home_packet(1,123456789,0x100000114ULL,&home) || !ios6_home_wire(&home,&home6) ||
  !leo_touch_make_hid_packet(240,400,1,false,123456789,0x100000115ULL,&touch) ||
  !ios6_touch_wire(&touch,&touch6))return 3;
 const void *data[]={&home,&home6,&touch6};const unsigned size[]={sizeof(home),sizeof(home6),sizeof(touch6)};
 const char *suffix[]={"home7","home6","touch6"};
 for(unsigned n=0;n<3;n++){
  uint32_t head=0,tail=0,flags=0,queue[1024]={};
  LeoTouchQueueHooks hooks={nullptr,barrier,copy};
  if(!leo_touch_queue_enqueue(&head,&tail,4096,(unsigned char *)queue,data[n],size[n],&hooks,&flags))return 6;
  assert(queue[0]==size[n] && tail==size[n]+4 && head==0);
  assert(std::memcmp((unsigned char *)queue+4,data[n],size[n])==0);
  char path[1024];std::snprintf(path,sizeof(path),"%s-%s.bin",argv[1],suffix[n]);
  FILE *file=std::fopen(path,"wb");if(!file)return 4;
  bool ok=std::fwrite((unsigned char *)queue+4,1,size[n],file)==size[n];std::fclose(file);if(!ok)return 5;
 }
 return 0;
}
