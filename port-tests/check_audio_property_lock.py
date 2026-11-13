#!/usr/bin/env python3
"""Production install gates and original ARM query/trampoline checks.

VM/COW protection, copyin/out, task lifetime and imported lock/query functions
are modeled. No phone or full operating-system execution claim.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import subprocess,struct,re,json,importlib.util,mmap
repo=Path(__file__).resolve().parents[1];root=repo.parents[1]
out=repo/'out'/('audio-property-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
header=(repo/'bsd/dev/arm/i9100_audio_property_lock.h').read_text()
body='\n'.join(line for line in header.splitlines() if not line.startswith('#include'))
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t,vm_map_offset_t,vm_offset_t;typedef int kern_return_t;
struct map {unsigned min_offset;};typedef struct map*vm_map_t;
struct task {unsigned thread_count;};typedef struct task*task_t;struct proc {const char*p_comm;unsigned p_pid;void*task;};typedef struct proc*proc_t;
typedef struct {uint32_t cpsr,pc;} arm_saved_state_t;
static unsigned abi=6,enabled=1,irq,preemption,interrupts=1,checks,protects,writes,fail_at,fail_write,reads;
static struct map map={4096};static unsigned char site[24],target[68];
#define FALSE 0
#define TRUE 1
#define KERN_SUCCESS 0
#define VM_FLAGS_ANYWHERE 1
#define VM_OBJECT_NULL 0
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
#define VM_PROT_ALL 7
#define VM_PROT_COPY 16
#define VM_INHERIT_COPY 1
#define vm_map_min(m) ((m)->min_offset)
#define OSMemoryBarrier() __sync_synchronize()
static int ml_at_interrupt_context(void){return irq;}static int get_preemption_level(void){return preemption;}
static int ml_get_interrupts_enabled(void){return interrupts;}
static int PE_parse_boot_argn(const char*n,void*out,unsigned size){(void)size;*(int*)out=!strcmp(n,"i9100hidabi")?abi:enabled;return 1;}
static int OSCompareAndSwap(unsigned old,unsigned v,volatile uint32_t*p){if(*p!=old)return 0;*p=v;return 1;}
static vm_map_t current_map(void){return &map;}
static int copyin(user_addr_t at,void*out,unsigned size){reads++;if(at!=0x3179e848 || size!=24)return 14;memcpy(out,site,size);return 0;}
static int copyout(const void*in,user_addr_t at,unsigned size){writes++;if(writes==fail_write)return 14;
 if(at==0x10000 && size==68)memcpy(target,in,size);else if(at==0x3179e848 && size==8)memcpy(site,in,size);else return 14;return 0;}
static int vm_map_enter(vm_map_t m,vm_map_offset_t*a,unsigned n,unsigned mask,unsigned flags,void*o,unsigned off,unsigned copy,unsigned cur,unsigned max,unsigned inh){(void)m;(void)n;(void)mask;(void)flags;(void)o;(void)off;(void)copy;(void)cur;(void)max;(void)inh;*a=0x10000;return fail_at==1?2:0;}
static int vm_map_protect(vm_map_t m,unsigned a,unsigned e,unsigned prot,unsigned max){(void)m;(void)a;(void)e;(void)prot;(void)max;protects++;return fail_at==protects+1?2:0;}
static void flush_dcache(unsigned a,unsigned n,unsigned phys){(void)a;(void)n;(void)phys;}
static void invalidate_icache(unsigned a,unsigned n,unsigned phys){(void)a;(void)n;(void)phys;}
static void check(int ok,const char*why){checks++;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
'''
suffix=r'''
static void reset(void){memset((void*)i9100_audio_property_lock_diag,0,sizeof(i9100_audio_property_lock_diag));memcpy(site,i9100_audio_property_original,24);reads=protects=writes=0;fail_at=fail_write=0;}
int main(void){struct task t={1};struct proc p={"mediaserverd",33,&t};arm_saved_state_t s={0x10,0x393cdc34};
 reset();i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==2,"installation complete");
 check(i9100_audio_property_lock_diag[8] && !i9100_audio_property_lock_diag[9] && protects==3,"RX restoration succeeded");
 check(!memcmp(site,"\xdf\xf8\x00\xf0",4) && *(uint32_t*)(site+4)==0x10001,"literal branch points to private target");
 check(*(uint32_t*)(target+56)==0x39317ec1 && *(uint32_t*)(target+60)==0x39315fa1 && *(uint32_t*)(target+64)==0x3179e85d,"all cache literals use actual slide");
 unsigned saved=writes;i9100_audio_property_lock_prepare(&p,372,&s);check(writes==saved,"no second installation");
 reset();p.p_comm="SpringBoard";i9100_audio_property_lock_prepare(&p,372,&s);check(!reads&&!writes,"process scope");p.p_comm="mediaserverd";
 reset();t.thread_count=2;i9100_audio_property_lock_prepare(&p,372,&s);check(!reads&&!writes,"only before worker creation");t.thread_count=1;
 reset();abi=7;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"ABI scope");abi=6;
 reset();enabled=0;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"diagnostic switch");enabled=1;
 reset();irq=1;i9100_audio_property_lock_prepare(&p,372,&s);check(!reads,"IRQ cannot copy or change VM");irq=0;
 reset();s.pc++;i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==2,"Thumb bit normalization");s.pc--;
 reset();s.pc+=2;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"unrelated syscall point rejected");s.pc-=2;
 reset();site[0]^=1;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"unknown original image rejected");
 reset();fail_at=1;i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==3&&!writes,"VM allocation failure preserved");
 reset();fail_write=2;i9100_audio_property_lock_prepare(&p,372,&s);check(!memcmp(site,i9100_audio_property_original,8)&&protects==3&&i9100_audio_property_lock_diag[0]==3,"failed site write restores bytes and RX");
 printf("%u production install gate/failure checks passed; VM and copy modeled\n",checks);}
'''
(out/'check.c').write_text(prefix+body+suffix)
subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
subprocess.run(['clang','--target=armv7-none-eabi','-c',str(repo/'port-tests/audio-property-lock-trampoline.s'),'-o',str(out/'trampoline.o')],check=True)
b=(out/'trampoline.o').read_bytes();off=struct.unpack_from('<I',b,32)[0];n,num,strings=struct.unpack_from('<3H',b,46);sh=[struct.unpack_from('<10I',b,off+i*n) for i in range(num)];st=b[sh[strings][4]:sh[strings][4]+sh[strings][5]]
code=next(b[h[4]:h[4]+h[5]] for h in sh if st[h[0]:st.find(b'\0',h[0])]==b'.text')
numbers=bytes(int(x,16) for x in re.search(r'i9100_audio_property_code\[68\]=\{(.*?)\};',header,re.S)[1].replace('\n','').split(',') if x.strip())
assert code==numbers
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB);cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
cpu.mem_map(0x10000000,0x20000);cpu.mem_write(0x10000000,code)
for page in [0x39217000,0x39215000,0x3169e000,0x10020000]:cpu.mem_map(page,4096)
SP=0x1001f000;OWNER=0x10001000;QUERY=0x10020000
cpu.mem_write(OWNER+12,struct.pack('<I',0x10002000));cpu.mem_write(OWNER+208,struct.pack('<I',77))
cpu.reg_write(UC_ARM_REG_SP,SP);cpu.reg_write(UC_ARM_REG_R5,OWNER);cpu.reg_write(UC_ARM_REG_R6,QUERY|1)
cpu.reg_write(UC_ARM_REG_R0,8);cpu.reg_write(UC_ARM_REG_R1,9);cpu.reg_write(UC_ARM_REG_R2,10);cpu.reg_write(UC_ARM_REG_R3,11)
events=[]
def hook(uc,at,size,unused):
 if at==0x3169e85c:uc.emu_stop();return
 if at in (0x39217ec0,0x39215fa0):
  assert uc.reg_read(UC_ARM_REG_R0)==0x10002000;events.append('unlock' if at==0x39217ec0 else 'lock');uc.reg_write(UC_ARM_REG_R0,0)
 elif at==QUERY:
  assert events==['unlock'];assert [uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]==[77,SP+12,0,0]
  assert struct.unpack('<2I',uc.mem_read(SP,8))==(16,SP+24);events.append('real-query');uc.reg_write(UC_ARM_REG_R0,0x1234)
 else:return
 uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
cpu.hook_add(UC_HOOK_CODE,hook);cpu.emu_start(0x10000001,0x3169e85c,count=500)
assert cpu.reg_read(UC_ARM_REG_PC)==0x3169e85c and cpu.reg_read(UC_ARM_REG_SP)==SP and cpu.reg_read(UC_ARM_REG_R0)==0x1234 and events==['unlock','real-query','lock']
result=dict(passed=True,scope=__doc__,events=events,original_result_preserved=True,phone_operations=0)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));print('EVIDENCE',out)
