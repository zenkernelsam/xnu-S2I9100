/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_camera_engine_io.h"
int i9100_camera_reg_save(const struct i9100_camera_engine_io *io,struct i9100_camera_register *r)
{
 uint32_t v=io->read32(io->cookie,r->offset);
 if(v==UINT32_MAX)return I9100_CE_IO;
 r->saved=r->last=r->attempted=v&r->mask;r->dirty=r->uncertain=0;return 0;
}
int i9100_camera_reg_set(const struct i9100_camera_engine_io *io,struct i9100_camera_register *r,uint32_t value)
{
 uint32_t v=io->read32(io->cookie,r->offset),wanted=value&r->mask;
 if(v==UINT32_MAX)return I9100_CE_IO;
 if((v&r->mask)!=r->last&&(!r->uncertain||(v&r->mask)!=r->attempted))return I9100_CE_OWNER;
 r->attempted=wanted;r->uncertain=1;r->dirty=1;
 io->write32(io->cookie,r->offset,(v&~r->mask)|wanted);io->barrier(io->cookie);
 v=io->read32(io->cookie,r->offset);
 if(v==UINT32_MAX||(v&r->mask)!=wanted)return I9100_CE_IO;
 r->last=wanted;r->uncertain=0;return 0;
}
int i9100_camera_reg_restore(const struct i9100_camera_engine_io *io,struct i9100_camera_register *r)
{
 if(!r->dirty)return 0;
 int e=i9100_camera_reg_set(io,r,r->saved);
 if(!e)r->dirty=0;
 return e;
}
