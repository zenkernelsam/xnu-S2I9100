/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_boot_contract.h"
#include <string.h>
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8U*i));}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static int nonzero(const uint8_t *p,unsigned n){unsigned sum=0;for(unsigned i=0;i<n;i++)sum|=p[i];return sum!=0;}
static uint32_t crc(const uint8_t *p,unsigned n)
{uint32_t c=~0U;while(n--){c^=*p++;for(unsigned bit=0;bit<8;bit++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return c^~0U;}
static int valid(const struct i9100_emmc_boot_contract *c)
{
 if(!c||(c->flags&~3U)||c->host.board_data_reviewed>1U||
    (!!(c->flags&I9100_EMMC_BOOT_REVIEWED)!=!!c->host.board_data_reviewed)||
    ((c->flags&3U)==3U)||c->host.ciu_clock_hz<400000U||c->host.ciu_clock_hz>200000000U||
    (c->host.source_mux&~0xf0000U)||(c->host.source_divider&~0xff0fU)||c->host.clksel==~0U||
    c->host.xxti_hz!=24000000U||c->host.xusbxti_hz!=24000000U||
    c->host.gpk0_mux!=0x3133U||(c->host.gpk0_pull&~0xffU)||(c->host.gpk0_drive&~0xffU)||
    c->card.sectors!=30777344ULL||!nonzero((const uint8_t *)c->card.cid,16)||
    !nonzero(c->card.disk_guid,16)||!nonzero(c->card.system_guid,16)||!nonzero(c->card.data_guid,16)||
    !memcmp(c->card.system_guid,c->card.data_guid,16)||
    !c->system.create_date||!c->data.create_date||!nonzero(c->system.finder_volume_id,8)||
    !nonzero(c->data.finder_volume_id,8)||!memcmp(c->system.finder_volume_id,c->data.finder_volume_id,8)||
    !nonzero(c->pair_manifest_sha256,32))return I9100_EMMC_ARGUMENT;
 const struct i9100_emmc_sdr_plan *p=&c->sdr;
 if(c->idmac_enabled>1U||c->idmac_reviewed>1U||(!c->idmac_enabled&&c->idmac_reviewed)||
    (c->idmac_enabled&&c->idmac_reviewed!=c->host.board_data_reviewed))return I9100_EMMC_ARGUMENT;
 if(p->width){
  if(p->board_reviewed!=c->host.board_data_reviewed||p->board_reviewed>1U||(p->width!=4U&&p->width!=8U)||
     p->high_speed>1U||p->vcc_mv!=3300U||p->ciu_hz!=c->host.ciu_clock_hz||memcmp(p->cid,c->card.cid,16)||
     !p->target_hz||p->target_hz>(p->high_speed?52000000U:20000000U)||p->target_hz>p->ciu_hz/2U)
   return I9100_EMMC_ARGUMENT;
 }else{
  const uint8_t *raw=(const uint8_t *)p;for(unsigned i=0;i<sizeof(*p);i++)if(raw[i])return I9100_EMMC_ARGUMENT;
 }
 return 0;
}
int i9100_emmc_boot_encode(const struct i9100_emmc_boot_contract *c,uint8_t out[I9100_EMMC_BOOT_BYTES])
{
 if(!out||valid(c))return I9100_EMMC_ARGUMENT;
 uint8_t b[I9100_EMMC_BOOT_BYTES]={0};memcpy(b,"I9EB",4);put32(b+4,c->idmac_enabled?4U:c->sdr.width?3U:2U);put32(b+8,sizeof(b));put32(b+12,c->flags);
 const uint32_t host[8]={c->host.board_data_reviewed,c->host.ciu_clock_hz,c->host.source_mux,c->host.source_divider,
  c->host.clksel,c->host.gpk0_mux,c->host.gpk0_pull,c->host.gpk0_drive};
 for(unsigned i=0;i<8;i++)put32(b+16+i*4U,host[i]);
 for(unsigned i=0;i<4;i++)put32(b+48+i*4U,c->card.cid[i]);
 put32(b+64,c->card.primary_crc);put32(b+68,c->card.backup_crc);put32(b+72,c->card.entries_crc);
 memcpy(b+76,c->card.disk_guid,16);memcpy(b+92,c->card.system_guid,16);memcpy(b+108,c->card.data_guid,16);
 put64(b+124,c->card.sectors);put32(b+132,c->system.create_date);memcpy(b+136,c->system.finder_volume_id,8);
 put32(b+144,c->data.create_date);memcpy(b+148,c->data.finder_volume_id,8);memcpy(b+156,c->pair_manifest_sha256,32);
 put32(b+188,c->host.xxti_hz);put32(b+192,c->host.xusbxti_hz);
 if(c->sdr.width){
  const uint32_t transport[10]={c->sdr.board_reviewed,c->sdr.width,c->sdr.high_speed,c->sdr.target_hz,
   c->sdr.ciu_hz,c->sdr.vcc_mv,c->sdr.cid[0],c->sdr.cid[1],c->sdr.cid[2],c->sdr.cid[3]};
  for(unsigned i=0;i<10;i++)put32(b+196+i*4U,transport[i]);
 }
 if(c->idmac_enabled){put32(b+236,1U);put32(b+240,c->idmac_reviewed);}
 put32(b+252,crc(b,252));memcpy(out,b,sizeof(b));return 0;
}
int i9100_emmc_boot_decode(const uint8_t *b,unsigned bytes,struct i9100_emmc_boot_contract *out)
{
 if(!b||!out||bytes!=I9100_EMMC_BOOT_BYTES||memcmp(b,"I9EB",4)||(get32(b+4)!=2U&&get32(b+4)!=3U&&get32(b+4)!=4U)||get32(b+8)!=bytes||
    get32(b+252)!=crc(b,252))return I9100_EMMC_ARGUMENT;
 for(unsigned i=get32(b+4)==4U?244U:get32(b+4)==3U?236U:196U;i<252;i++)if(b[i])return I9100_EMMC_ARGUMENT;
 struct i9100_emmc_boot_contract c;memset(&c,0,sizeof(c));c.flags=get32(b+12);
 uint32_t host[8];for(unsigned i=0;i<8;i++)host[i]=get32(b+16+i*4U);
 c.host.board_data_reviewed=host[0];c.host.ciu_clock_hz=host[1];c.host.source_mux=host[2];c.host.source_divider=host[3];
 c.host.clksel=host[4];c.host.gpk0_mux=host[5];c.host.gpk0_pull=host[6];c.host.gpk0_drive=host[7];
 c.host.xxti_hz=get32(b+188);c.host.xusbxti_hz=get32(b+192);
 if(get32(b+4)>=3U){
  c.sdr.board_reviewed=get32(b+196);c.sdr.width=get32(b+200);c.sdr.high_speed=get32(b+204);
  c.sdr.target_hz=get32(b+208);c.sdr.ciu_hz=get32(b+212);c.sdr.vcc_mv=get32(b+216);
  for(unsigned i=0;i<4;i++)c.sdr.cid[i]=get32(b+220+i*4U);
  if(get32(b+4)==3U&&!c.sdr.width)return I9100_EMMC_ARGUMENT;
 }
 if(get32(b+4)==4U){c.idmac_enabled=get32(b+236);c.idmac_reviewed=get32(b+240);if(c.idmac_enabled!=1U)return I9100_EMMC_ARGUMENT;}
 for(unsigned i=0;i<4;i++)c.card.cid[i]=get32(b+48+i*4U);
 c.card.primary_crc=get32(b+64);c.card.backup_crc=get32(b+68);c.card.entries_crc=get32(b+72);
 memcpy(c.card.disk_guid,b+76,16);memcpy(c.card.system_guid,b+92,16);memcpy(c.card.data_guid,b+108,16);
 c.card.sectors=get64(b+124);c.system.create_date=get32(b+132);memcpy(c.system.finder_volume_id,b+136,8);
 c.data.create_date=get32(b+144);memcpy(c.data.finder_volume_id,b+148,8);memcpy(c.pair_manifest_sha256,b+156,32);
 if(valid(&c))return I9100_EMMC_ARGUMENT;*out=c;return 0;
}
int i9100_emmc_boot_match(const struct i9100_emmc_boot_contract *c,const struct i9100_emmc_native_snapshot *s)
{
 if(valid(c)||!s)return I9100_EMMC_ARGUMENT;
 if(s->error||s->writer_poisoned||s->sealed||!s->system_hfs_valid||!s->data_hfs_valid||
    s->identity.sectors!=c->card.sectors||memcmp(s->identity.cid,c->card.cid,16)||
    s->identity.primary_crc!=c->card.primary_crc||s->identity.backup_crc!=c->card.backup_crc||
    s->identity.entries_crc!=c->card.entries_crc||memcmp(s->identity.disk_guid,c->card.disk_guid,16)||
    memcmp(s->identity.system_guid,c->card.system_guid,16)||memcmp(s->identity.data_guid,c->card.data_guid,16))return I9100_EMMC_OWNER;
 if(s->system.start_lba!=344064ULL||s->system.sectors!=3145728ULL||s->data.start_lba!=3489792ULL||
    s->data.sectors!=12582912ULL||s->system_hfs.bytes!=1610612736ULL||s->data_hfs.bytes!=6442450944ULL||
    s->system_hfs.signature!=0x4858U||s->data_hfs.signature!=0x4858U||
    s->system_hfs.allocation_block!=8192U||s->data_hfs.allocation_block!=8192U||
    (s->system_hfs.attributes&0x2000U)||!(s->data_hfs.attributes&0x2000U)||!s->data_hfs.journal_info_block||
    s->data_hfs.journal_info_block>=s->data_hfs.total_blocks||s->system_hfs.create_date!=c->system.create_date||
    s->data_hfs.create_date!=c->data.create_date||memcmp(s->system_hfs.finder_volume_id,c->system.finder_volume_id,8)||
    memcmp(s->data_hfs.finder_volume_id,c->data.finder_volume_id,8))return I9100_LAYOUT_HFS;
 return 0;
}
