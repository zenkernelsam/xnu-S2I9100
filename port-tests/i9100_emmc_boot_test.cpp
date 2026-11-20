/* SPDX-License-Identifier: BSD-2-Clause */
#define I9100_EMMC_BOOT_TEST_HOOK 1
#define I9100_EMMC_REAL_PARENT_MODEL 1
#define I9100_EMMC_STORAGE_MAIN storage_fixture_test_main
class IOService;static void i9100_boot_model_registered(IOService *);
#if defined(I9100_EMMC_SDR_BOOT_MODEL) || defined(I9100_EMMC_IDMAC_BOOT_MODEL)
static unsigned sdr_gpio_drop_offset=~0U,sdr_gpio_writes;
#define I9100_EMMC_SDR_NATIVE_MODEL 1
#endif
#include "i9100_emmc_storage_test.cpp"
#include "i9100_emmc_boot.h"
#include <IOKit/IOCatalogue.h>
#include <libkern/c++/OSUnserialize.h>
extern "C" void fixture_volume_ids(uint32_t,uint32_t,const uint8_t[8],const uint8_t[8]);
extern "C" void fixture_export_table_evidence(const char *);
static IOCatalogue catalogue;IOCatalogue *gIOCatalogue=&catalogue;
static unsigned parser_calls,catalogue_calls,parser_fail,catalogue_fail,raw_registered;
#if defined(I9100_EMMC_SDR_BOOT_MODEL) || defined(I9100_EMMC_IDMAC_SDR_BOOT_MODEL)
extern "C" void fixture_enable_sdr(unsigned);
static void boot_setup(const char *,i9100_emmc_boot_contract *);
static void boot_sdr(const char *path,i9100_emmc_boot_contract *c)
{
 boot_setup(path,c);fixture_enable_sdr(0);c->sdr.board_reviewed=1;c->sdr.width=8;
 c->sdr.high_speed=1;c->sdr.target_hz=50000000;c->sdr.ciu_hz=100000000;c->sdr.vcc_mv=3300;memcpy(c->sdr.cid,c->card.cid,16);
}
#endif
#if defined(I9100_EMMC_IDMAC_BOOT_MODEL)
extern "C" void fixture_dma_buffers(uint8_t *,uint32_t,uint8_t *,uint32_t,unsigned);
#endif
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
bool IOCatalogue::addDrivers(OSArray *a,bool matching)
{assert(a&&a->xml.find("IOFDiskPartitionScheme")==std::string::npos);
 assert(!matching);
 catalogue_calls++;return catalogue_calls!=catalogue_fail;}
static void i9100_boot_model_registered(IOService *service)
{if(dynamic_cast<IOS6I9100EMMCRaw *>(service)){raw_registered++;assert(catalogue_calls==1);}}
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
static uint32_t legacy_record_crc(const uint8_t *bytes,unsigned length)
{
 uint32_t value=UINT32_MAX;
 for(unsigned i=0;i<length;i++){
  value^=bytes[i];for(unsigned bit=0;bit<8;bit++)value=(value>>1)^((0U-(value&1U))&0xedb88320U);
 }
 return value^UINT32_MAX;
}
#ifndef I9100_EMMC_BOOT_MAIN
#define I9100_EMMC_BOOT_MAIN main
#endif
int I9100_EMMC_BOOT_MAIN()
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
 uint8_t legacy_wire[256];memcpy(legacy_wire,wire,256);legacy_wire[4]=1;
 memset(legacy_wire+188,0,8);uint32_t legacy_crc=legacy_record_crc(legacy_wire,252);
 for(unsigned k=0;k<4;k++)legacy_wire[252+k]=(uint8_t)(legacy_crc>>(8*k));
 auto unchanged=decoded;
 assert(i9100_emmc_boot_decode(legacy_wire,256,&decoded)==I9100_EMMC_ARGUMENT&&!memcmp(&decoded,&unchanged,sizeof(decoded)));
 auto bad_clock=c;bad_clock.host.xxti_hz=26000000U;
 assert(i9100_emmc_boot_encode(&bad_clock,again)==I9100_EMMC_ARGUMENT&&!m.count);
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
 for(unsigned fault=1;fault<=1;fault++){
  boot_setup(source,&c);parser_fail=fault;assert(!i9100_emmc_boot_encode(&c,wire));
  assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_IO&&!owner&&!raw_registered);boot_quiet();
 }
 boot_setup(source,&c);catalogue_fail=1;assert(!i9100_emmc_boot_encode(&c,wire));
 assert(i9100_emmc_boot_open(1,&platform,wire,256,&owner)==I9100_EMMC_IO&&!owner&&!raw_registered);boot_quiet();
 /* Actual native/card/table and actual publication reach RAW only after
  * exact HFS IDs. Ordinary block-driver matching remains a registry facade. */
#if defined(I9100_EMMC_IDMAC_BOOT_MODEL)
 #if defined(I9100_EMMC_IDMAC_SDR_BOOT_MODEL)
 boot_sdr(source,&c);
 #else
 boot_setup(source,&c);
 #endif
 dma_model_next=0;fixture_dma_buffers(dma_model_memory,0x46000000,dma_model_memory+4096,0x46001000,0);
 c.idmac_enabled=c.idmac_reviewed=1;assert(!i9100_emmc_boot_encode(&c,wire)&&wire[4]==4U);
 assert(!i9100_emmc_boot_decode(wire,256,&decoded)&&decoded.idmac_enabled==1U&&decoded.idmac_reviewed==1U);
 uint8_t bad_idmac[256];memcpy(bad_idmac,wire,256);bad_idmac[240]=0;
 uint32_t bad_crc=legacy_record_crc(bad_idmac,252);for(unsigned k=0;k<4;k++)bad_idmac[252+k]=(uint8_t)(bad_crc>>(8U*k));
 auto idmac_unchanged=decoded;assert(i9100_emmc_boot_decode(bad_idmac,256,&decoded)==I9100_EMMC_ARGUMENT&&!memcmp(&decoded,&idmac_unchanged,sizeof(decoded)));
 memcpy(bad_idmac,wire,256);bad_idmac[244]=1;bad_crc=legacy_record_crc(bad_idmac,252);
 for(unsigned k=0;k<4;k++)bad_idmac[252+k]=(uint8_t)(bad_crc>>(8U*k));
 assert(i9100_emmc_boot_decode(bad_idmac,256,&decoded)==I9100_EMMC_ARGUMENT&&!memcmp(&decoded,&idmac_unchanged,sizeof(decoded)));
 if(const char *at=getenv("I9100_EMMC_BOOT_MODEL_IDMAC_RECORD")){
  FILE *file=fopen(at,"wb");assert(file&&fwrite(wire,1,256,file)==256&&!fclose(file));
 }
#elif defined(I9100_EMMC_SDR_BOOT_MODEL)
 boot_sdr(source,&c);assert(!i9100_emmc_boot_encode(&c,wire)&&wire[4]==3U);
 assert(!i9100_emmc_boot_decode(wire,256,&decoded)&&decoded.sdr.width==8&&decoded.sdr.high_speed==1);
 if(const char *at=getenv("I9100_EMMC_BOOT_MODEL_SDR_RECORD")){
  FILE *file=fopen(at,"wb");assert(file&&fwrite(wire,1,256,file)==256&&!fclose(file));
 }
#else
 boot_setup(source,&c);assert(!i9100_emmc_boot_encode(&c,wire));
#endif
 assert(!i9100_emmc_boot_open(1,&platform,wire,256,&owner)&&owner&&raw_registered==1&&catalogue_calls==1);
 struct i9100_emmc_native_snapshot state;assert(!i9100_emmc_boot_snapshot(owner,&state)&&!i9100_emmc_boot_match(&c,&state));
 assert(i9100_emmc_boot_close(&owner)==I9100_EMMC_BUSY&&owner&&m.maps==15);
#if defined(I9100_EMMC_IDMAC_BOOT_MODEL)
 assert(dma_model_descriptors==2&&dma_model_prepared==2);
#endif
#if defined(I9100_EMMC_SDR_BOOT_MODEL) || defined(I9100_EMMC_IDMAC_SDR_BOOT_MODEL)
 assert(sdr_gpio_writes==6U&&regs[GPIO][0x44/4]==4U);
#endif
 /* Preserve the boot-lifetime owner just as production. Never invent a
  * successful media termination to make its cleanup appear complete. */
 static i9100_emmc_boot_owner *volatile retained;retained=owner;assert(retained);
 static std::mutex *retained_mutexes[24];unsigned index=0;for(auto &entry:host_locks){assert(index<24);retained_mutexes[index++]=entry.second;}
 assert(index>=3&&retained_mutexes[0]);fixture_finish();
 puts("PASS actual boot contract/native/MMC/GPT/PIT/HFS-ID/publication: unreviewed/model/CRC/CID/clock/volume drift denied, cleanup/no re-probe, no writes; IOKit allocator/MMIO/card/catalogue facade, no actual registry mount or hardware");return 0;
}
