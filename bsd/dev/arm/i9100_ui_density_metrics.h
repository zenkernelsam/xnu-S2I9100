#ifndef I9100_UI_DENSITY_METRICS_H
#define I9100_UI_DENSITY_METRICS_H
/* Experimental publication-point calibration. Run the complete original
 * UIScreen computation first, then change only its matched internal 480x800
 * result and the original GS scale before the caller receives the metrics. */
#include "i9100_ui_density_metrics_code.h"
static const uint8_t i9100_metrics_original[12]={
 0x0a,0xb0,0xbd,0xec,0x02,0x8b,0xbd,0xe8,0x00,0x05,0xf0,0xbd};
static unsigned i9100_metrics_enabled(void)
{
 int enabled=0;PE_parse_boot_argn("i9100uidensitymetrics",&enabled,4);
 return enabled==1 && i9100_ui_density_option();
}
static void i9100_metrics_code(uint8_t *out,unsigned slide,unsigned option)
{
 memcpy(out,i9100_metrics_template,sizeof(i9100_metrics_template));
 i9100_ui_density_mov32(out+64,1,0x397e9534U+slide);
 i9100_ui_density_mov32(out+114,2,0x3a282460U+slide);
 i9100_ui_density_mov32(out+138,2,0x3a283e7cU+slide);
 i9100_ui_density_mov32(out+156,12,option==1333?0x3faaaaabU:0x3fc00000U);
}
static int i9100_metrics_bootstrap(proc_t p,unsigned pc,unsigned cpsr)
{
 int slot=i9100_ui_density_slot(p);unsigned option=i9100_ui_density_option();
 if(slot<1 || !i9100_metrics_enabled() || !option || !p->task || current_task()!=p->task ||
    current_map()!=get_task_map(p->task) || get_task_numacts(p->task)!=1 ||
    ml_at_interrupt_context() || !ml_get_interrupts_enabled() || get_preemption_level())return 0;
 if(!OSCompareAndSwap(0,1,&i9100_ui_density_guard[slot]))return 0;
 uint32_t *pending=i9100_ui_density_pending[slot];volatile uint32_t *d=i9100_ui_density_diag[slot];
 if(pending[0]!=(unsigned)p->p_pid || pending[1]!=(unsigned)(uintptr_t)p->task ||
    pending[3]!=(unsigned)p->p_idversion){i9100_ui_density_guard[slot]=0;return 0;}
 unsigned header=pending[2];memset(pending,0,16);
 d[0]=4;d[1]=1;d[2]=p->p_pid;d[3]=(unsigned)(uintptr_t)p->task;d[4]=header;
 d[6]++;d[7]=d[8]=d[9]=d[11]=0;d[12]=p->p_idversion;d[13]=option;d[18]=3;d[19]=372;
 task_dyld_info_data_t info={0};mach_msg_type_number_t count=TASK_DYLD_INFO_COUNT;
 uint32_t dyld[21];unsigned slide=0,site=0,page=0;uint8_t original[12],code[276];
 vm_map_t map=current_map();vm_map_offset_t address=4096;unsigned allocated=0,touched=0;int error=0;
 if(pc<4096 || pc>=0x40000000U || (cpsr&31U)!=16 ||
    task_info(current_task(),TASK_DYLD_INFO,(task_info_t)&info,&count)!=KERN_SUCCESS ||
    count!=TASK_DYLD_INFO_COUNT || info.all_image_info_format!=TASK_DYLD_ALL_IMAGE_INFO_32 ||
    info.all_image_info_addr<4096 || info.all_image_info_addr>0x3fffffacU || info.all_image_info_size<84 ||
    copyin(info.all_image_info_addr,dyld,84) || dyld[0]<12 || dyld[0]>15){d[8]=1;goto failed;}
 slide=dyld[20];d[14]=slide;
 if((slide&4095U) || slide>0x0375b000U){d[8]=1;goto failed;}
 /* UIKit original UUID; GS remains the source of the physical pixel state. */
 uint32_t mh[7],lc[2];uint8_t uuid[16];unsigned at=0x32f2f01cU+slide,end;int matched=0;
 static const uint8_t ui_uuid[16]={0xad,0x8b,0x3a,0xd2,0x3f,0x41,0x31,0x87,0xa1,0x78,0x17,0x9d,0xb3,0x9c,0xfa,0x6b};
 if(copyin(0x32f2f000U+slide,mh,28) || mh[0]!=0xfeedfaceU || mh[1]!=12 ||
    mh[4]>128 || mh[5]<24 || mh[5]>0x4000){d[8]=2;goto failed;}
 end=at+mh[5];
 for(unsigned n=0;n<mh[4];n++){
  if(at>end-8 || copyin(at,lc,8) || lc[1]<8 || lc[1]>end-at)break;
  if(lc[0]==27){matched=lc[1]==24 && !copyin(at+8,uuid,16) && !memcmp(uuid,ui_uuid,16);break;}
  at+=lc[1];
 }
 if(!matched || !i9100_ui_density_image(0x34c2f000U+slide)){d[8]=2;goto failed;}
 site=0x32f882dcU+slide;page=site&~4095U;d[15]=site;
 if(copyin(site,original,12) || memcmp(original,i9100_metrics_original,12)){d[8]=3;goto failed;}
 kern_return_t result=vm_map_enter(map,&address,4096,0,VM_FLAGS_ANYWHERE,VM_OBJECT_NULL,0,FALSE,
  VM_PROT_READ|VM_PROT_WRITE,VM_PROT_ALL,VM_INHERIT_COPY);d[7]=result;
 if(result!=KERN_SUCCESS)goto failed;allocated=1;d[5]=address;
 if(address<4096 || address>0x3ffff000U){d[8]=4;goto failed;}
 i9100_metrics_code(code,slide,option);
 if(copyout(code,address,sizeof(code))){d[8]=5;goto failed;}
 flush_dcache(address,sizeof(code),FALSE);invalidate_icache(address,sizeof(code),FALSE);
 result=vm_map_protect(map,address,address+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
 if(result!=KERN_SUCCESS)goto failed;
 result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);d[7]=result;
 if(result!=KERN_SUCCESS)goto failed;
 uint32_t branch[3]={0xf000f8dfU,(unsigned)address+1U,0xbf00bf00U};touched=1;
 if(copyout(branch,site,12)){d[8]=6;goto failed;}
 flush_dcache(site,12,FALSE);invalidate_icache(site,12,FALSE);
 result=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);d[7]=result;
 if(result!=KERN_SUCCESS)goto failed;
 i9100_ui_density_sync_current();d[10]++;d[11]=4;OSMemoryBarrier();d[1]=2;
 i9100_ui_density_guard[slot]=0;return 0;
failed:
 if(touched){
  kern_return_t restored=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY,FALSE);
  if(restored==KERN_SUCCESS)restored=copyout(original,site,12);
  flush_dcache(site,12,FALSE);invalidate_icache(site,12,FALSE);
  kern_return_t rx=vm_map_protect(map,page,page+4096,VM_PROT_READ|VM_PROT_EXECUTE,FALSE);
  if(restored!=KERN_SUCCESS || rx!=KERN_SUCCESS){d[9]=restored?restored:rx;error=EIO;}
 }
 i9100_ui_density_sync_current();if(allocated && !error)vm_map_remove(map,address,address+4096,0);
 OSMemoryBarrier();d[1]=3;i9100_ui_density_guard[slot]=0;return error;
}
static void i9100_metrics_note(proc_t p,int slot,unsigned pc,unsigned sp,unsigned phase)
{
 volatile uint32_t *d=i9100_ui_density_diag[slot];unsigned offset=phase==1?22:phase==2?248:0;
 if(!offset || d[1]!=2 || d[2]!=(unsigned)p->p_pid || d[12]!=(unsigned)p->p_idversion ||
    p->task!=current_task() || current_map()!=get_task_map(p->task) || pc!=d[5]+offset ||
    sp<4096 || sp>0x3fffffc0U || (sp&3U) || ml_at_interrupt_context() ||
    !ml_get_interrupts_enabled() || get_preemption_level())return;
 uint32_t saved[16],object[8]={0};unsigned error=copyin(sp,saved,64),self=0;
 if(!error){self=saved[6];if(self<4096 || self>0x3fffffe0U)error=14;else error=copyin(self,object,32);}
 if(!error && object[0]!=0x397e9534U+d[14])error=14;
 if(!OSCompareAndSwap(0,1,&i9100_ui_density_guard[slot]))return;
 volatile uint32_t *e=i9100_ui_density_execution[slot];e[0]=2;e[1]=p->p_pid;e[2]=p->p_idversion;
 if(e[phase+2]!=UINT32_MAX)e[phase+2]++;e[6]=error;e[7]=phase;e[8]=error?0:saved[15];
 e[9]=sp;e[10]=error?0:object[6];e[11]=error?0:self;
 OSMemoryBarrier();i9100_ui_density_guard[slot]=0;
}
#endif
