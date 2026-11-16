/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_firmware.h"
#include "i9100_bcm_backplane_private.h"
#include "i9100_bcm_chip_private.h"
#include "i9100_bcm_firmware_private.h"
#include <string.h>
static int aliases(uintptr_t,uint32_t,uintptr_t,uint32_t);
static uint32_t align4(uint32_t n){return (n+3U)&~3U;}
static int nonzero(const uint8_t *bytes,unsigned n)
{uint8_t v=0;for(unsigned i=0;i<n;i++)v|=bytes[i];return v!=0;}
static int equal(const uint8_t *a,const uint8_t *b,unsigned n)
{uint8_t v=0;for(unsigned i=0;i<n;i++)v|=a[i]^b[i];return !v;}
static const struct i9100_bcm_core *sdio_core(const struct i9100_bcm_chip *c)
{for(unsigned n=0;n<c->topology.count;n++)if(c->topology.cores[n].id==0x829U)return &c->topology.cores[n];return 0;}
int i9100_bcm_fw_make_layout(uint32_t ram,uint32_t rev,uint32_t fw,uint32_t nv,struct i9100_bcm_fw_layout *out)
{
 if(!out||ram<16384U||ram>I9100_BCM_MAX_RAM||(ram&3U)||fw<8U||fw>I9100_BCM_MAX_RAM||
    nv<3U||nv>I9100_BCM_NVRAM_MAX||rev>255U)return I9100_BCM_FW_ARGUMENT;
 struct i9100_bcm_fw_layout l;memset(&l,0,sizeof(l));
 l.ram_bytes=ram;l.firmware_bytes=fw;l.firmware_padded=align4(fw);l.nvram_bytes=nv;l.nvram_padded=align4(nv);
 if(l.nvram_padded>ram-4U)return I9100_BCM_FW_LAYOUT;
 l.token_address=ram-4U;l.nvram_address=l.token_address-l.nvram_padded;
 /* Pinned dhdsdio_write_vars PR85623 workaround, not a generic guessed pad. */
 if(rev==7U&&(l.nvram_address&0x3cU)==0x3cU&&l.nvram_padded>4U){
  if(l.nvram_address<4U)return I9100_BCM_FW_LAYOUT;l.nvram_address-=4U;l.nvram_padded+=4U;
 }
 if(l.firmware_padded>l.nvram_address)return I9100_BCM_FW_LAYOUT;
 uint32_t words=l.nvram_padded/4U;l.token=((~words&0xffffU)<<16)|words;*out=l;return 0;
}
static int hex(uint8_t ch)
{return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:ch>='A'&&ch<='F'?ch-'A'+10:-1;}
static int valid_mac(const uint8_t mac[6])
{return !(mac[0]&1U)&&nonzero(mac,6U);}
int i9100_bcm_nvram_validate(const uint8_t *bytes,uint32_t n,const uint8_t expected[6])
{
 if(!bytes||!expected||n<3U||n>I9100_BCM_NVRAM_MAX||!valid_mac(expected)||bytes[n-1U]||bytes[n-2U])
  return I9100_BCM_FW_ARGUMENT;
 uint16_t offsets[128],lengths[128];unsigned entries=0,found_mac=0;uint32_t offset=0;
 while(offset<n&&bytes[offset]){
  uint32_t end=offset,key=0;
  while(end<n&&bytes[end]){if(bytes[end]<32U||bytes[end]>126U)return I9100_BCM_FW_LAYOUT;end++;}
  if(end==n||end-offset>512U||entries==128U)return I9100_BCM_FW_LAYOUT;
  while(key<end-offset&&bytes[offset+key]!='='){
   uint8_t ch=bytes[offset+key];
   if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
      (ch>='0'&&ch<='9')||ch=='_'||ch=='.'))return I9100_BCM_FW_LAYOUT;
   key++;
  }
  if(!key||key>64U||key==end-offset)return I9100_BCM_FW_LAYOUT;
  for(unsigned i=0;i<entries;i++)if(lengths[i]==key&&!memcmp(bytes+offset,bytes+offsets[i],key))return I9100_BCM_FW_LAYOUT;
  offsets[entries]=(uint16_t)offset;lengths[entries++]=(uint16_t)key;
  if(key==7U&&!memcmp(bytes+offset,"macaddr",7U)){
   if(end-offset-key-1U!=17U)return I9100_BCM_FW_ID;
   uint8_t mac[6];const uint8_t *v=bytes+offset+key+1U;
   for(unsigned i=0;i<6U;i++){
    int hi=hex(v[i*3U]),lo=hex(v[i*3U+1U]);if(hi<0||lo<0||(i<5U&&v[i*3U+2U]!=':'))return I9100_BCM_FW_ID;
    mac[i]=(uint8_t)((hi<<4)|lo);
   }
   if(!equal(mac,expected,6U))return I9100_BCM_FW_ID;found_mac=1;
  }
  offset=end+1U;
 }
 for(uint32_t i=offset;i<n;i++)if(bytes[i])return I9100_BCM_FW_LAYOUT;
 return entries&&found_mac?0:I9100_BCM_FW_ID;
}
static int take(struct i9100_bcm_firmware *t,int cleanup)
{
 if(!t||!t->initialized||!t->enabled||t->owner!=(uintptr_t)t||!t->chip||!t->chip->initialized||
    t->chip->owner!=(uintptr_t)t->chip||!t->chip->bus||t->chip->firmware_holds!=1U)
  return I9100_BCM_FW_ARGUMENT;
 struct i9100_bcm4330 *b=t->chip->bus;if(b->io.claim(b->io.cookie))return I9100_BCM_FW_BUSY;
 int e=!b->enabled?I9100_BCM_FW_DISABLED:b->protocol_ready?I9100_BCM_FW_BUSY:
       !cleanup&&(b->fault||t->chip->fault||t->fault)?I9100_BCM_FW_IO:0;
 if(e)b->io.release(b->io.cookie);return e;
}
static void drop(struct i9100_bcm_firmware *t){t->chip->bus->io.release(t->chip->bus->io.cookie);}
static int guard(struct i9100_bcm_firmware *t)
{
 int e=i9100_bcm_chip_ram_guard_locked(t->chip);
 if(e)return e==I9100_BCM_CHIP_NOT_READY?I9100_BCM_FW_NOT_READY:I9100_BCM_FW_IO;
 const struct i9100_bcm_core *wireless=0;
 for(unsigned n=0;n<t->chip->topology.count;n++)if(t->chip->topology.cores[n].id==0x812U){
  if(wireless)return I9100_BCM_FW_LAYOUT;wireless=&t->chip->topology.cores[n];
 }
 if(!wireless)return I9100_BCM_FW_LAYOUT;
 uint32_t reset=0;
 if(i9100_bcm_bp_read_locked(t->chip->bus,wireless->wrapper+0x800U,&reset))return I9100_BCM_FW_IO;
 return reset&1U?0:I9100_BCM_FW_NOT_READY;
}
static int hash(struct i9100_bcm_firmware *t,const uint8_t *bytes,uint32_t n,const uint8_t expected[32])
{
 uint8_t digest[32]={0};if(t->integrity.sha256(t->integrity.cookie,bytes,n,digest))return I9100_BCM_FW_CRYPTO;
 return equal(digest,expected,32U)?0:I9100_BCM_FW_HASH;
}
int i9100_bcm_fw_init(struct i9100_bcm_firmware *t,int enabled,struct i9100_bcm_chip *c,
 const struct i9100_bcm_fw_profile *p,const struct i9100_bcm_fw_integrity *io)
{
 if(!t||t->initialized||(enabled!=0&&enabled!=1))return I9100_BCM_FW_ARGUMENT;
 if(!enabled)return I9100_BCM_FW_DISABLED;
 if(!c||!c->initialized||c->owner!=(uintptr_t)c||!c->bus||!c->probed||!c->ram_ready||
    c->topology.count>I9100_BCM_MAX_CORES||!p||!io||!io->sha256||p->board_data_reviewed!=1U||
    p->chip_revision>15U||p->ram_bytes!=c->ram_size||p->chip_revision!=((c->chip_id>>16)&15U)||
    !nonzero(p->firmware_sha256,32U)||!nonzero(p->nvram_sha256,32U)||!valid_mac(p->verified_factory_mac))
  return I9100_BCM_FW_ARGUMENT;
 if(aliases((uintptr_t)t,sizeof(*t),(uintptr_t)c,sizeof(*c))||
    aliases((uintptr_t)t,sizeof(*t),(uintptr_t)c->bus,sizeof(*c->bus))||
    aliases((uintptr_t)t,sizeof(*t),(uintptr_t)p,sizeof(*p))||
    aliases((uintptr_t)t,sizeof(*t),(uintptr_t)io,sizeof(*io)))return I9100_BCM_FW_ARGUMENT;
 const struct i9100_bcm_core *sd=sdio_core(c);if(!sd)return I9100_BCM_FW_LAYOUT;
 struct i9100_bcm_fw_layout l;int e=i9100_bcm_fw_make_layout(p->ram_bytes,sd->revision,p->firmware_bytes,p->nvram_bytes,&l);
 if(e)return e;
 struct i9100_bcm4330 *b=c->bus;if(b->io.claim(b->io.cookie))return I9100_BCM_FW_BUSY;
 if(c->firmware_holds||c->ram_selector_owned||c->ram_image_owned||c->fault||b->fault||!b->enabled||b->protocol_ready){
  b->io.release(b->io.cookie);return I9100_BCM_FW_BUSY;
 }
 memset(t,0,sizeof(*t));t->chip=c;t->profile=*p;t->layout=l;t->integrity=*io;
 t->owner=(uintptr_t)t;t->initialized=t->enabled=1;c->firmware_holds=1;b->io.release(b->io.cookie);return 0;
}
static int memory(struct i9100_bcm_firmware *t,int write,uint32_t address,uint8_t *data,uint32_t n)
{
 uint64_t now=t->chip->bus->io.now_ns(t->chip->bus->io.cookie);
 if(now<t->started||now-t->started>30000000000ULL)return I9100_BCM_FW_TIMEOUT;
 t->transfer_address=address;t->transfer_bytes=n;
 return i9100_bcm_bp_memory_locked(t->chip->bus,write,address,data,n)?I9100_BCM_FW_IO:0;
}
static int token(struct i9100_bcm_firmware *t,uint32_t value)
{
 uint8_t data[4],verify[4]={0};for(unsigned n=0;n<4U;n++)data[n]=(uint8_t)(value>>(8U*n));
 int e=memory(t,1,t->layout.token_address,data,4);
 if(!e)e=memory(t,0,t->layout.token_address,verify,4);
 if(!e&&!equal(data,verify,4U))e=I9100_BCM_FW_VERIFY;return e;
}
static int copy(struct i9100_bcm_firmware *t,uint32_t address,const uint8_t *source,uint32_t bytes,uint32_t padded,uint32_t *confirmed)
{
 uint8_t data[512],verify[512];uint32_t offset=0;
 while(offset<padded){
  int e=guard(t);if(e)return e;
  uint32_t n=padded-offset,window=0x8000U-((address+offset)&0x7fffU);
  if(n>512U)n=512U;if(n>window)n=window;
  memset(data,0,n);uint32_t logical=offset<bytes?bytes-offset:0U;if(logical>n)logical=n;
  if(logical)memcpy(data,source+offset,logical);
  e=memory(t,1,address+offset,data,n);if(!e)e=memory(t,0,address+offset,verify,n);
  if(e)return e;if(!equal(data,verify,n))return I9100_BCM_FW_VERIFY;
  *confirmed+=logical;offset+=n;
 }
 return 0;
}
static int aliases(uintptr_t a,uint32_t n,uintptr_t b,uint32_t m)
{return a>UINTPTR_MAX-n||b>UINTPTR_MAX-m||(a+n>b&&b+m>a);}
int i9100_bcm_fw_load(struct i9100_bcm_firmware *t,const uint8_t *fw,const uint8_t *nv)
{
 if(!t||!t->initialized||!fw||!nv)return I9100_BCM_FW_ARGUMENT;
 uint32_t fn=t->profile.firmware_bytes,nn=t->profile.nvram_bytes;
 if(aliases((uintptr_t)fw,fn,(uintptr_t)nv,nn)||aliases((uintptr_t)fw,fn,(uintptr_t)t,sizeof(*t))||
    aliases((uintptr_t)nv,nn,(uintptr_t)t,sizeof(*t)))return I9100_BCM_FW_ARGUMENT;
 int e=take(t,0);if(e)return e;
 if(t->boot_holds||t->loaded||t->execution_possible||t->chip->ram_image_owned){drop(t);return I9100_BCM_FW_BUSY;}
 t->stage=1;e=i9100_bcm_nvram_validate(nv,nn,t->profile.verified_factory_mac);
 if(!e)e=hash(t,fw,fn,t->profile.firmware_sha256);if(!e)e=hash(t,nv,nn,t->profile.nvram_sha256);
 if(e){t->error=e;drop(t);return e;}
 t->stage=2;e=guard(t);uint32_t identity=0;
 if(!e&&i9100_bcm_bp_read_locked(t->chip->bus,0x18000000U,&identity))e=I9100_BCM_FW_IO;
 if(!e&&identity!=t->chip->chip_id)e=I9100_BCM_FW_ID;
 if(e){t->error=e;if(e==I9100_BCM_FW_IO)t->fault=t->chip->bus->fault=1;drop(t);return e;}
 t->started=t->chip->bus->io.now_ns(t->chip->bus->io.cookie);
 t->confirmed_firmware_bytes=t->confirmed_nvram_bytes=t->token_verified=0;
 t->transfer_address=t->transfer_bytes=0;t->cleanup_error=0;
 t->chip->ram_image_owned=1;t->stage=3;e=token(t,0);
 if(!e){t->stage=4;e=copy(t,0,fw,fn,t->layout.firmware_padded,&t->confirmed_firmware_bytes);}
 if(!e){t->stage=5;e=copy(t,t->layout.nvram_address,nv,nn,t->layout.nvram_padded,&t->confirmed_nvram_bytes);}
 if(!e){t->stage=6;e=hash(t,fw,fn,t->profile.firmware_sha256);if(!e)e=hash(t,nv,nn,t->profile.nvram_sha256);}
 if(!e){t->stage=7;e=guard(t);if(!e)e=token(t,t->layout.token);}
 if(!e)e=guard(t);
 if(!e){t->loaded=t->token_verified=1;t->stage=8;}
 else{t->fault=1;if(e==I9100_BCM_FW_IO)t->chip->bus->fault=1;}
 t->error=e;drop(t);return e;
}
int i9100_bcm_fw_abort_locked(struct i9100_bcm_firmware *t)
{
 if(t->execution_possible)return I9100_BCM_FW_BUSY;
 int e=i9100_bcm_bp_restore_locked(t->chip->bus)?I9100_BCM_FW_IO:0;
 if(!e&&t->chip->ram_image_owned){
  e=guard(t);
  t->started=t->chip->bus->io.now_ns(t->chip->bus->io.cookie);
  if(!e)e=token(t,0);
  if(!e){t->chip->ram_image_owned=0;t->loaded=t->token_verified=0;}
 }
 t->cleanup_error=e;if(e)t->fault=1;return e;
}
int i9100_bcm_fw_abort(struct i9100_bcm_firmware *t)
{
 int e=take(t,1);if(e)return e;
 if(t->boot_holds){drop(t);return I9100_BCM_FW_BUSY;}
 e=i9100_bcm_fw_abort_locked(t);drop(t);return e;
}
int i9100_bcm_fw_close(struct i9100_bcm_firmware *t)
{
 int e=take(t,1);if(e)return e;
 if(t->boot_holds||t->execution_possible||t->chip->ram_image_owned||t->chip->bus->window_restore_pending){drop(t);return I9100_BCM_FW_BUSY;}
 t->chip->firmware_holds=0;drop(t);t->enabled=t->initialized=0;t->owner=0;t->chip=0;return 0;
}
