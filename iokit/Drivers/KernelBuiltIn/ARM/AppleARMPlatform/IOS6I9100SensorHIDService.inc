/* SPDX-License-Identifier: BSD-2-Clause; optional companion to IOHIDEventService.
 * Included after the existing class declaration, never a second IOHID class. */
struct i9100_sensor_hid_service {
 IOService *platform;IOHIDEventService *services[3];uint32_t attached,published,mask;
 i9100_sensor_hid *collector;
 i9100_sensor_hid_hold hold;
};
bool IOHIDEventService::sensorProperties()
{
 uint32_t type=0,page=0,usage=0;
 if(i9100_sensor_hid_usage(sensorKind,&type,&page,&usage))return false;
 const char *product=sensorKind==1?"GT-I9100 K3DH accelerometer":sensorKind==2?"GT-I9100 K3G gyroscope":"GT-I9100 AK8975 magnetometer";
 setName(sensorKind==1?"IOS6I9100Accelerometer":sensorKind==2?"IOS6I9100Gyroscope":"IOS6I9100Magnetometer");
 if(!setProperty("HIDServiceSupport",true)||!setProperty("Built-In",true)||!setProperty("Transport","I2C")||
    !setProperty("Product",product)||!setProperty("PrimaryUsagePage",page,32)||!setProperty("PrimaryUsage",usage,32)||
    !setProperty("VendorID",0ULL,32)||!setProperty("ProductID",sensorKind+1U,32)||!setProperty("QueueSize",4096,32))return false;
 /* Actual fixed polling/ODR has no per-client rate setter. Do not advertise
  * ReportInterval and promise a control contract we do not implement. */
 removeProperty("ReportInterval");
 OSDictionary *pair=OSDictionary::withCapacity(2),*plugins=OSDictionary::withCapacity(4);
 OSArray *pairs=OSArray::withCapacity(1);OSNumber *p=OSNumber::withNumber(page,32),*u=OSNumber::withNumber(usage,32);
 OSString *path=OSString::withCString("IOHIDFamily.kext/PlugIns/IOHIDLib.plugin");
 bool ok=pair&&plugins&&pairs&&p&&u&&path;
 if(ok)ok=pair->setObject("DeviceUsagePage",p)&&pair->setObject("DeviceUsage",u)&&pairs->setObject(pair)&&setProperty("DeviceUsagePairs",pairs);
 if(ok)ok=plugins->setObject("7DDEECA8-A7B4-11DA-8A0E-0014519758EF",path)&&
  plugins->setObject("FA12FA38-6F1A-11D4-BA0C-0005028F18D5",path)&&
  plugins->setObject("0516B563-B15B-11DA-96EB-0014519758EF",path)&&
  plugins->setObject("40A57A4E-26A0-11D8-9295-000A958A2C78",path)&&setProperty("IOCFPlugInTypes",plugins);
 if(pair)pair->release();if(plugins)plugins->release();if(pairs)pairs->release();if(p)p->release();if(u)u->release();if(path)path->release();return ok;
}
int IOHIDEventService::sensorConfigure(i9100_sensor_hid *collector,uint32_t kind)
{
 if(sensorEndpoint||sensorKind||!collector||kind<1U||kind>3U||!ios6Wire)return I9100_SH_ARGUMENT;
 sensorKind=kind;
 sensorLock=IOLockAlloc();if(!sensorLock)return I9100_SH_FAULT;
 int e=i9100_sensor_hid_endpoint_create(1,collector,kind,getRegistryEntryID(),&sensorEndpoint);
 if(e)return e;return sensorProperties()?0:I9100_SH_FAULT;
}
bool IOHIDEventService::sensorStart(IOService *provider)
{return sensorEndpoint&&IOService::start(provider)&&!i9100_sensor_hid_endpoint_start(sensorEndpoint);}
int IOHIDEventService::sensorClose()
{if(!sensorLock)return sensorEndpoint?I9100_SH_OWNER:0;IOLockLock(sensorLock);
 int e=sensorClient?I9100_SH_BUSY:sensorEndpoint?i9100_sensor_hid_endpoint_destroy(&sensorEndpoint):0;
 IOLockUnlock(sensorLock);return e;}
static int sensor_session_close(i9100_sensor_hid_service *s)
{
 int e=s->hold.owned?i9100_sensor_hid_stop(s->collector):0;if(e)return e;
 for(unsigned n=0;n<3;n++)if(s->services[n]){
  e=s->services[n]->sensorClose();if(e)return e;
  if(s->published&(1U<<n)){if(!s->services[n]->terminate(kIOServiceSynchronous))return I9100_SH_BUSY;s->published&=~(1U<<n);}
  if(s->attached&(1U<<n)){s->services[n]->detach(s->platform);s->attached&=~(1U<<n);}
  s->services[n]->release();s->services[n]=0;
 }
 if(s->hold.owned){e=i9100_sensor_hid_hold_put(&s->hold);if(e)return e;}
 return 0;
}
int i9100_sensor_hid_service_create(int enabled,IOService *platform,i9100_sensor_hid *collector,uint32_t mask,i9100_sensor_hid_service **out)
{
 if(!out)return I9100_SH_ARGUMENT;if(*out)return I9100_SH_BUSY;if(enabled!=1)return I9100_SH_DISABLED;
 if(!platform||!collector||!mask||(mask&~7U)||!ml_get_interrupts_enabled())return I9100_SH_ARGUMENT;
 struct i9100_sensor_hid_status state={};int e=i9100_sensor_hid_status(collector,&state);if(e)return e;
 if(!state.ready||state.running||state.closing||state.closed||state.mask!=mask||state.consumer_mask)return I9100_SH_BUSY;
 auto *s=(i9100_sensor_hid_service *)IOMalloc(sizeof(i9100_sensor_hid_service));if(!s)return I9100_SH_FAULT;
 memset(s,0,sizeof(*s));s->platform=platform;s->collector=collector;s->mask=mask;platform->retain();
 e=i9100_sensor_hid_hold_get(collector,&s->hold);if(e)goto failed;
 for(unsigned n=0;n<3;n++)if(mask&(1U<<n)){
  s->services[n]=new IOHIDEventService;if(!s->services[n]){e=I9100_SH_FAULT;goto failed;}
  OSDictionary *properties=OSDictionary::withCapacity(1);
  if(!properties){e=I9100_SH_FAULT;goto failed;}
  bool initialized=properties->setObject("I9100SensorEndpoint",kOSBooleanTrue)&&s->services[n]->init(properties);properties->release();
  if(!initialized||!s->services[n]->attach(platform)){e=I9100_SH_FAULT;goto failed;}
  s->attached|=1U<<n;e=s->services[n]->sensorConfigure(collector,n+1U);if(e)goto failed;
 }
 for(unsigned n=0;n<3;n++)if(s->services[n]&&!s->services[n]->sensorStart(platform)){e=I9100_SH_FAULT;goto failed;}
 for(unsigned n=0;n<3;n++)if(s->services[n]){s->published|=1U<<n;s->services[n]->registerService();}
 *out=s;return 0;
failed:
 {int closed=sensor_session_close(s);if(closed){*out=s;return e;}platform->release();IOFree(s,sizeof(*s));return e;}
}
int i9100_sensor_hid_service_status(i9100_sensor_hid_service *s,uint32_t *mask)
{if(!s||!mask)return I9100_SH_ARGUMENT;*mask=s->published;return 0;}
int i9100_sensor_hid_service_destroy(i9100_sensor_hid_service **owner)
{if(!owner||!*owner)return I9100_SH_ARGUMENT;auto *s=*owner;int e=sensor_session_close(s);if(e)return e;
 s->platform->release();IOFree(s,sizeof(*s));*owner=0;return 0;}
