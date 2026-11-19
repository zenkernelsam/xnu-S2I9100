/* SPDX-License-Identifier: BSD-2-Clause; original ABI endpoint with real collector. */
#define I9100_SENSOR_HID_TEST_ENTRY sensor_hid_reference_main
#include "i9100_sensor_hid_test.cpp"
#include "i9100_sensor_hid_endpoint.h"
#include "offline-native-sdk/IOKit/IOBufferMemoryDescriptor.h"
#include "offline-native-sdk/mach/message.h"
static unsigned notifications,buffers;static kern_return_t notification_result;
void OSMemoryBarrier(){barriers++;}
IOBufferMemoryDescriptor *IOBufferMemoryDescriptor::withCapacity(size_t size,unsigned direction,bool)
{
 assert(size==8192&&direction==kIODirectionInOut);if(!allocate())return nullptr;
 auto *p=new IOBufferMemoryDescriptor;p->refs=1;p->bytes=size;p->data=calloc(1,size);assert(p->data);buffers++;return p;
}
void IOBufferMemoryDescriptor::release()
{assert(refs);if(!--refs){free(data);assert(buffers&&active_allocations);buffers--;active_allocations--;delete this;}}
extern "C" kern_return_t mach_msg_send_from_kernel_proper(mach_msg_header_t *p,uint32_t size)
{assert(p->msgh_bits==19&&size==24&&p->msgh_size==24&&p->msgh_remote_port==7);notifications++;return notification_result;}
static struct i9100_sensor_hid_endpoint_status endpoint_status(i9100_sensor_hid_endpoint *p)
{struct i9100_sensor_hid_endpoint_status s={};assert(!i9100_sensor_hid_endpoint_status(p,&s));return s;}
static IOReturn endpoint_open(i9100_sensor_hid_endpoint *p,void *client,unsigned selector)
{uint64_t zero=0;IOExternalMethodArguments a;a.scalarInput=&zero;a.scalarInputCount=1;return i9100_sensor_hid_endpoint_method(p,client,selector,&a);}
#ifndef I9100_SENSOR_ENDPOINT_TEST_ENTRY
#define I9100_SENSOR_ENDPOINT_TEST_ENTRY main
#endif
int I9100_SENSOR_ENDPOINT_TEST_ENTRY()
{
 assert(!sensor_hid_reference_main());
 auto *parent=start_parent();auto wp=profile(1);auto hp=hid_profile();
 i9100_sensor_worker *w=nullptr;i9100_sensor_hid *p=nullptr;i9100_sensor_hid_endpoint *endpoint=nullptr;
 assert(!i9100_sensor_worker_create(1,parent,&wp,&w));IOWorkLoop *worker_loop=sensor_loop;
 assert(!i9100_sensor_hid_create(1,w,&hp,&p));IOWorkLoop *hid_loop=sensor_loop;
 unsigned allocations_before=allocations;
 assert(i9100_sensor_hid_endpoint_create(0,p,1,123,&endpoint)==I9100_SH_DISABLED&&!endpoint&&allocations==allocations_before);
 assert(!i9100_sensor_hid_endpoint_create(1,p,1,123,&endpoint));IOWorkLoop *endpoint_loop=sensor_loop;
 i9100_sensor_hid_consumer copied={},other={};
 assert(i9100_sensor_hid_consumer_get(p,1,&other)==I9100_SH_BUSY);
 assert(i9100_sensor_hid_destroy(&p)==I9100_SH_BUSY&&p);
 assert(!i9100_sensor_hid_endpoint_start(endpoint)&&!i9100_sensor_hid_start(p));
 void *client=(void *)0x1234;assert(!i9100_sensor_hid_endpoint_claim(endpoint,client));
 assert(i9100_sensor_hid_endpoint_claim(endpoint,(void *)0x5678)==I9100_SH_BUSY);
 assert(!endpoint_open(endpoint,client,0));
 IOMemoryDescriptor *mapped=nullptr;IOOptionBits flags=99;
 assert(!i9100_sensor_hid_endpoint_map(endpoint,client,0,&flags,&mapped)&&mapped&&flags==0);
 auto *buffer=static_cast<IOBufferMemoryDescriptor *>(mapped);uint32_t *q=(uint32_t *)buffer->data;
 assert(q[0]==4096&&!q[1]&&!q[2]&&buffer->refs==2);
 assert(!i9100_sensor_hid_endpoint_port(endpoint,client,7,0));
 st_data(1,100);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();endpoint_loop->tick();
 assert(q[2]==64&&q[3]==60&&q[9]==13&&notifications==1);
 assert(q[(12+4+40)/4]==123); /* actual registry sender bound before sampling */
 uint64_t scalar[2]={13,0};i9100_sensor_hid_packet packet;IOExternalMethodArguments a;
 a.scalarInput=scalar;a.scalarInputCount=2;a.structureOutput=&packet;a.structureOutputSize=60;
 assert(!i9100_sensor_hid_endpoint_method(endpoint,client,2,&a)&&a.structureOutputSize==60&&hid_word(packet,48)==6553U);
 a.structureOutputSize=59;assert(i9100_sensor_hid_endpoint_method(endpoint,client,2,&a)==kIOReturnNoSpace);
 scalar[0]=20;a.structureOutputSize=60;assert(i9100_sensor_hid_endpoint_method(endpoint,client,2,&a)==kIOReturnNotFound);scalar[0]=13;
 q[1]=q[2];notification_result=MACH_SEND_TIMED_OUT;
 st_data(1,101);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();endpoint_loop->tick();
 assert(endpoint_status(endpoint).forwarded==2&&endpoint_status(endpoint).notify_busy==1&&notifications==2);
 notification_result=0;
 /* Tail is private; a reader scribble stops the endpoint without OOB writes. */
 q[2]=4095;st_data(1,102);ns+=10000000ULL;worker_loop->tick();hid_loop->tick();endpoint_loop->tick();
 assert(endpoint_status(endpoint).closing&&endpoint_status(endpoint).error==I9100_SH_OWNER&&endpoint_status(endpoint).forwarded==2);
 assert(i9100_sensor_hid_endpoint_destroy(&endpoint)==I9100_SH_BUSY&&endpoint);
 assert(!i9100_sensor_hid_endpoint_unclaim(endpoint,client));
 assert(!i9100_sensor_hid_stop(p));
 fail_remove=1;assert(i9100_sensor_hid_endpoint_destroy(&endpoint)==-98&&endpoint);fail_remove=0;
 assert(!i9100_sensor_hid_endpoint_destroy(&endpoint)&&!endpoint&&buffers==1&&buffer->refs==1);
 q[0]=123;buffer->release();assert(!buffers); /* retained mapping survives endpoint destruction */
 finish_hid(&p,&w,parent);
 (void)copied;
 puts("PASS actual original-HID endpoint ABI -> collector/worker/controller: queue60+4, native sender, exclusive client, port/map/copy methods, notification busy, corrupt tail isolation, retained mapping, failed detach/default OFF; framework/IOKit facades");return 0;
}
