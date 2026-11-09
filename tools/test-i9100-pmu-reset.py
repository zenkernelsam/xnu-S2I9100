#!/usr/bin/env python3
"""Validate the actual PE callback and PMU sequence without touching hardware."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10',
            'ionice', '-c2', '-n7', sys.executable, __file__],
            dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    out = repo / 'out' / ('pmu-reset-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    source = (repo / 'pexpert/arm/pe_exynos4210_i9100.c').read_text()
    start = source.index('static int i9100_halt_restart(')
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    callback = source[start:end].replace('__asm__ volatile("wfi")', 'test_halt()')
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
typedef uintptr_t vm_offset_t;
static unsigned count,checks,interrupts=1;
static struct {uintptr_t base;unsigned offset,value;} writes[3];
static unsigned barriers[3],nb;
static jmp_buf reset_complete;
static vm_offset_t i9100_trace_pmu;
static int i9100_os_reboot_download;
#define FALSE 0
#define kPERestartCPU 1
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
static int ml_set_interrupts_enabled(int on) {int old=interrupts;interrupts=on;return old;}
static void test_write(uintptr_t base,unsigned offset,unsigned value) {
    CHECK(interrupts==0 && count<3);writes[count].base=base;
    writes[count].offset=offset;writes[count++].value=value;
}
static void test_barrier(void) {CHECK(nb<3);barriers[nb++]=count;}
static void test_halt(void) {longjmp(reset_complete,1);}
#define I9100_PMU_RESET_WRITE(b,o,v) test_write(b,o,v)
#define I9100_PMU_RESET_BARRIER() test_barrier()
''' + '#include "' + str(repo / 'pexpert/arm/i9100_pmu_reset.h') + '"\n' + callback + r'''
int main(void) {
    i9100_trace_pmu=0;
    CHECK(i9100_halt_restart(1)<0 && count==0 && nb==0 && interrupts==1);
    i9100_trace_pmu=0xc0004000U;
    for(unsigned action=0;action<7;action++) if(action!=1)
        CHECK(i9100_halt_restart(action)<0 && count==0 && nb==0 && interrupts==1);
    for(int fallback=-1;fallback<3;fallback++) {
        count=nb=0;interrupts=1;i9100_os_reboot_download=fallback;
        if(setjmp(reset_complete)==0) {i9100_halt_restart(1);CHECK(0);}
        CHECK(count==3 && nb==3 && interrupts==0);
        CHECK(barriers[0]==0 && barriers[1]==2 && barriers[2]==3);
        CHECK(writes[0].base==i9100_trace_pmu && writes[0].offset==0x808 && writes[0].value==0x12345678U);
        CHECK(writes[1].base==i9100_trace_pmu && writes[1].offset==0x80c &&
            writes[1].value==(fallback==1?0x12345671U:0x12345670U));
        CHECK(writes[2].base==i9100_trace_pmu && writes[2].offset==0x400 && writes[2].value==1);
    }
    printf("PASS %u production PE callback/PMU ordering checks\n",checks);
    return 0;
}
'''
    (out / 'test.c').write_text(harness)
    binary = out / 'test'
    for name, command in [('build', ['gcc', '-std=c99', '-O1', '-g',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        str(out / 'test.c'), '-o', str(binary)]), ('test', [str(binary)])]:
        result = subprocess.run(command, text=True, capture_output=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        if result.stdout:print(result.stdout, end='', flush=True)
    print('Artifacts:', out, flush=True)


if __name__ == '__main__':
    main()
