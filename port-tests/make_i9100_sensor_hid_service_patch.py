#!/usr/bin/env python3
"""Extend the existing HID matching class on opt-in copies; never patch live code."""
import difflib
import hashlib
import json
from pathlib import Path
REPO=Path(__file__).resolve().parents[1]
path='iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoHIDTouch.cpp'
source=REPO/path;raw=source.read_bytes();original=raw.decode().replace('\r\n','\n');text=original
if 'sensorEndpoint' in text:raise RuntimeError('Sensor extension is already present')
def replace(anchor,new):
 global text
 if text.count(anchor)!=1:raise RuntimeError('Ambiguous/missing original HID anchor: '+anchor[:100])
 text=text.replace(anchor,new,1)
replace('#include "i9100_home.h"\n','#include "i9100_home.h"\n#include "i9100_sensor_hid_service.h"\n#include "i9100_sensor_hid_endpoint.h"\n#include <machine/machine_routines.h>\n')
replace(' bool homeDevice,ios6Wire;\n',' bool homeDevice,ios6Wire;\n i9100_sensor_hid_endpoint *sensorEndpoint;uint32_t sensorKind;\n bool sensorProperties();\n')
replace(' void notePublished(void);\n',' void notePublished(void);\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n int sensorConfigure(i9100_sensor_hid *,uint32_t);\n bool sensorStart(IOService *);\n int sensorClose();\n#endif\n')
replace('OSDefineMetaClassAndStructors(IOS7LeoHIDTouchClient,IOUserClient)\n','OSDefineMetaClassAndStructors(IOS7LeoHIDTouchClient,IOUserClient)\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n#include "IOS6I9100SensorHIDService.inc"\n#endif\n')
replace(' homeDevice=getProperty("I9100HomeButton")!=0;',' sensorEndpoint=0;sensorKind=0;\n homeDevice=getProperty("I9100HomeButton")!=0;')
replace(' if(!homeDevice){\n  int enabled=0,control=0,diagnostic=0;',' if(!homeDevice && !getProperty("I9100SensorEndpoint")){\n  int enabled=0,control=0,diagnostic=0;')
replace('bool IOHIDEventService::publishProperties(void)\n{\n','bool IOHIDEventService::publishProperties(void)\n{\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return sensorProperties();\n#endif\n')
replace(' if(!IOService::start(provider))return false;\n lock=IOLockAlloc();queue=new IOS7LeoHIDQueue;',' #if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return sensorStart(provider);\n#endif\n if(!IOService::start(provider))return false;\n lock=IOLockAlloc();queue=new IOS7LeoHIDQueue;')
replace('void IOHIDEventService::free(void)\n{\n','void IOHIDEventService::free(void)\n{\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind){assert(!sensorEndpoint);IOService::free();return;}\n#endif\n')
replace('bool IOHIDEventService::claim(IOS7LeoHIDTouchClient *client)\n{\n','bool IOHIDEventService::claim(IOS7LeoHIDTouchClient *client)\n{\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return i9100_sensor_hid_endpoint_claim(sensorEndpoint,client)==0;\n#endif\n')
replace('void IOHIDEventService::unclaim(IOS7LeoHIDTouchClient *client)\n{\n','void IOHIDEventService::unclaim(IOS7LeoHIDTouchClient *client)\n{\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind){(void)i9100_sensor_hid_endpoint_unclaim(sensorEndpoint,client);return;}\n#endif\n')
replace(' if(type)return kIOReturnUnsupported;\n IOLockLock(lock);',' #if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return i9100_sensor_hid_endpoint_port(sensorEndpoint,client,port,type);\n#endif\n if(type)return kIOReturnUnsupported;\n IOLockLock(lock);')
replace(' if(type || !options || !memory || !queueMemory)return kIOReturnBadArgument;',' #if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return i9100_sensor_hid_endpoint_map(sensorEndpoint,sensorClient,type,options,memory);\n#endif\n if(type || !options || !memory || !queueMemory)return kIOReturnBadArgument;')
# Existing mapQueue/method have no client parameter; forward the exclusive
# identity stored by claim, without changing virtual/MIG signatures.
replace(' i9100_sensor_hid_endpoint *sensorEndpoint;uint32_t sensorKind;',' i9100_sensor_hid_endpoint *sensorEndpoint;uint32_t sensorKind;IOS7LeoHIDTouchClient *sensorClient;IOLock *sensorLock;')
replace(' sensorEndpoint=0;sensorKind=0;',' sensorEndpoint=0;sensorKind=0;sensorClient=0;sensorLock=0;')
replace(' if(sensorKind)return i9100_sensor_hid_endpoint_claim(sensorEndpoint,client)==0;',' if(sensorKind){IOLockLock(sensorLock);bool ok=!sensorClient&&!i9100_sensor_hid_endpoint_claim(sensorEndpoint,client);if(ok)sensorClient=client;IOLockUnlock(sensorLock);return ok;}')
replace(' if(sensorKind){(void)i9100_sensor_hid_endpoint_unclaim(sensorEndpoint,client);return;}',' if(sensorKind){IOLockLock(sensorLock);if(!i9100_sensor_hid_endpoint_unclaim(sensorEndpoint,client))sensorClient=0;IOLockUnlock(sensorLock);return;}')
replace(' if(sensorKind)return i9100_sensor_hid_endpoint_map(sensorEndpoint,sensorClient,type,options,memory);',' if(sensorKind){IOLockLock(sensorLock);IOReturn e=i9100_sensor_hid_endpoint_map(sensorEndpoint,sensorClient,type,options,memory);IOLockUnlock(sensorLock);return e;}')
replace(' if(!args || args->structureInputDescriptor || args->structureOutputDescriptor)return kIOReturnBadArgument;',' #if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n if(sensorKind)return i9100_sensor_hid_endpoint_method(sensorEndpoint,sensorClient,selector,args);\n#endif\n if(!args || args->structureInputDescriptor || args->structureOutputDescriptor)return kIOReturnBadArgument;')
replace(' if(sensorKind)return i9100_sensor_hid_endpoint_method(sensorEndpoint,sensorClient,selector,args);',' if(sensorKind){IOLockLock(sensorLock);IOReturn e=i9100_sensor_hid_endpoint_method(sensorEndpoint,sensorClient,selector,args);IOLockUnlock(sensorLock);return e;}')
replace(' if(sensorKind)return i9100_sensor_hid_endpoint_port(sensorEndpoint,client,port,type);',' if(sensorKind){IOLockLock(sensorLock);IOReturn e=i9100_sensor_hid_endpoint_port(sensorEndpoint,client,port,type);IOLockUnlock(sensorLock);return e;}')
replace(' if(sensorKind){assert(!sensorEndpoint);IOService::free();return;}',' if(sensorKind){assert(!sensorEndpoint&&!sensorClient);if(sensorLock)IOLockFree(sensorLock);IOService::free();return;}')
replace(' bool homeDevice,ios6Wire;',' bool homeDevice,ios6Wire,sensorDevice;')
replace(' sensorEndpoint=0;sensorKind=0;sensorClient=0;sensorLock=0;',' sensorEndpoint=0;sensorKind=0;sensorClient=0;sensorLock=0;sensorDevice=getProperty("I9100SensorEndpoint")!=0;')
replace(' if(!homeDevice && !getProperty("I9100SensorEndpoint")){',' if(!homeDevice && !sensorDevice){')
replace(' if(sensorKind){assert(!sensorEndpoint&&!sensorClient);if(sensorLock)IOLockFree(sensorLock);IOService::free();return;}',' if(sensorDevice||sensorKind){assert(!sensorEndpoint&&!sensorClient);if(sensorLock)IOLockFree(sensorLock);IOService::free();return;}')
if source.read_bytes()!=raw:raise RuntimeError('Live HID changed during patch preparation')
patch=REPO/'port-tests/i9100_sensor_hid_service_integration.patch'
patch.write_text(''.join(difflib.unified_diff(original.splitlines(True),text.splitlines(True),fromfile='a/'+path,tofile='b/'+path)))
manifest=dict(source=path,source_sha256=hashlib.sha256(raw).hexdigest(),patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),applied=False,hardware_enabled=False,
 companion='iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS6I9100SensorHIDService.inc')
(REPO/'port-tests/i9100_sensor_hid_service_integration.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PREPARED (not applied)',patch)
