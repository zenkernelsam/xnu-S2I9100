/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_publication.h"
#include "IOS6I9100EMMCRaw.h"
#include "i9100_emmc_alias.h"
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSUnserialize.h>
#include <machine/machine_routines.h>
#include "IOS7LabStoragePersonalities.h"
bool ios6i9100_publish_emmc_storage(int enabled,IOService *platform,i9100_emmc_native *native,IOS6I9100EMMCRaw **out)
{
 if(enabled!=1||!platform||!native||!out||*out||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return false;
 struct i9100_emmc_native_snapshot current;
 if(i9100_emmc_native_validate(native)||i9100_emmc_native_snapshot(native,&current)||current.mode!=I9100_EMMC_N_DATA_RW||current.error||current.sealed||!i9100_emmc_alias_prepare())return false;
 IOS6I9100EMMCRaw *raw=new IOS6I9100EMMCRaw;if(!raw)return false;
 bool attached=false,ok=raw->init()&&raw->bind(1,native);
 if(ok)attached=raw->attach(platform);
 if(ok&&attached)ok=raw->start(platform);else ok=false;
 static const char personalities[]= "<array><dict>"
  "<key>IOClass</key><string>IOS6I9100EMMCStorage</string>"
  "<key>IOProviderClass</key><string>IOMedia</string>"
  "<key>IOMatchCategory</key><string>IOStorage</string>"
  "<key>IOProbeScore</key><integer>6000</integer>"
  "<key>IOPropertyMatch</key><dict><key>Whole</key><true/></dict>"
  "</dict></array>";
 if(ok){
  OSArray *standard=OSDynamicCast(OSArray,OSUnserializeXML(ios7labStoragePersonalities));
  OSArray *native_drivers=OSDynamicCast(OSArray,OSUnserializeXML(personalities));
  ok=standard&&native_drivers&&gIOCatalogue->addDrivers(standard)&&gIOCatalogue->addDrivers(native_drivers);
  if(standard)standard->release();if(native_drivers)native_drivers->release();
 }
 if(!ok){if(attached)raw->detach(platform);raw->release();return false;}
 *out=raw;raw->registerService();return true;
}
