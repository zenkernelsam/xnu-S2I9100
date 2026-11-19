/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_emmc_host.h"
#include "i9100_emmc.h"
static volatile uintptr_t host_owner;
int i9100_emmc_host_acquire(struct i9100_emmc_host_lease *l)
{
 if(!l||l->owned||(l->self&&l->self!=(uintptr_t)l))return I9100_EMMC_ARGUMENT;
 if(!__sync_bool_compare_and_swap(&host_owner,0U,(uintptr_t)l))return I9100_EMMC_BUSY;
 l->self=(uintptr_t)l;l->owned=1;return 0;
}
int i9100_emmc_host_check(const struct i9100_emmc_host_lease *l)
{
 return !l||!l->owned||l->self!=(uintptr_t)l||__sync_val_compare_and_swap(&host_owner,0U,0U)!=(uintptr_t)l
  ?I9100_EMMC_OWNER:0;
}
int i9100_emmc_host_release(struct i9100_emmc_host_lease *l,int idle)
{
 int e=i9100_emmc_host_check(l);if(e)return e;if(idle!=1)return I9100_EMMC_BUSY;
 if(!__sync_bool_compare_and_swap(&host_owner,(uintptr_t)l,0U))return I9100_EMMC_OWNER;
 l->owned=0;return 0;
}
