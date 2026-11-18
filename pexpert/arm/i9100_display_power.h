#ifndef I9100_DISPLAY_POWER_H
#define I9100_DISPLAY_POWER_H
#include <stdint.h>
/* WIN0MAP black mapping lowers AMOLED pixel load without disturbing FIMD
 * timing, framebuffer storage or the bootloader's panel/gamma setup. */
struct i9100_display_power_io {
 void *cookie;
 uint32_t (*read)(void *);
 void (*write)(void *,uint32_t);
 void (*sync)(void *);
};
struct i9100_display_power_state {uint32_t blanked,saved_map,owned_map;};
static int i9100_display_power_change(struct i9100_display_power_state *s,
 const struct i9100_display_power_io *io,unsigned state)
{
 if(!s || !io || !io->read || !io->write || !io->sync || state>2)return 1;
 uint32_t current=io->read(io->cookie);
 if(!state){
  if(s->blanked)return current==s->owned_map?0:3;
  uint32_t black=(current&~0x01ffffffU)|0x01000000U;
  io->write(io->cookie,black);io->sync(io->cookie);
  if(io->read(io->cookie)!=black)return 4;
  s->saved_map=current;s->owned_map=black;s->blanked=1;return 0;
 }
 if(!s->blanked)return 0;
 if(current!=s->owned_map)return 3;
 io->write(io->cookie,s->saved_map);io->sync(io->cookie);
 if(io->read(io->cookie)!=s->saved_map)return 4;
 s->blanked=0;return 0;
}
#endif
