/* SPDX-License-Identifier: BSD-2-Clause */
/* Included in actual kern_shutdown.c; proc_shutdown remains its original
 * implementation. Interrupt path only publishes a request. One worker does
 * checked process shutdown, non-forced HFS unmount, then native card flush.
 * Any failure keeps PMU untouched and does not retry the same request. */
static thread_call_t i9100_storage_shutdown_call;
volatile uint32_t i9100_storage_shutdown_diag[8]={1U};
static void i9100_storage_shutdown_failed(int error)
{
 i9100_storage_shutdown_diag[2]=(uint32_t)error;i9100_storage_shutdown_diag[1]=7U;
 __sync_synchronize();i9100_storage_reboot_gate=I9100_STORAGE_REBOOT_FAILED;
 i9100_storage_reboot_ready=I9100_STORAGE_REBOOT_FAILED;
}
static void i9100_storage_shutdown_worker(thread_call_param_t p0,thread_call_param_t p1)
{
 (void)p0;(void)p1;
 unsigned action=__sync_val_compare_and_swap(&i9100_storage_reboot_request,0U,0U);
 if(action<1U||action>4U||action==3U||!i9100_hfs_root_required())return;
 i9100_storage_shutdown_diag[3]=action;i9100_storage_shutdown_diag[4]++;
 i9100_storage_shutdown_diag[1]=1U;
 if(!__sync_bool_compare_and_swap(&system_inshutdown,0U,1U)){
  i9100_storage_shutdown_failed(EBUSY);return;
 }
 /* proc_shutdown drops its historical init reference. Pin our own process
  * reference before entering it, instead of dereferencing a bare global. */
 proc_t init=proc_find(1);
 i9100_storage_shutdown_diag[1]=2U;
 proc_shutdown();
 i9100_storage_shutdown_diag[1]=3U;
 int suspend_error=init&&current_proc()!=init?task_suspend(init->task):0;
 if(init)proc_rele(init);
 if(suspend_error!=KERN_SUCCESS){i9100_storage_shutdown_failed(EBUSY);return;}
 i9100_storage_shutdown_diag[1]=4U;
 int error=i9100_hfs_data_shutdown();
 if(error){
  i9100_storage_shutdown_failed(error);
  printf("i9100 DATAFS shutdown: errno=%d, reset blocked\n",error);return;
 }
 /* This means safe to route a reset, not that reset already occurred.
  * USB/MCT may only reset after observing this word. */
 i9100_storage_shutdown_diag[1]=6U;
 __sync_synchronize();i9100_storage_reboot_ready=I9100_STORAGE_REBOOT_READY;
}
static void i9100_storage_shutdown_poll(void *unused)
{
 (void)unused;unsigned request=i9100_storage_reboot_request;
 if(request&&i9100_storage_shutdown_call&&
    __sync_bool_compare_and_swap(&i9100_storage_reboot_ready,0U,I9100_STORAGE_REBOOT_PREPARING))
  thread_call_enter(i9100_storage_shutdown_call);
 if(!request)timeout(i9100_storage_shutdown_poll,NULL,hz);
}
void i9100_hfs_shutdown_prepare(void)
{
 if(!i9100_hfs_root_required())return;
 if(i9100_storage_shutdown_call||i9100_storage_reboot_gate!=I9100_STORAGE_REBOOT_PREPARING)return;
 i9100_storage_shutdown_call=thread_call_allocate_with_priority(i9100_storage_shutdown_worker,NULL,THREAD_CALL_PRIORITY_KERNEL);
 if(!i9100_storage_shutdown_call){i9100_storage_shutdown_failed(ENOMEM);return;}
 timeout(i9100_storage_shutdown_poll,NULL,hz);
 __sync_synchronize();i9100_storage_reboot_gate=I9100_STORAGE_REBOOT_READY;
}
