/* SPDX-License-Identifier: BSD-2-Clause; property/registry fixture for exact inc. */
#include "offline-native-sdk/IOKit/IOService.h"
#include "offline-native-sdk/IOKit/IOUserClient.h"
#include "offline-native-sdk/IOKit/i9100_model_events.h"
#include "i9100_sensor_hid_endpoint.h"
#include <map>
#include <string>
#include <vector>
struct SensorProperty {bool boolean=false;uint64_t number=0;std::string text;std::vector<SensorProperty> children;std::map<std::string,SensorProperty> fields;};
static unsigned properties_created,property_fail,property_live,service_fail_init,service_fail_attach;
static uint64_t service_next_id=500;
class SensorPropertyObject : public OSObject {
public:
 SensorProperty value;
 SensorPropertyObject(){property_live++;}~SensorPropertyObject(){assert(property_live);property_live--;}
};
static bool property_allowed(){return ++properties_created!=property_fail;}
class OSNumber : public SensorPropertyObject {
public:static OSNumber *withNumber(uint64_t n,unsigned){if(!property_allowed())return nullptr;auto *p=new OSNumber;if(p)p->value.number=n;return p;}
};
class OSString : public SensorPropertyObject {
public:static OSString *withCString(const char *s){if(!property_allowed())return nullptr;auto *p=new OSString;if(p)p->value.text=s;return p;}
};
class OSArray : public SensorPropertyObject {
public:static OSArray *withCapacity(unsigned){return property_allowed()?new OSArray:nullptr;}
 bool setObject(SensorPropertyObject *p){if(!property_allowed())return false;value.children.push_back(p->value);return true;}
};
class SensorDictionary : public SensorPropertyObject {
public:static SensorDictionary *withCapacity(unsigned){return property_allowed()?new SensorDictionary:nullptr;}
 bool setObject(const char *key,SensorPropertyObject *p){if(!property_allowed())return false;value.fields[key]=p->value;return true;}
};
static SensorPropertyObject model_boolean_true;
static const unsigned kIOServiceSynchronous=2;
/* This facade replaces registry/platform objects only. The companion inc
 * executes verbatim and calls the real collector and endpoint implementation. */
class IOHIDEventService : public IOService {
public:
 using IOService::init;using IOService::terminate;
 i9100_sensor_hid_endpoint *sensorEndpoint=nullptr;uint32_t sensorKind=0;void *sensorClient=nullptr;IOLock *sensorLock=nullptr;
 bool ios6Wire=true;uint64_t registry=0;std::map<std::string,SensorProperty> properties;std::string name;
 bool init(SensorDictionary *d){return !service_fail_init&&d&&d->value.fields.count("I9100SensorEndpoint");}
 bool attach(IOService *p) override{if(service_fail_attach)return false;registry=service_next_id++;return IOService::attach(p);}
 bool setProperty(const char *key,bool b){if(!property_allowed())return false;properties[key].boolean=b;return true;}
 bool setProperty(const char *key,const char *s){if(!property_allowed())return false;properties[key].text=s;return true;}
 bool setProperty(const char *key,uint64_t n,unsigned){if(!property_allowed())return false;properties[key].number=n;return true;}
 bool setProperty(const char *key,SensorPropertyObject *p){if(!property_allowed())return false;properties[key]=p->value;return true;}
 void removeProperty(const char *key){properties.erase(key);}void setName(const char *s){name=s;}
 uint64_t getRegistryEntryID(){return registry;}
 bool terminate(unsigned flags){assert(flags==kIOServiceSynchronous);return IOService::terminate();}
 bool sensorProperties();int sensorConfigure(i9100_sensor_hid *,uint32_t);bool sensorStart(IOService *);int sensorClose();
 void free() override{assert(!sensorEndpoint&&!sensorClient);if(sensorLock)IOLockFree(sensorLock);IOService::free();}
};
#define OSDictionary SensorDictionary
#define kOSBooleanTrue (&model_boolean_true)
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS6I9100SensorHIDService.inc"
#undef OSDictionary
#undef kOSBooleanTrue
