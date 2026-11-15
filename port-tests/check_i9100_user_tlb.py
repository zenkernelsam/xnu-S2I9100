#!/usr/bin/env python3
"""Production PTE flags and ASID invalidation against a retained global alias."""
import os
import sys
if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],
              dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import hashlib
import json
import subprocess

repo=Path(__file__).resolve().parents[1]
source=(repo/'osfmk/arm/pmap.c').read_text()
def function(name):
    at=source.index(name+'(')
    # Skip any earlier call/declaration; these two definitions have no earlier calls.
    begin=source.index('{',at);depth=1;end=begin+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[at:end]
flags='uint32_t '+function('pmap_vm_prot_to_page_flags')
flush='void '+function('pmap_flush_tlbs')
out=repo/'out'/('user-tlb-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
test=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned vm_prot_t,pt_entry_t,vm_map_offset_t,pmap_paddr_t;
typedef int boolean_t,arm_l2_t;
struct pmap {uint32_t pm_l1_phys,pm_asid;int pm_shared;};typedef struct pmap*pmap_t;
static struct pmap kernel={0x40100000,0,0},user={0x40200000,17,0},other={0x40300000,18,0};
static pmap_t kernel_pmap=&kernel;static arm_l2_t arm_pte_prot_templates[1];
static unsigned current_ttbr=0x40200013,real_ncpus=1,processor_avail_count=1,pmap_asid_ncpus=1;
struct cpu {int cpu_running;};static struct cpu cpu={1};
#define FALSE 0
#define TRUE 1
#define L2_ACCESS_USER 0x20
#define L2_ACCESS_PRW 0x10
#define L2_ACCESS_APX 0x200
#define L2_NX_BIT 1
#define L2_NG_BIT 0x800
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define __improbable(x) (x)
static unsigned cpu_number(void){return 0;}
static struct cpu*cpu_datap(unsigned n){assert(n==0);return &cpu;}
static unsigned armreg_ttbr_read(void){return current_ttbr;}
static int ml_get_interrupts_enabled(void){return 1;}
static unsigned get_preemption_level(void){return 1;}
static void panic(const char*s){(void)s;assert(!"unexpected panic");}
struct entry {uint32_t va,pa,asid;unsigned global,valid;};
static struct entry tlb[3];static unsigned all_flushes,asid_flushes,invalidations,validations;
static void pmap_asid_invalidate_all_cpus(pmap_t p){assert(p);invalidations++;}
static void pmap_asid_validate_cpu(pmap_t p,unsigned n){assert(p&&n==0);validations++;}
static void arm_tlb_flushID(void){all_flushes++;for(unsigned n=0;n<3;n++)tlb[n].valid=0;}
static void arm_tlb_flushID_ASID(unsigned asid){
 asid_flushes++;for(unsigned n=0;n<3;n++)if(tlb[n].valid&&!tlb[n].global&&tlb[n].asid==asid)tlb[n].valid=0;
}
''' + flags + '\n' + flush + r'''
int main(void){
 for(unsigned prot=0;prot<8;prot++)for(int wired=0;wired<2;wired++)for(int nx=0;nx<2;nx++){
  uint32_t expected=L2_ACCESS_PRW|((prot&2)?0:L2_ACCESS_APX)|((prot&4)?0:L2_NX_BIT);
  assert(pmap_vm_prot_to_page_flags(&kernel,prot,wired,nx)==expected);
  assert(pmap_vm_prot_to_page_flags(&user,prot,wired,nx)==(expected|L2_ACCESS_USER|(BOARD_CONFIG_EXYNOS4210_I9100?L2_NG_BIT:0)));
 }
 uint32_t f=pmap_vm_prot_to_page_flags(&user,3,0,0);
 tlb[0]=(struct entry){0xbe000,0x52000000,17,!(f&L2_NG_BIT),1};
 tlb[1]=(struct entry){0xbe000,0x52100000,18,0,1};
 tlb[2]=(struct entry){0x80000000,0x40000000,0,1,1};
 /* Software deletes/replaces the user PTE. The actual production flush
  * chooses TLBIASID; the architecture model excludes global entries. */
 pmap_flush_tlbs(&user,0xbe000,0xbf000);
 assert(asid_flushes==1&&all_flushes==0&&invalidations==1&&validations==1);
 assert(tlb[1].valid&&tlb[2].valid);
 if(BOARD_CONFIG_EXYNOS4210_I9100){
  assert(!tlb[0].valid);puts("PASS fixed user PTE: stale alias removed; kernel global and other ASID preserved");
 }else{
  assert(tlb[0].valid&&tlb[0].pa==0x52000000);puts("PASS old control: global user alias survives the exact production ASID invalidation");
 }
 pmap_flush_tlbs(&other,0xbe000,0xbf000);assert(asid_flushes==1&&all_flushes==0&&invalidations==2);
 pmap_flush_tlbs(&kernel,0x80000000,0x80001000);assert(all_flushes==1&&!tlb[1].valid&&!tlb[2].valid);
 user.pm_shared=1;pmap_flush_tlbs(&user,0xbe000,0xbf000);assert(all_flushes==2);
 puts("PASS production permission bits, private/shared/kernel flush selection and foreign-platform control");
}
'''
(out/'check.c').write_text(test)
for target in (0,1):
    executable=out/('check-'+str(target))
    subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-DBOARD_CONFIG_EXYNOS4210_I9100='+str(target),out/'check.c','-o',executable],check=True)
    subprocess.run([executable],check=True)
(out/'result.json').write_text(json.dumps(dict(result='PASS',pmap_sha256=hashlib.sha256((repo/'osfmk/arm/pmap.c').read_bytes()).hexdigest(),
        production_pte_and_flush_functions=True,old_global_alias_survives=True,fixed_user_asid_alias_removed=True,
        permission_bits_unchanged=True,tlb_model_limit='TLBIASID matching is modeled; physical timing and cache coherence require the phone'),indent=2)+'\n')
print('EVIDENCE',out)
