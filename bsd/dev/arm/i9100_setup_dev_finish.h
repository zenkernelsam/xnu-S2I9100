#ifndef I9100_SETUP_DEV_FINISH_H
#define I9100_SETUP_DEV_FINISH_H
/* Explicit developer shortcut for the UUID-matched S2/iOS6 Setup image.
 * Preserve run's window/software drawing initialization. Schedule the original
 * _finishBuddy with NSObject's native run-loop timer, after application launch.
 * The timer is not a CPU spin or a proof of setup completion. Native preferences,
 * notification and suspension remain responsible for the transition.
 * No activation certificates, credentials or encrypted keybags are created. */
volatile uint32_t i9100_setup_dev_finish_diag[16];
static uint32_t i9100_setup_dev_finish_pending[4];
static const uint8_t i9100_setup_dev_finish_uuid[16]={
 0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd};
static const uint8_t i9100_setup_run_original[8]={0xf0,0xb5,0x03,0xaf,0x2d,0xe9,0x00,0x0d};
static const uint8_t i9100_setup_finish_original[8]={0xb0,0xb5,0x02,0xaf,0x81,0xb0,0x04,0x46};
static void i9100_setup_finish_mov32(uint8_t *out,unsigned reg,uint32_t value)
{
 for(unsigned n=0;n<2;n++){
  unsigned v=(value>>(n*16))&65535U;
  uint16_t words[2]={(uint16_t)((n?0xf2c0U:0xf240U)|((v>>12)&15U)|(((v>>11)&1U)<<10)),
   (uint16_t)((((v>>8)&7U)<<12)|(reg<<8)|(v&255U))};
  memcpy(out+n*4,words,4);
 }
}
static void i9100_setup_finish_code(uint8_t *out,unsigned header)
{
 /* Matched to port-tests/setup-finish-template.s. objc_msgSend takes the
  * afterDelay double on the aligned ARM32 stack after self/SEL/SEL/id. */
 static const uint8_t code[132]={
  0x2d,0xe9,0xff,0x5f,0xef,0xf3,0x00,0x8c,0x82,0xb0,0xcd,0xf8,0x00,0xc0,0x2d,0xed,
  0x10,0x0b,0x6d,0xed,0x20,0x0b,0x84,0xb0,0xf1,0xee,0x10,0xca,0xcd,0xf8,0x08,0xc0,
  0x40,0xf2,0x00,0x01,0xc0,0xf2,0x00,0x01,0x09,0x68,0x40,0xf2,0x00,0x02,0xc0,0xf2,
  0x00,0x02,0x12,0x68,0x69,0xb1,0x62,0xb1,0x00,0x23,0x00,0x93,0x40,0xf2,0x00,0x0c,
  0xc0,0xf2,0x00,0x0c,0xcd,0xf8,0x04,0xc0,0x40,0xf2,0x00,0x0c,0xc0,0xf2,0x00,0x0c,
  0xe0,0x47,0xdd,0xf8,0x08,0xc0,0xe1,0xee,0x10,0xca,0x04,0xb0,0xfd,0xec,0x20,0x0b,
  0xbd,0xec,0x10,0x0b,0xdd,0xf8,0x00,0xc0,0x02,0xb0,0x8c,0xf3,0x00,0x8c,0xbd,0xe8,
  0xff,0x5f,0x00,0xbf,0,0,0,0,0,0,0,0,0xdf,0xf8,0x00,0xf0,0,0,0,0};
 memcpy(out,code,sizeof(code));
 i9100_setup_finish_mov32(out+32,1,header+0x60068U);
 i9100_setup_finish_mov32(out+42,2,header+0x5f430U);
 i9100_setup_finish_mov32(out+60,12,0x40140000U); /* IEEE double 5 seconds, low word zero. */
 i9100_setup_finish_mov32(out+72,12,header+0x53f10U); /* Original ARM objc_msgSend stub. */
 memcpy(out+116,i9100_setup_run_original,8);
 unsigned resume=header+0x2775U;memcpy(out+128,&resume,4);
}
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
 d[0]=2;d[1]=1;d[2]=p->p_pid;d[3]=header;d[4]=p->p_idversion;
 for(unsigned n=5;n<16;n++)d[n]=0;
 /* Original __TEXT prefers 0x1000; header is the actual mapped base. */
 unsigned site=header+0x276cU,target=header+0x1f84U,page=site&~4095U;
 uint8_t original[8],finish[8],message[4],selector[sizeof("performSelector:withObject:afterDelay:")];
 uint8_t finish_name[sizeof("_finishBuddy")];
 static const uint8_t original_message[4]={0x70,0xf2,0x9f,0xe5};
 if(copyin(site,original,8) || memcmp(original,i9100_setup_run_original,8) ||
    copyin(target,finish,8) || memcmp(finish,i9100_setup_finish_original,8) ||
    copyin(header+0x53f10U,message,4) || memcmp(message,original_message,4) ||
    copyin(header+0x48f4eU,selector,sizeof(selector)) ||
    memcmp(selector,"performSelector:withObject:afterDelay:",sizeof(selector)) ||
    copyin(header+0x42e52U,finish_name,sizeof(finish_name)) ||
    memcmp(finish_name,"_finishBuddy",sizeof(finish_name))){
  d[5]=1;d[1]=3;return 0;
 }
 vm_map_t map=current_map();
 vm_map_offset_t address=4096;uint8_t code[132];int touched=0;
 kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
  VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[6]=result;
 if(result!=KERN_SUCCESS){d[1]=3;return 0;}
 if(address<4096 || address>0x3ffff000U){d[5]=2;goto failed;}
 d[12]=address;d[13]=sizeof(code);d[14]=header+0x60068U;d[15]=header+0x5f430U;
 i9100_setup_finish_code(code,header);
 d[7]=copyout(code,address,sizeof(code));if(d[7])goto failed;
 flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
 result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[6]=result;
 if(result!=KERN_SUCCESS)goto failed;
 result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[6]=result;
 if(result!=KERN_SUCCESS)goto failed;
 uint32_t branch[2]={0xf000f8dfU,(unsigned)address+1U};touched=1;
 d[7]=copyout(branch,site,8);
 flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
 result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[6]=result;
 if(d[7] || result!=KERN_SUCCESS)goto failed;
 i9100_ui_density_sync_current();
 d[10]=site;d[11]=target;OSMemoryBarrier();d[1]=2;return 0;
failed:
 if(touched){
  kern_return_t restored=vm_map_protect(map,page,page+4096,
   VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
  if(restored==KERN_SUCCESS)restored=copyout(original,site,8);
  flush_dcache(site,8,FALSE);invalidate_icache(site,8,FALSE);
  kern_return_t rx=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
  d[8]=restored;d[9]=rx;d[1]=3;
  i9100_ui_density_sync_current();
  if(restored!=KERN_SUCCESS || rx!=KERN_SUCCESS)return EIO;
 }
 vm_map_remove(map,address,address+4096,0);
 d[1]=3;return 0;
}
#endif
