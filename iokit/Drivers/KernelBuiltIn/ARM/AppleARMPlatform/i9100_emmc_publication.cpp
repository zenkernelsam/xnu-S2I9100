/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_publication.h"
#include "IOS6I9100EMMCRaw.h"
#include "i9100_emmc_alias.h"
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSUnserialize.h>
#include <machine/machine_routines.h>
static const char personalities[]=
 "<array><dict><key>IOClass</key><string>IOS6I9100EMMCBlockDriver</string>"
 "<key>IOProbeScore</key><integer>6000</integer>"
 "<key>IOProviderClass</key><string>IOS6I9100EMMCRaw</string></dict>"
 "<dict><key>IOClass</key><string>IOS6I9100EMMCStorage</string>"
 "<key>IOProviderClass</key><string>IOS6I9100EMMCMedia</string>"
 "<key>IOMatchCategory</key><string>IOStorage</string>"
 "<key>IOProbeScore</key><integer>6000</integer>"
 "<key>IOPropertyMatch</key><dict><key>Whole</key><true/>"
 "<key>Writable</key><false/></dict>"
 "<key>IOParentMatch</key><dict><key>IOProviderClass</key>"
 "<string>IOS6I9100EMMCBlockDriver</string></dict></dict>"
 "<dict><key>IOClass</key><string>IOMediaBSDClient</string>"
 "<key>IOMatchCategory</key><string>IOMediaBSDClient</string>"
 "<key>IOProbeScore</key><integer>30000</integer>"
 "<key>IOProviderClass</key><string>IOS6I9100EMMCMedia</string>"
 "<key>IOParentMatch</key><dict><key>IOProviderClass</key>"
 "<string>IOS6I9100EMMCRaw</string></dict>"
 "<key>IOResourceMatch</key><string>IOBSD</string></dict></array>";
bool ios6i9100_publish_emmc_storage(int enabled,IOService *platform,i9100_emmc_native *native,IOS6I9100EMMCRaw **out)
{
 if(enabled!=1||!platform||!native||!out||*out||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return false;
 struct i9100_emmc_native_snapshot current;
 if(i9100_emmc_native_validate(native)||i9100_emmc_native_snapshot(native,&current)||current.mode!=I9100_EMMC_N_DATA_RW||current.error||current.sealed||!i9100_emmc_alias_prepare())return false;
 IOS6I9100EMMCRaw *raw=new IOS6I9100EMMCRaw;if(!raw)return false;
 bool attached=false,started=false,ok=raw->init()&&raw->bind(1,native);
 if(ok)attached=raw->attach(platform);
 if(ok&&attached)started=raw->start(platform);ok=ok&&attached&&started;
 if(ok){
  /* One scoped array, no matching while the catalogue is partially added.
   * Failed catalogue additions can retain scoped entries, but no native raw
   * service is registered. Family matching also denies inherited DOS/GPT
   * personalities already installed by the ordinary SD path. */
  OSArray *drivers=OSDynamicCast(OSArray,OSUnserializeXML(personalities));
  ok=drivers&&gIOCatalogue&&gIOCatalogue->addDrivers(drivers,false);
  if(drivers)drivers->release();
 }
 if(!ok){if(started)raw->stop(platform);if(attached)raw->detach(platform);raw->release();return false;}
 *out=raw;raw->registerService();return true;
}
