#ifndef I9100_SETUP_DEV_FINISH_H
#define I9100_SETUP_DEV_FINISH_H
/* Explicit developer shortcut for the UUID-matched S2/iOS6 Setup image.
 * Original SetupController.run and _finishBuddy are void methods with the
 * same self ABI. _finishBuddy uses native preferences/notification/suspend.
 * No activation certificates, credentials or encrypted keybags are created. */
volatile uint32_t i9100_setup_dev_finish_diag[12];
static uint32_t i9100_setup_dev_finish_pending[4];
static const uint8_t i9100_setup_dev_finish_uuid[16]={
 0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd};
static const uint8_t i9100_setup_run_original[8]={0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d};
static const uint8_t i9100_setup_finish_original[8]={0xb0,0xb5,0x02,0xaf,0x81,0xb0,0x04,0x46};
static unsigned i9100_setup_dev_finish_enabled(proc_t p)
{
 int enabled=0,abi=0,aks=0;
 PE_parse_boot_argn("i9100setupdevfinish",&enabled,4);
 PE_parse_boot_argn("i9100hidabi",&abi,4);
 PE_parse_boot_argn("i9100aksdev",&aks,4);
 return p && enabled==1 && abi==6 && aks==1 && !strcmp(p->p_comm,"Setup") &&
  !memcmp(p->p_uuid,i9100_setup_dev_finish_uuid,16);
}
static void i9100_setup_dev_finish_record(proc_t p,unsigned header)
{
 if(!i9100_setup_dev_finish_enabled(p) || !p->task || header<4096 ||
    header>0x3ff00000U || (header&4095U))return;
 i9100_setup_dev_finish_pending[0]=p->p_pid;
 i9100_setup_dev_finish_pending[1]=(uint32_t)(uintptr_t)p->task;
 i9100_setup_dev_finish_pending[2]=header;
 i9100_setup_dev_finish_pending[3]=p->p_idversion;
}
int i9100_setup_dev_finish_bootstrap(proc_t p)
{
 if(!i9100_setup_dev_finish_enabled(p) || !p->task || current_task()!=p->task ||
    current_map()!=get_task_map(p->task) || get_task_numacts(p->task)!=1 ||
    ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level())return 0;
 uint32_t *pending=i9100_setup_dev_finish_pending;
 if(pending[0]!=(unsigned)p->p_pid || pending[1]!=(uint32_t)(uintptr_t)p->task ||
    pending[3]!=(unsigned)p->p_idversion)return 0;
 unsigned header=pending[2];memset(pending,0,sizeof(i9100_setup_dev_finish_pending));
 volatile uint32_t *d=i9100_setup_dev_finish_diag;
 d[0]=1;d[1]=1;d[2]=p->p_pid;d[3]=header;d[4]=p->p_idversion;
 unsigned site=header+0x376cU,target=header+0x2f84U,page=site&~4095U;
 uint8_t original[8],finish[8];
 if(copyin(site,original,8) || memcmp(original,i9100_setup_run_original,8) ||
    copyin(target,finish,8) || memcmp(finish,i9100_setup_finish_original,8)){
  d[5]=1;d[1]=3;return 0;
 }
 vm_map_t map=current_map();
 kern_return_t result=vm_map_protect(map,page,page+4096,
  VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[6]=result;
 if(result!=KERN_SUCCESS){d[1]=3;return 0;}
 uint32_t branch[2]={0xf000f8dfU,target+1U};
 unsigned error=copyout(branch,site,8);d[7]=error;
 flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
 kern_return_t protect=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
 if(error || protect!=KERN_SUCCESS){
  kern_return_t restored=vm_map_protect(map,page,page+4096,
   VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
  if(restored==KERN_SUCCESS)restored=copyout(original,site,8);
  flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
  kern_return_t rx=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
  d[8]=restored;d[9]=rx;d[1]=3;
  i9100_ui_density_sync_current();
  return restored!=KERN_SUCCESS || rx!=KERN_SUCCESS?EIO:0;
 }
 i9100_ui_density_sync_current();
 d[10]=site;d[11]=target;OSMemoryBarrier();d[1]=2;return 0;
}
#endif
