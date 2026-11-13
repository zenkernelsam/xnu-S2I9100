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
repo=Path(__file__).resolve().parents[1];root=repo.parent
out=repo/'out'/('audio-property-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
header=(repo/'bsd/dev/arm/i9100_audio_property_lock.h').read_text()
body='\n'.join(line for line in header.splitlines() if not line.startswith('#include'))
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UInt32,user_addr_t,vm_map_offset_t,vm_offset_t;typedef int kern_return_t;
typedef uint32_t vm_map_size_t;typedef uint64_t vm_object_offset_t;
typedef void*vm_object_t;typedef int boolean_t,vm_prot_t,vm_inherit_t;
struct map {unsigned min_offset;};typedef struct map*vm_map_t;
struct task {unsigned thread_count;};typedef struct task*task_t;struct proc {const char*p_comm;unsigned p_pid;void*task;};typedef struct proc*proc_t;
typedef struct {uint32_t cpsr,pc;} arm_saved_state_t;
static unsigned abi=6,enabled=1,irq,preemption,interrupts=1,checks,protects,writes,fail_at,fail_write,reads,target_cur,target_max,site_cur,site_max;
static struct map map={4096};static unsigned char site[24],target[256],runtime_data[64];
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
int get_task_numacts(task_t t){return t->thread_count;}
static int copyin(user_addr_t at,void*out,unsigned size){reads++;if(at!=0x3179e848 || size!=24)return 14;memcpy(out,site,size);return 0;}
static int copyout(const void*in,user_addr_t at,unsigned size){writes++;if(writes==fail_write)return 14;
 if(at==0x10000 && size==256)memcpy(target,in,size);else if(at==0x20000 && size==64)memcpy(runtime_data,in,size);else if(at==0x3179e848 && size==8)memcpy(site,in,size);else return 14;return 0;}
int vm_map_enter(vm_map_t m,vm_map_offset_t*a,unsigned n,unsigned mask,int flags,void*o,uint64_t off,int copy,int cur,int max,int inh){
 if(m!=&map || *a!=4096 || n!=4096 || mask || flags!=VM_FLAGS_ANYWHERE || o || off || copy || cur!=3 || (max!=7 && max!=3) || inh!=VM_INHERIT_COPY)abort();
 *a=max==7?0x10000:0x20000;if(max==7){target_cur=cur;target_max=max;}return fail_at==1?2:0;}
static int vm_map_protect(vm_map_t m,unsigned a,unsigned e,unsigned prot,unsigned max){
 (void)m;(void)e;protects++;if(fail_at==protects+1)return 2;
 unsigned*cur=a==0x10000?&target_cur:&site_cur;unsigned*cap=a==0x10000?&target_max:&site_max;
 if(prot&VM_PROT_COPY)*cap|=VM_PROT_WRITE;
 if((prot&~VM_PROT_COPY)&~*cap)return 2;
 if(max){*cap=prot&~VM_PROT_COPY;*cur&=*cap;}else *cur=prot&~VM_PROT_COPY;
 return 0;
}
static void flush_dcache(unsigned a,unsigned n,unsigned phys){(void)a;(void)n;(void)phys;}
static void invalidate_icache(unsigned a,unsigned n,unsigned phys){(void)a;(void)n;(void)phys;}
static void check(int ok,const char*why){checks++;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
'''
suffix=r'''
static void reset(void){memset((void*)i9100_audio_property_lock_diag,0,sizeof(i9100_audio_property_lock_diag));memset((void*)i9100_audio_property_lock_history,0,sizeof(i9100_audio_property_lock_history));memcpy(site,i9100_audio_property_original,24);reads=protects=writes=0;fail_at=fail_write=0;target_cur=0;target_max=0;site_cur=site_max=VM_PROT_READ|VM_PROT_EXECUTE;}
int main(void){struct task t={1};struct proc p={"mediaserverd",33,&t};arm_saved_state_t s={0x10,0x393cdc34};
 reset();i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==2,"installation complete");
 check(i9100_audio_property_lock_diag[8] && !i9100_audio_property_lock_diag[9] && protects==5,"RX restoration succeeded");
 check(target_cur==5 && target_max==5 && site_cur==5 && site_max==5,"both pages execute with maximum RX under actual protection intersection semantics");
 check(!memcmp(site,"\xdf\xf8\x00\xf0",4) && *(uint32_t*)(site+4)==0x10001,"literal branch points to private target");
 check(*(uint32_t*)(target+240)==0x39317ec1 && *(uint32_t*)(target+244)==0x39315fa1 && *(uint32_t*)(target+248)==0x3179e85d,"all cache literals use actual slide");
 check(*(uint32_t*)(target+252)==0x20000 && i9100_audio_property_runtime[1]==33 && !memcmp(runtime_data,(unsigned char[64]){0},64),"data-only runtime starts zero and belongs to installed PID");
 unsigned saved=writes;i9100_audio_property_lock_prepare(&p,372,&s);check(writes==saved,"no second installation");
 memcpy(site,i9100_audio_property_original,24);site_cur=site_max=5;p.p_pid=78;
 i9100_audio_property_lock_prepare(&p,372,&s);
 check(i9100_audio_property_lock_diag[0]==2 && i9100_audio_property_lock_diag[1]==78 && i9100_audio_property_lock_history[0]==2,"new process gets its own installation");
 check(i9100_audio_property_lock_history[2]==33 && i9100_audio_property_lock_history[14]==78,"old and new owners retained separately");p.p_pid=33;
 reset();p.p_comm="SpringBoard";i9100_audio_property_lock_prepare(&p,372,&s);check(!reads&&!writes,"process scope");p.p_comm="mediaserverd";
 reset();t.thread_count=2;i9100_audio_property_lock_prepare(&p,372,&s);check(!reads&&!writes,"only before worker creation");t.thread_count=1;
 reset();abi=7;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"ABI scope");abi=6;
 reset();enabled=0;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"diagnostic switch");enabled=1;
 reset();irq=1;i9100_audio_property_lock_prepare(&p,372,&s);check(!reads,"IRQ cannot copy or change VM");irq=0;
 reset();s.pc++;i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==2,"Thumb bit normalization");s.pc--;
 reset();s.pc+=2;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"unrelated syscall point rejected");s.pc-=2;
 reset();site[0]^=1;i9100_audio_property_lock_prepare(&p,372,&s);check(!writes,"unknown original image rejected");
 reset();fail_at=1;i9100_audio_property_lock_prepare(&p,372,&s);check(i9100_audio_property_lock_diag[0]==3&&!writes,"VM allocation failure preserved");
 reset();fail_write=3;i9100_audio_property_lock_prepare(&p,372,&s);check(!memcmp(site,i9100_audio_property_original,8)&&protects==5&&site_cur==5&&site_max==5&&i9100_audio_property_lock_diag[0]==3,"failed site write restores bytes and RX");
 printf("%u production install gate/failure checks passed; VM and copy modeled\n",checks);}
'''
(out/'check.c').write_text(prefix+body+suffix)
subprocess.run(['clang','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
subprocess.run(['clang','--target=armv7-none-eabi','-c',str(repo/'port-tests/audio-property-lock-trampoline.s'),'-o',str(out/'trampoline.o')],check=True)
b=(out/'trampoline.o').read_bytes();off=struct.unpack_from('<I',b,32)[0];n,num,strings=struct.unpack_from('<3H',b,46);sh=[struct.unpack_from('<10I',b,off+i*n) for i in range(num)];st=b[sh[strings][4]:sh[strings][4]+sh[strings][5]]
code=next(b[h[4]:h[4]+h[5]] for h in sh if st[h[0]:st.find(b'\0',h[0])]==b'.text')
numbers=bytes(int(x,16) for x in re.search(r'i9100_audio_property_code\[256\]=\{(.*?)\};',header,re.S)[1].replace('\n','').split(',') if x.strip())
assert code==numbers
sys.path.insert(0,str(root/'assets/py-native-hid'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
results=[]
for depth in (1,2,3,8,16):
 cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB);cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
 cpu.mem_map(0x10000000,0x20000);cpu.mem_write(0x10000000,code[:-4]+struct.pack("<I",0x10003000))
 for page in [0x39217000,0x39215000,0x3169e000,0x10020000]:cpu.mem_map(page,4096)
 SP=0x1001f000;OWNER=0x10001000;QUERY=0x10020000;MUTEX=0x10002000
 cpu.mem_write(OWNER+12,struct.pack('<I',MUTEX));cpu.mem_write(OWNER+208,struct.pack('<I',77))
 cpu.mem_write(MUTEX,struct.pack('<3I',0x4d555458,0,(depth<<16)|0x2068))
 cpu.reg_write(UC_ARM_REG_SP,SP);cpu.reg_write(UC_ARM_REG_R5,OWNER);cpu.reg_write(UC_ARM_REG_R6,QUERY|1)
 cpu.reg_write(UC_ARM_REG_R8,0x8888);cpu.reg_write(UC_ARM_REG_R10,0xaaaa)
 cpu.reg_write(UC_ARM_REG_R0,8);cpu.reg_write(UC_ARM_REG_R1,9);cpu.reg_write(UC_ARM_REG_R2,10);cpu.reg_write(UC_ARM_REG_R3,11)
 events=[];held=[depth]
 def hook(uc,at,size,unused):
  if at==0x3169e85c:uc.emu_stop();return
  if at==0x39217ec0:
   assert uc.reg_read(UC_ARM_REG_R0)==MUTEX and held[0]>0
   held[0]-=1;events.append('unlock');uc.reg_write(UC_ARM_REG_R0,0);uc.reg_write(UC_ARM_REG_R9,0xdeadbeef)
  elif at==0x39215fa0:
   assert uc.reg_read(UC_ARM_REG_R0)==MUTEX
   held[0]+=1;events.append('lock');uc.reg_write(UC_ARM_REG_R0,0);uc.reg_write(UC_ARM_REG_R9,0xdeadbeef)
  elif at==QUERY:
   assert held[0]==0 and events==['unlock']*depth
   assert [uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]==[77,SP+12,0,0]
   query_sp=uc.reg_read(UC_ARM_REG_SP)
   assert query_sp%8==0 and struct.unpack('<2I',uc.mem_read(query_sp,8))==(16,SP+24)
   events.append('real-query');uc.reg_write(UC_ARM_REG_R0,0x1234)
  else:return
  uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 cpu.hook_add(UC_HOOK_CODE,hook);cpu.emu_start(0x10000001,0x3169e85c,count=1200)
 assert cpu.reg_read(UC_ARM_REG_PC)==0x3169e85c and cpu.reg_read(UC_ARM_REG_SP)==SP
 assert cpu.reg_read(UC_ARM_REG_R0)==0x1234 and held[0]==depth
 assert cpu.reg_read(UC_ARM_REG_R8)==0x8888 and cpu.reg_read(UC_ARM_REG_R10)==0xaaaa
 assert events==['unlock']*depth+['real-query']+['lock']*depth
 runtime=struct.unpack('<16I',cpu.mem_read(0x10003000,64))
 assert runtime[:9]==(1,4,MUTEX,0x4d555458,(depth<<16)|0x2068,depth,0,depth,0x1234)
 results.append(dict(depth=depth,events=events,held_after=held[0],original_result_preserved=True,runtime=list(runtime)))
native_counts=[]
with (root/'assets/ios6-extract/dyld_cache6_armv7').open('rb') as f:
 with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as cache:
  mo,mc=struct.unpack_from('<2I',cache,16)
  maps=[struct.unpack_from('<QQQII',cache,mo+n*32) for n in range(mc)]
  def original(address,size):
   va,length,offset,_,_=next(row for row in maps if row[0]<=address and address+size<=row[0]+row[1])
   return bytes(cache[offset+address-va:offset+address-va+size])
  for depth in (2,3,8,16):
   uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
   uc.mem_map(0x10000000,0x20000);uc.mem_map(0x39245000,4096)
   uc.mem_write(0x39245000,original(0x39245000,4096))
   mutex=0x10002000;flags=(depth<<16)|0x2068
   uc.mem_write(mutex+8,struct.pack('<I',flags));uc.mem_write(mutex+32,struct.pack('<Q',112))
   uc.reg_write(UC_ARM_REG_R5,mutex);uc.reg_write(UC_ARM_REG_R8,flags)
   uc.reg_write(UC_ARM_REG_SP,0x1001f000);uc.reg_write(UC_ARM_REG_R7,0x1001f080)
   reached=[]
   def native_stop(uc,at,size,unused):
    if at==0x39245a8c:reached.append(at);uc.emu_stop()
   uc.hook_add(UC_HOOK_CODE,native_stop)
   uc.emu_start(0x39245921,0x39245a8c,count=32)
   count=struct.unpack('<I',uc.mem_read(mutex+8,4))[0]>>16
   owner=struct.unpack('<Q',uc.mem_read(mutex+32,8))[0]
   assert count==depth-1 and owner==112 and uc.reg_read(UC_ARM_REG_PC)==0x39245a8c
   native_counts.append(dict(operation='original recursive drop branch',depth=depth,
       count_after=count,owner_after=owner,full_release_not_reached=True))
  for depth in (1,2,3,8):
   uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
   uc.mem_map(0x10000000,0x20000);uc.mem_map(0x39216000,4096)
   uc.mem_write(0x39216000,original(0x39216000,4096));mutex=0x10002000
   flags=(depth<<16)|0x2068;uc.mem_write(mutex+8,struct.pack('<I',flags))
   uc.mem_write(mutex+32,struct.pack('<Q',112));uc.mem_write(0x1001f008,struct.pack('<Q',112))
   uc.reg_write(UC_ARM_REG_R0,flags);uc.reg_write(UC_ARM_REG_R1,2)
   uc.reg_write(UC_ARM_REG_R11,mutex);uc.reg_write(UC_ARM_REG_SP,0x1001f000)
   def native_stop(uc,at,size,unused):
    if at==0x39216162:uc.emu_stop()
   uc.hook_add(UC_HOOK_CODE,native_stop);uc.emu_start(0x3921608f,0x39216162,count=40)
   count=struct.unpack('<I',uc.mem_read(mutex+8,4))[0]>>16
   assert count==depth+1 and uc.reg_read(UC_ARM_REG_PC)==0x39216162
   native_counts.append(dict(operation='original recursive owner lock branch',depth=depth,count_after=count))
result=dict(passed=True,scope=__doc__,recursive_depth_cases=results,
            original_recursive_instruction_cases=native_counts,phone_operations=0)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));print('EVIDENCE',out)
