/* SPDX-License-Identifier: BSD-2-Clause; actual companion publication path. */
#define I9100_SENSOR_ENDPOINT_TEST_ENTRY sensor_endpoint_reference_main
#include "i9100_sensor_hid_endpoint_test.cpp"
#include "i9100_sensor_hid_service.h"
#include "i9100_sensor_hid_service_model.h"
void IOLockLock(IOLock *p){assert(p&&!p->held);p->held=true;}
static void prepare_service(i9100_peripheral_native **parent,i9100_sensor_worker **w,i9100_sensor_hid **p,unsigned mask)
{
 *parent=start_parent();auto wp=profile(mask);auto hp=hid_profile();
 assert(!i9100_sensor_worker_create(1,*parent,&wp,w));assert(!i9100_sensor_hid_create(1,*w,&hp,p));
 properties_created=property_fail=service_fail_init=service_fail_attach=0;
}
static void inspect_service(IOHIDEventService *service,unsigned kind)
{
 assert(service&&service->registered&&service->sensorKind==kind&&service->registry);
 assert(service->properties["HIDServiceSupport"].boolean&&service->properties["Built-In"].boolean);
 assert(service->properties["PrimaryUsagePage"].number==0xff00U&&service->properties["PrimaryUsage"].number==(kind==1?3U:kind==2?9U:10U));
 auto pair=service->properties["DeviceUsagePairs"].children[0];
 assert(pair.fields["DeviceUsagePage"].number==0xff00U&&pair.fields["DeviceUsage"].number==service->properties["PrimaryUsage"].number);
 assert(service->properties["IOCFPlugInTypes"].fields.size()==4&&!service->properties.count("ReportInterval"));
}
int main()
{
 assert(!sensor_endpoint_reference_main());
 IOService platform;i9100_peripheral_native *parent=nullptr;i9100_sensor_worker *w=nullptr;i9100_sensor_hid *p=nullptr;i9100_sensor_hid_service *session=nullptr;
 prepare_service(&parent,&w,&p,7);unsigned before=allocations;
 assert(i9100_sensor_hid_service_create(0,&platform,p,7,&session)==I9100_SH_DISABLED&&!session&&allocations==before);
 assert(!i9100_sensor_hid_service_create(1,&platform,p,7,&session));uint32_t published=0;
 assert(!i9100_sensor_hid_service_status(session,&published)&&published==7&&hid_status(p).service_owned&&hid_status(p).consumer_mask==7);
 for(unsigned kind=1;kind<=3;kind++)inspect_service(session->services[kind-1],kind);
 assert(!i9100_sensor_hid_start(p));auto *service=session->services[0];void *client=(void *)0x1234;
 assert(!i9100_sensor_hid_endpoint_claim(service->sensorEndpoint,client));service->sensorClient=client;
 assert(i9100_sensor_hid_service_destroy(&session)==I9100_SH_BUSY&&session&&hid_status(p).closed&&hid_status(p).service_owned);
 assert(!i9100_sensor_hid_endpoint_unclaim(service->sensorEndpoint,client));service->sensorClient=nullptr;
 assert(!i9100_sensor_hid_service_destroy(&session)&&!session&&!hid_status(p).service_owned&&!hid_status(p).consumer_mask);
 finish_hid(&p,&w,parent);
 for(unsigned point=1;point<=6;point++){
  prepare_service(&parent,&w,&p,1);unsigned live=active_allocations;fail_alloc=allocations+point;
  assert(i9100_sensor_hid_service_create(1,&platform,p,1,&session)!=0&&!session&&active_allocations<=live);
  fail_alloc=0;finish_hid(&p,&w,parent);
 }
 prepare_service(&parent,&w,&p,1);property_fail=3;
 assert(i9100_sensor_hid_service_create(1,&platform,p,1,&session)!=0&&!session);property_fail=0;finish_hid(&p,&w,parent);
 prepare_service(&parent,&w,&p,1);service_fail_attach=1;
 assert(i9100_sensor_hid_service_create(1,&platform,p,1,&session)!=0&&!session);service_fail_attach=0;finish_hid(&p,&w,parent);
 assert(property_live==1&&!buffers);
 puts("PASS exact optional IOHIDEventService companion -> real endpoint/collector/worker/controller: native registry senders, original usage/plugin properties, all3 publication, retained claimed shutdown, allocation/property/attach faults, default OFF; registry/IOKit facade, no real discovery");return 0;
}
