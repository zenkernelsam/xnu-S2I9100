/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_mfc_host.h"
#include "i9100_mfc_power.h"
static volatile uint32_t gate;
static uintptr_t root,child;static uint32_t root_generation,child_generation,generation,root_closing;
static int take(void){for(unsigned n=0;n<1000U;n++)if(__sync_bool_compare_and_swap(&gate,0U,1U))return 0;return I9100_MP_BUSY;}
static void drop(void){__sync_lock_release(&gate);}
static int exact(const struct i9100_mfc_host_lease *l)
{
 if(!l||l->self!=(uintptr_t)l||l->owned!=1U||!l->generation)return 0;
 if(!l->parent)return root==(uintptr_t)l&&root_generation==l->generation;
 return child==(uintptr_t)l&&child_generation==l->generation&&root==l->parent&&root_generation==l->parent_generation;
}
int i9100_mfc_host_acquire(const struct i9100_mfc_host_lease *parent,struct i9100_mfc_host_lease *l)
{
 if(!l||l==parent||l->owned||(l->self&&l->self!=(uintptr_t)l))return I9100_MP_ARGUMENT;
 int e=take();if(e)return e;
 if(generation==UINT32_MAX)e=I9100_MP_BUSY;
 else if(parent){
  if(!exact(parent)||parent->parent)e=I9100_MP_OWNER;
  else if(child||root_closing)e=I9100_MP_BUSY;
  else{child=(uintptr_t)l;child_generation=++generation;l->parent=(uintptr_t)parent;l->parent_generation=parent->generation;l->generation=child_generation;}
 }else if(root)e=I9100_MP_BUSY;
 else{root=(uintptr_t)l;root_generation=++generation;root_closing=0;l->parent=0;l->parent_generation=0;l->generation=root_generation;}
 if(!e){l->self=(uintptr_t)l;l->owned=1;}drop();return e;
}
int i9100_mfc_host_check(const struct i9100_mfc_host_lease *l)
{int e=take();if(e)return e;e=exact(l)?0:I9100_MP_OWNER;drop();return e;}
int i9100_mfc_host_has_child(const struct i9100_mfc_host_lease *l)
{int e=take();if(e)return e;e=exact(l)&&!l->parent?(child?1:0):I9100_MP_OWNER;drop();return e;}
int i9100_mfc_host_begin_close(const struct i9100_mfc_host_lease *l)
{
 int e=take();if(e)return e;
 if(!exact(l)||l->parent)e=I9100_MP_OWNER;else if(child)e=I9100_MP_BUSY;else root_closing=1;
 drop();return e;
}
int i9100_mfc_host_release(struct i9100_mfc_host_lease *l,int idle)
{
 int e=take();if(e)return e;
 if(!exact(l))e=I9100_MP_OWNER;else if(idle!=1||(!l->parent&&child))e=I9100_MP_BUSY;
 else{if(l->parent){child=0;child_generation=0;}else{root=0;root_generation=0;root_closing=0;}l->owned=0;}
 drop();return e;
}
