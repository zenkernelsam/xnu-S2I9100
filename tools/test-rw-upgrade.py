#!/usr/bin/env python3
"""Execute the real ARM upgrade fast/dispatch path with controlled contention."""
import os
from pathlib import Path
import subprocess
import sys

if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1]
out=repo/'out/one-shot/rw-upgrade';out.mkdir(parents=True,exist_ok=True)
prefix=r'''
.syntax unified
.arm
.text
.global start
start:
    ldr sp, =0x41000000
    ldr r0, =word
    ldr r1, =0x18000
    str r1, [r0]
    bl _lck_rw_lock_shared_to_exclusive
    cmp r0, #1
    bne failed
    ldr r0, =word
    ldr r1, [r0]
    ldr r2, =0x8004
    cmp r1, r2
    bne failed
    ldr r1, =0x28000
    str r1, [r0]
    bl _lck_rw_lock_shared_to_exclusive
    cmp r0, #1
    bne failed
    ldr r0, =marker
    ldr r0, [r0]
    cmp r0, #2
    bne failed
    mov r0, #4
    ldr r1, =pass_text
    svc 0x123456
    mov r0, #0x18
    ldr r1, =0x20026
    svc 0x123456
failed:
    mov r0, #4
    ldr r1, =fail_text
    svc 0x123456
    mov r0, #0x18
    ldr r1, =0x20023
    svc 0x123456
_lck_rw_lock_shared_gen:
    mov r2, #1
    b mark
_lck_rw_lock_shared_to_exclusive_gen:
    ldr r1, [r0]
    ldr r2, =0x28000
    cmp r1, r2
    bne failed
    mov r2, #2
mark:
    ldr r1, =marker
    str r2, [r1]
    mov r0, #1
    bx lr
rwlsepanic:
    b failed
#define EnterARM(name) .global _##name; _##name:
#define LoadConstantToReg(value,reg) ldr reg, =value
#define LOAD_ADDR(reg,name) ldr reg, =_##name
'''
suffix=r'''
.ltorg
.data
.align 2
word: .word 0
marker: .word 0
pass_text: .asciz "PASS ARM reader upgrade fast and contended dispatch\n"
fail_text: .asciz "FAIL ARM reader upgrade slow dispatch\n"
'''
old=subprocess.check_output(['git','show','4121f2b:osfmk/arm/hw_lock.s'],cwd=repo,text=True)
new=(repo/'osfmk/arm/hw_lock.s').read_text()
for label,source,expect in [('before',old,1),('after',new,0)]:
    body=source.split('EnterARM(lock_read_to_write)',1)[1].split('rwlsepanic:',1)[0]
    asm=out/(label+'.S');asm.write_text(prefix+'EnterARM(lock_read_to_write)'+body+suffix)
    elf=out/(label+'.elf')
    subprocess.run(['arm-none-eabi-gcc','-march=armv7-a','-marm','-nostdlib',
        '-Wl,-Ttext=0x40008000','-Wl,-e,start',str(asm),'-o',str(elf)],check=True)
    result=subprocess.run(['timeout','5s','qemu-system-arm','-M','smdkc210','-smp','2',
        '-m','1024M','-display','none','-serial','null','-monitor','none',
        '-semihosting-config','enable=on,target=native','-device',f'loader,file={elf},cpu-num=0'],
        capture_output=True,text=True)
    (out/(label+'.log')).write_text(result.stdout+result.stderr)
    print(label, result.returncode, (result.stdout+result.stderr).strip(),flush=True)
    assert result.returncode==expect
print('PASS: old dispatch fails, corrected ARM dispatch passes')
