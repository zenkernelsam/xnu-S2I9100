/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_BOOT_TEST_HOOK 1
#define I9100_EMMC_REAL_PARENT_MODEL 1
#define I9100_EMMC_STORAGE_MAIN storage_fixture_test_main
class IOService;static void i9100_boot_model_registered(IOService *);
#include "i9100_emmc_storage_test.cpp"
#include "i9100_emmc_boot.h"
#include <IOKit/IOCatalogue.h>
#include <libkern/c++/OSUnserialize.h>
extern "C" void fixture_volume_ids(uint32_t,uint32_t,const uint8_t[8],const uint8_t[8]);
extern "C" void fixture_export_table_evidence(const char *);
static IOCatalogue catalogue;IOCatalogue *gIOCatalogue=&catalogue;
static unsigned parser_calls,catalogue_calls,parser_fail,catalogue_fail,raw_registered;
IOSimpleLock *IOSimpleLockAlloc(){auto *l=(IOSimpleLock *)IOMalloc(sizeof(IOSimpleLock));return l;}
void IOSimpleLockFree(IOSimpleLock *l){assert(!l->held);IOFree(l,sizeof(*l));}
IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *l){assert(!l->held);unsigned old=m.interrupts;m.interrupts=false;l->held=true;return old;}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *l,IOInterruptState old){assert(l->held);l->held=false;m.interrupts=old;}
static IOService chosen;
static char root_arg[128]="disk0s1";static unsigned boot_enabled=0;
extern "C" int PE_parse_boot_argn(const char *key,void *out,int length)
{if(!strcmp(key,"rd")){assert((size_t)length>strlen(root_arg));strcpy((char *)out,root_arg);return 1;}
 if(!strcmp(key,"i9100emmcdata")){assert(length==4);memcpy(out,&boot_enabled,4);return 1;}return 0;}
IOService *IOService::fromPath(const char *path,const void *){assert(!strcmp(path,"/chosen"));chosen.retain();return &chosen;}
OSObject *OSUnserializeXML(const char *xml)
{if(++parser_calls==parser_fail)return nullptr;assert(xml&&strstr(xml,"<array>"));auto *a=new OSArray;if(a)a->xml=xml;return a;}
bool IOCatalogue::addDrivers(OSArray *a)
{assert(a&&a->xml.find("IOFDiskPartitionScheme")==std::string::npos);
 catalogue_calls++;return catalogue_calls!=catalogue_fail;}
static void i9100_boot_model_registered(IOService *service)
{if(dynamic_cast<IOS6I9100EMMCRaw *>(service)){raw_registered++;assert(catalogue_calls==2);}}
static void boot_setup(const char *path,i9100_emmc_boot_contract *c)
{
 setup(path);parser_calls=catalogue_calls=parser_fail=catalogue_fail=raw_registered=0;memset(c,0,sizeof(*c));
 c->flags=I9100_EMMC_BOOT_REVIEWED;c->host=profile;fixture_identity(&c->card);
 c->system.create_date=3445669832U;c->data.create_date=3877958648U;
 const uint8_t sys[8]={0x7d,0xab,0x69,0xd7,0x69,0x06,0xfd,0xf6},dat[8]={0x21,0x76,0x16,0x88,0x95,0x45,0xcc,0xfd};
 memcpy(c->system.finder_volume_id,sys,8);memcpy(c->data.finder_volume_id,dat,8);
 for(unsigned i=0;i<32;i++)c->pair_manifest_sha256[i]=(uint8_t)(i+1U);
 fixture_volume_ids(c->system.create_date,c->data.create_date,sys,dat);
}
static void boot_quiet()
{assert(!m.contexts&&!m.locks&&!m.maps&&!m.descriptors&&!objects);fixture_finish();}
int main()
{
 alarm(20);m.interrupts=true;assert(i9100_emmc_alias_prepare());
 static std::mutex *retained_gate=host_locks.begin()->second;assert(retained_gate);m.locks=0;m.count=0;
 const char *source=getenv("I9100_EMMC_HISTORICAL_SOURCE");assert(source);i9100_emmc_boot_contract c,decoded;
 i9100_emmc_boot_owner *owner=nullptr;IOService platform;uint8_t wire[256],again[256];
 boot_setup(source,&c);assert(!i9100_emmc_boot_encode(&c,wire)&&!i9100_emmc_boot_decode(wire,256,&decoded));
 if(const char *at=getenv("I9100_EMMC_BOOT_MODEL_RECORD")){
  FILE *file=fopen(at,"wb");assert(file&&fwrite(wire,1,256,file)==256&&!fclose(file));
 }
 if(const char *folder=getenv("I9100_EMMC_BOOT_MODEL_EVIDENCE"))fixture_export_table_evidence(folder);
 assert(!i9100_emmc_boot_encode(&decoded,again)&&!memcmp(wire,again,256));
 memset(&decoded,0x55,sizeof(decoded));auto saved=decoded;wire[27]^=1;
 assert(i9100_emmc_boot_decode(wire,256,&decoded)==I9100_EMMC_ARGUMENT&&!memcmp(&decoded,&saved,sizeof(decoded)));wire[27]^=1;
 assert(i9100_emmc_boot_open(0,&platform,wire,256,&owner)==I9100_EMMC_UNSUPPORTED&&!owner&&!m.count);
 c.flags=I9100_EMMC_BOOT_MODEL;c.host.board_data_reviewed=0;assert(!i9100_emmc_boot_encode(&c,wire));
 assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_OWNER&&!m.count&&!owner);boot_quiet();
 /* Every wrong identity is rejected before any IOMedia registration. */
 for(unsigned fault=0;fault<4;fault++){
  boot_setup(source,&c);if(fault==0)c.card.cid[0]^=1;else if(fault==1)c.system.create_date++;else if(fault==2)c.data.finder_volume_id[0]^=1;else c.host.source_divider^=1;
  assert(!i9100_emmc_boot_encode(&c,wire));int e=i9100_emmc_boot_open(1,&platform,wire,256,&owner);
  assert(e&&!owner&&!raw_registered&&!fixture_sector_writes());boot_quiet();
 }
 /* Caller output remains empty when constructor allocation fails; no host
  * slot or GPIO map is consumed. Partial native IO failures are separately
  * covered in the real native fixture, without a fabricated clean reprobe. */
 boot_setup(source,&c);m.fail_at=1;assert(!i9100_emmc_boot_encode(&c,wire));
 assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_IO&&!owner);boot_quiet();
 // Default selected-root query is read-only and allocates no native maps.
 strcpy(root_arg,"disk0s1");assert(!i9100_emmc_root_selected());
 strcpy(root_arg,"*i9100-system");assert(i9100_emmc_root_selected());strcpy(root_arg,"disk0s1");
 for(unsigned fault=1;fault<=2;fault++){
  boot_setup(source,&c);parser_fail=fault;assert(!i9100_emmc_boot_encode(&c,wire));
  assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_IO&&!owner&&!raw_registered);boot_quiet();
 }
 boot_setup(source,&c);catalogue_fail=2;assert(!i9100_emmc_boot_encode(&c,wire));
 assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_IO&&!owner&&!raw_registered);boot_quiet();
 /* Actual native/card/table and actual publication reach RAW only after
  * exact HFS IDs. Ordinary block-driver matching remains a registry facade. */
 boot_setup(source,&c);assert(!i9100_emmc_boot_encode(&c,wire));
 assert(!i9100_emmc_boot_open(1,&platform,wire,256,&owner)&&owner&&raw_registered==1&&catalogue_calls==2);
 struct i9100_emmc_native_snapshot state;assert(!i9100_emmc_boot_snapshot(owner,&state)&&!i9100_emmc_boot_match(&c,&state));
 assert(i9100_emmc_boot_close(&owner)==I9100_EMMC_BUSY&&owner&&m.maps==15);
 /* Preserve the boot-lifetime owner just as production. Never invent a
  * successful media termination to make its cleanup appear complete. */
 static i9100_emmc_boot_owner *volatile retained;retained=owner;assert(retained);
 static std::mutex *retained_mutexes[24];unsigned index=0;for(auto &entry:host_locks){assert(index<24);retained_mutexes[index++]=entry.second;}
 assert(index>=3&&retained_mutexes[0]);fixture_finish();
 puts("PASS actual boot contract/native/MMC/GPT/PIT/HFS-ID/publication: unreviewed/model/CRC/CID/clock/volume drift denied, cleanup/no re-probe, no writes; IOKit allocator/MMIO/card/catalogue facade, no actual registry mount or hardware");return 0;
}
