/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_writer.h"
#include <string.h>
static int valid(struct i9100_emmc_writer *w)
{return !w||!w->initialized?I9100_EMMC_ARGUMENT:w->self!=(uintptr_t)w?I9100_EMMC_OWNER:0;}
static int exclusive(struct i9100_emmc_writer *w)
{return !w->card||w->io.verify_exclusive(w->io.cookie,w->card)?I9100_EMMC_OWNER:0;}
static int fail(struct i9100_emmc_writer *w,int error)
{
 if(!w->error)w->error=error;w->grant.poisoned=1;
 if(!w->grant.first_error)w->grant.first_error=error;return error;
}
static int read_sector(void *cookie,uint64_t lba,uint8_t bytes[512])
{return i9100_emmc_read(((struct i9100_emmc_writer *)cookie)->card,lba,1,bytes);}
static int named(const struct i9100_emmc_partition *p,uint64_t lba,uint64_t sectors,unsigned gpt,unsigned pit)
{return p&&p->start_lba==lba&&p->sectors==sectors&&p->gpt_number==gpt&&p->pit_identifier==pit&&!p->attributes;}
int i9100_emmc_writer_init(struct i9100_emmc_writer *w,const struct i9100_emmc_writer_io *io)
{
 if(!w||w->initialized||!io||!io->claim||!io->release||!io->verify_exclusive)return I9100_EMMC_ARGUMENT;
 memset(w,0,sizeof(*w));w->self=(uintptr_t)w;w->initialized=1;w->io=*io;return 0;
}
int i9100_emmc_writer_open(struct i9100_emmc_writer *w,int enabled,struct i9100_emmc_card *card,
 const struct i9100_emmc_writer_identity *identity,enum i9100_emmc_write_role role,uint64_t first,uint64_t count)
{
 int e=valid(w);if(e)return e;if(enabled==0)return I9100_EMMC_UNSUPPORTED;
 if(enabled!=1||!card||!identity||(role!=I9100_EMMC_SYSTEM_VOLUME&&role!=I9100_EMMC_DATA_VOLUME)||!count)
  return I9100_EMMC_ARGUMENT;
 if(w->io.claim(w->io.cookie))return I9100_EMMC_BUSY;
 if(w->opened||w->closing||card->write_grant){e=I9100_EMMC_BUSY;goto out;}
 w->card=card;
 if(exclusive(w)||card->self!=(uintptr_t)card||!card->ready||card->sectors!=30777344ULL||identity->sectors!=card->sectors||
    memcmp(card->cid,identity->cid,sizeof(card->cid))){e=I9100_EMMC_OWNER;goto out;}
 /* Read freshly after exclusive ownership; no caller-provided fake layout
  * or historical backup may grant a writable card by itself. */
 struct i9100_emmc_layout layout;
 struct i9100_layout_io io={w,read_sector,card->sectors};
 e=i9100_emmc_read_layout(&io,&layout);if(!e)e=i9100_emmc_check_pit(&io,&layout);if(e)goto out;
 if(layout.count!=12U||memcmp(layout.disk_guid,identity->disk_guid,16)||layout.primary_crc!=identity->primary_crc||
    layout.backup_crc!=identity->backup_crc||layout.entries_crc!=identity->entries_crc){e=I9100_EMMC_OWNER;goto out;}
 const struct i9100_emmc_partition *system=i9100_emmc_find_partition(&layout,"FACTORYFS"),*data=i9100_emmc_find_partition(&layout,"DATAFS");
 if(!named(system,344064ULL,3145728ULL,9,10)||!named(data,3489792ULL,12582912ULL,10,11)||
    memcmp(system->unique_guid,identity->system_guid,16)||memcmp(data->unique_guid,identity->data_guid,16))
  {e=I9100_EMMC_PARTITION;goto out;}
 const struct i9100_emmc_partition *p=role==I9100_EMMC_SYSTEM_VOLUME?system:data;
 if(first<p->start_lba||first>=p->start_lba+p->sectors||count>p->start_lba+p->sectors-first)
  {e=I9100_EMMC_PARTITION;goto out;}
 if(card->ext_csd[33]>1U||(card->ext_csd[33]&&card->ext_csd[192]<6U)){e=I9100_EMMC_UNSUPPORTED;goto out;}
 if(card->ext_csd[33]&&!(card->ext_csd[249]|card->ext_csd[250]|card->ext_csd[251]|card->ext_csd[252]))
  {e=I9100_EMMC_UNSUPPORTED;goto out;}
 memset(&w->grant,0,sizeof(w->grant));w->grant.self=(uintptr_t)&w->grant;w->grant.card=(uintptr_t)card;
 w->grant.generation=card->generation;memcpy(w->grant.cid,card->cid,sizeof(card->cid));w->grant.enabled=1;
 w->grant.partition_role=role;w->grant.first_lba=first;w->grant.sectors=count;
 w->identity=*identity;w->role=role;w->opened=1;w->error=0;w->verified_sectors=0;card->write_grant=&w->grant;
out:
 if(e&&!w->opened)w->card=0;w->io.release(w->io.cookie);return e;
}
int i9100_emmc_writer_write(struct i9100_emmc_writer *w,uint64_t relative,uint32_t count,const uint8_t *bytes)
{
 int e=valid(w);if(e)return e;if(!bytes||!count||count>I9100_EMMC_MAX_SECTORS)return I9100_EMMC_ARGUMENT;
 uint32_t n=count*512U;
 if((uintptr_t)bytes>UINTPTR_MAX-n||((uintptr_t)bytes<(uintptr_t)w+sizeof(*w)&&
    (uintptr_t)w<(uintptr_t)bytes+n))return I9100_EMMC_ARGUMENT;
 if(w->io.claim(w->io.cookie))return I9100_EMMC_BUSY;
 if(!w->opened||w->closing){e=I9100_EMMC_NO_DEVICE;goto out;}
 if(w->error||w->grant.poisoned){e=w->error?w->error:I9100_EMMC_DIRTY;goto out;}
 if(exclusive(w)){e=fail(w,I9100_EMMC_OWNER);goto out;}
 if(relative>=w->grant.sectors||count>w->grant.sectors-relative){e=I9100_EMMC_PARTITION;goto out;}
 memcpy(w->staged,bytes,n);
 e=i9100_emmc_write(w->card,&w->grant,w->grant.first_lba+relative,count,w->staged);
 if(e){e=fail(w,e);goto out;}
 e=i9100_emmc_read(w->card,w->grant.first_lba+relative,count,w->readback);
 if(!e&&memcmp(w->staged,w->readback,n))e=I9100_EMMC_READBACK;
 if(e){e=fail(w,e);goto out;}
 w->verified_sectors=UINT32_MAX-w->verified_sectors<count?UINT32_MAX:w->verified_sectors+count;
out:w->io.release(w->io.cookie);return e;
}
int i9100_emmc_writer_synchronize(struct i9100_emmc_writer *w)
{
 int e=valid(w);if(e)return e;if(w->io.claim(w->io.cookie))return I9100_EMMC_BUSY;
 if(!w->opened){e=I9100_EMMC_NO_DEVICE;goto out;}
 if(w->error||w->grant.poisoned){e=w->error?w->error:I9100_EMMC_DIRTY;goto out;}
 if(exclusive(w)){e=fail(w,I9100_EMMC_OWNER);goto out;}
 e=i9100_emmc_synchronize(w->card,&w->grant);if(e)e=fail(w,e);
out:w->io.release(w->io.cookie);return e;
}
int i9100_emmc_writer_close(struct i9100_emmc_writer *w)
{
 int e=valid(w);if(e)return e;if(w->io.claim(w->io.cookie))return I9100_EMMC_BUSY;
 if(!w->opened){e=I9100_EMMC_NO_DEVICE;goto out;}
 w->closing=1;
 if(w->error||w->grant.poisoned){e=w->error?w->error:I9100_EMMC_DIRTY;goto out;}
 if(exclusive(w)){e=fail(w,I9100_EMMC_OWNER);goto out;}
 e=i9100_emmc_synchronize(w->card,&w->grant);if(e){e=fail(w,e);goto out;}
 if(w->card->write_grant!=&w->grant){e=fail(w,I9100_EMMC_OWNER);goto out;}
 w->card->write_grant=0;w->grant.enabled=0;w->opened=w->closing=0;w->card=0;
out:w->io.release(w->io.cookie);return e;
}
