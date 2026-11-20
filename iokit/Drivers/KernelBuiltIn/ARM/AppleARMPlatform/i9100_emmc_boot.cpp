/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_boot.h"
#include "i9100_emmc_publication.h"
#include "IOS6I9100EMMCRaw.h"
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <string.h>
enum {BOOT_NEW=1,BOOT_HOST=2,BOOT_VERIFIED=3,BOOT_PUBLISHED=4,BOOT_FAILED=5};
struct i9100_emmc_boot_owner {
 uintptr_t self;IOService *platform;i9100_peripheral_native *parent;
 i9100_emmc_native *native;IOS6I9100EMMCRaw *raw;
 i9100_emmc_boot_contract contract;uint32_t phase;int error,cleanup_error;
};
static volatile uint32_t boot_once;
static i9100_emmc_boot_owner *retained_boot;
extern "C" {volatile uint32_t i9100_emmc_boot_diag[8]={1U};}
static int cleanup(i9100_emmc_boot_owner *o)
{
 if(o->raw)return I9100_EMMC_BUSY;
 if(o->native){int e=i9100_emmc_native_close(&o->native);if(e)return e;}
 if(o->parent){int e=i9100_peripheral_native_destroy(&o->parent);if(e)return e;}
 if(o->platform){o->platform->release();o->platform=0;}return 0;
}
int i9100_emmc_boot_open(int enabled,IOService *platform,const uint8_t *wire,unsigned bytes,i9100_emmc_boot_owner **out)
{
 if(!out||*out||(enabled!=0&&enabled!=1))return I9100_EMMC_ARGUMENT;
 if(!enabled)return I9100_EMMC_UNSUPPORTED;
 if(!platform||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return I9100_EMMC_ARGUMENT;
 i9100_emmc_boot_contract c;int e=i9100_emmc_boot_decode(wire,bytes,&c);if(e)return e;
 if(c.flags!=I9100_EMMC_BOOT_REVIEWED||c.host.board_data_reviewed!=1)return I9100_EMMC_OWNER;
 i9100_emmc_boot_owner *o=(i9100_emmc_boot_owner *)IOMalloc(sizeof(*o));if(!o)return I9100_EMMC_IO;
 memset(o,0,sizeof(*o));o->self=(uintptr_t)o;o->contract=c;o->phase=BOOT_NEW;o->platform=platform;
 platform->retain();*out=o;
 e=i9100_peripheral_native_create(1,&o->parent);if(e)goto fail;
 e=i9100_emmc_native_open(1,o->parent,&o->contract.host,I9100_EMMC_N_DATA_RW,&o->contract.card,&o->native);
 if(e)goto fail;o->phase=BOOT_HOST;
 {
  struct i9100_emmc_native_snapshot observed;
  e=i9100_emmc_native_snapshot(o->native,&observed);if(!e)e=i9100_emmc_boot_match(&o->contract,&observed);
 }
 if(e)goto fail;o->phase=BOOT_VERIFIED;
 if(!ios6i9100_publish_emmc_storage(1,platform,o->native,&o->raw)){e=I9100_EMMC_IO;goto fail;}
 o->phase=BOOT_PUBLISHED;return 0;
fail:
 o->phase=BOOT_FAILED;o->error=e;o->cleanup_error=cleanup(o);
 if(!o->cleanup_error){IOFree(o,sizeof(*o));*out=0;}return e;
}
int i9100_emmc_boot_close(i9100_emmc_boot_owner **owner)
{
 if(!owner||!*owner||(*owner)->self!=(uintptr_t)*owner||ml_at_interrupt_context()||!ml_get_interrupts_enabled())return I9100_EMMC_ARGUMENT;
 i9100_emmc_boot_owner *o=*owner;int e=cleanup(o);o->cleanup_error=e;
 if(!e){IOFree(o,sizeof(*o));*owner=0;}return e;
}
int i9100_emmc_boot_snapshot(i9100_emmc_boot_owner *o,struct i9100_emmc_native_snapshot *out)
{return !o||o->self!=(uintptr_t)o||!o->native||!out?I9100_EMMC_ARGUMENT:i9100_emmc_native_snapshot(o->native,out);}
bool i9100_emmc_root_selected()
{
 char root[128]={0};if(!PE_parse_boot_argn("rd",root,sizeof(root)))(void)PE_parse_boot_argn("rootdev",root,sizeof(root));
 return !strcmp(root[0]=='*'?root+1:root,"i9100-system");
}
void ios6i9100_register_emmc_boot(IOService *platform)
{
 if(!i9100_emmc_root_selected())return;
 if(!__sync_bool_compare_and_swap(&boot_once,0U,1U))return;
 unsigned enabled=0;(void)PE_parse_boot_argn("i9100emmcdata",&enabled,sizeof(enabled));
 i9100_emmc_boot_diag[1]=BOOT_NEW;i9100_emmc_boot_diag[2]=enabled;
 if(enabled!=1||!platform){i9100_emmc_boot_diag[1]=BOOT_FAILED;i9100_emmc_boot_diag[3]=(uint32_t)I9100_EMMC_ARGUMENT;
  IOLog("i9100 EMMCBOOT missing explicit data gate/platform; root remains maintenance\n");return;}
 IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 OSObject *prop=chosen?chosen->copyProperty(I9100_EMMC_BOOT_PROPERTY):0;
 OSData *data=OSDynamicCast(OSData,prop);uint8_t contract[I9100_EMMC_BOOT_BYTES];int e=I9100_EMMC_ARGUMENT;
 if(data&&data->getLength()==sizeof(contract)){
  memcpy(contract,data->getBytesNoCopy(),sizeof(contract));
  e=i9100_emmc_boot_open(1,platform,contract,sizeof(contract),&retained_boot);
 }
 if(prop)prop->release();if(chosen)chosen->release();
 i9100_emmc_boot_diag[1]=e?BOOT_FAILED:BOOT_PUBLISHED;i9100_emmc_boot_diag[3]=(uint32_t)e;
 i9100_emmc_boot_diag[4]=retained_boot?retained_boot->phase:0;
 i9100_emmc_boot_diag[5]=retained_boot?(uint32_t)retained_boot->cleanup_error:0;
 IOLog("i9100 EMMCBOOT phase=%u error=%d cleanup=%d current-card+HFS roles checked; no SD fallback\n",
  i9100_emmc_boot_diag[1],e,(int)i9100_emmc_boot_diag[5]);
}
