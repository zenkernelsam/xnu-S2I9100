#!/usr/bin/env python3
"""Prepare paired raw bus/Power lifecycle integration; never change live sources."""
import difflib
import hashlib
import json
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
base="iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/"
names=["i9100_power_bus.c","IOS7I9100Power.cpp","AppleARMPE.cpp"]
original={base+name:(repo/base/name).read_bytes() for name in names}
edited={p:b.decode().replace("\r\n","\n") for p,b in original.items()}
def replace(path,old,new):
    if edited[path].count(old)!=1:raise RuntimeError("Integration anchor changed: "+path)
    edited[path]=edited[path].replace(old,new,1)
bus=base+names[0]
replace(bus,'#include "i9100_power.h"','#include "i9100_power.h"\n#include "i9100_power_shared.h"')
replace(bus," struct bus b={hw,"," struct i9100_power_bus_ticket shared={0};\n if(!i9100_power_bus_enter(fuel,&shared))return 0;\n if(!i9100_power_bus_filter(&shared,slave,reg,data,count,writing)){(void)i9100_power_bus_leave(&shared);return 0;}\n struct bus b={hw,")
replace(bus," hw->leave(hw->cookie);return ok;"," hw->leave(hw->cookie);if(!i9100_power_bus_leave(&shared))ok=0;return ok;")
replace(bus,"int i9100_power_pmic_write(void *hw,unsigned reg,uint8_t value)\n{return transfer(hw,0,0x66,reg,&value,1,1);}",
 "int i9100_power_pmic_write(void *hw,unsigned reg,uint8_t value)\n{return transfer(hw,0,0x66,reg,&value,1,1);}\n"
 "int i9100_power_haptic_config_read(void *hw,uint8_t *value)\n{uint8_t data=0;if(!value||!transfer(hw,0,0x48,2,&data,1,0))return 0;*value=data;return 1;}\n"
 "int i9100_power_haptic_config_write(void *hw,uint8_t value)\n{return transfer(hw,0,0x48,2,&value,1,1);}")
power=base+names[1]
replace(power,'#include "i9100_power.h"','#include "i9100_power.h"\n#include "i9100_power_shared.h"')
replace(power," virtual void free(void);"," virtual void stop(IOService *);\n static IOReturn stopShared(OSObject *,void *,void *,void *,void *);\n virtual void free(void);")
replace(power," sample();return timer->setTimeoutMS(1000)==kIOReturnSuccess;"," sample();if(timer->setTimeoutMS(1000)!=kIOReturnSuccess)return false;\n const i9100_power_haptic_ops haptic={i9100_power_haptic_config_read,i9100_power_haptic_config_write,1};\n return !i9100_power_shared_enabled()||!i9100_power_shared_publish_haptic(this,&hw,&haptic);")
stop='''IOReturn IOS7I9100Power::stopShared(OSObject *owner,void *,void *,void *,void *)
{
 IOS7I9100Power *s=(IOS7I9100Power *)owner;
 if(s->timer){s->timer->disable();s->timer->cancelTimeout();}
 if(s->battery.ready)(void)i9100_power_stop(&s->battery);
 return kIOReturnSuccess;
}
void IOS7I9100Power::stop(IOService *provider)
{
 int revoked=i9100_power_shared_unpublish(this);
 if(revoked&&revoked!=I9100_SHARED_DISABLED)IOLog("I9100 shared power cleanup retained: %d\\n",revoked);
 if(loop)(void)loop->runAction(stopShared,this);else (void)stopShared(this,0,0,0,0);
 IOPMPowerSource::stop(provider);
}
'''
replace(power,"void IOS7I9100Power::free(void)\n",stop+"void IOS7I9100Power::free(void)\n")
replace(power,"void IOS7I9100Power::free(void)\n{","void IOS7I9100Power::free(void)\n{\n (void)i9100_power_shared_unpublish(this);")
pe=base+names[2]
replace(pe,'#include "AppleARMPE.h"','#include "AppleARMPE.h"\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n#include "i9100_power_shared.h"\n#endif')
replace(pe,"    assert(IOService::getPlatform() == this);",'''    assert(IOService::getPlatform() == this);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    unsigned sharedPower=0;
    (void)PE_parse_boot_argn("i9100sharedpower",&sharedPower,sizeof(sharedPower));
    if(sharedPower)(void)i9100_power_shared_prepare(1);
#endif''')
patch="".join("".join(difflib.unified_diff(original[p].decode().replace("\r\n","\n").splitlines(True),edited[p].splitlines(True),fromfile="a/"+p,tofile="b/"+p)) for p in original)
for p,b in original.items():
    if (repo/p).read_bytes()!=b:raise RuntimeError("Live source changed during patch generation: "+p)
destination=repo/"port-tests/i9100_power_shared_integration.patch"
destination.write_text(patch)
manifest=dict(source_sha256={p:hashlib.sha256(b).hexdigest() for p,b in original.items()},patch_sha256=hashlib.sha256(patch.encode()).hexdigest(),applied=False,default_enabled=False,
              objects=["iokit/i9100_power_shared.cpo","iokit/i9100_power_bus.o","iokit/IOS7I9100Power.cpo","iokit/AppleARMPE.cpo"],
              prerequisite="Apply paired patch, rebuild all four objects and full relink before opt-in; common raw transfer covers Mali too")
manifest["haptic_wire"]="Fixed slave48 CONFIG2 + PMIC LDO14; shared filter before GPIO/IRQ-disabled IO"
(repo/"port-tests/i9100_power_shared_integration.json").write_text(json.dumps(manifest,indent=2)+"\n")
print("PREPARED (not applied)",destination)
