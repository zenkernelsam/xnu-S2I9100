#!/usr/bin/env python3
"""Check that native charge diagnostics only read the fixed safe registers."""
import os, sys, subprocess
from pathlib import Path
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice',
        '-c2', '-n7', sys.executable, '-B', __file__],
        dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
out = repo/'out'/('charge-diag-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_charge_diag.h"
static unsigned reads, fail, writes;
static int read8(void *c, unsigned reg, uint8_t *v) {
    assert(c==(void*)1);
    assert(reg==0x10 || (reg>=0x50 && reg<=0x55) || reg==0x5a);
    if(++reads==fail)return 0; *v=(uint8_t)reg; return 1;
}
static int muic(void *c, unsigned reg, uint8_t *v) {
    assert(c==(void*)1 && (reg==0x0d || reg==0x05));
    if(++reads==fail)return 0; *v=(uint8_t)reg; return 1;
}
static int fuel(void *c, unsigned reg, uint16_t *v) {
    assert(c==(void*)1 && (reg==0x0a || reg==0x0b || reg==0x1d || reg==0x2b || reg==0x2e));
    if(++reads==fail)return 0; *v=(uint16_t)(0xf000U+reg); return 1;
}
static int write8(void *c, unsigned reg, uint8_t v) {
    (void)c;(void)reg;(void)v;writes++;return 1;
}
static uint32_t gpio(void *c, unsigned region, unsigned reg) {
    assert(c==(void*)2 && region==1 && (reg==0x100 || reg==0x104 || reg==0x108));
    return 0xabcdef00U+reg;
}
int main(void) {
    struct i9100_power_state s={0};struct i9100_power_hw hw={0};
    s.ready=s.valid=s.enabled=s.connected=s.charging=1;
    s.revision=14;s.use_sub=1;s.percent=60;s.millivolts=3859;
    s.temperature_deci_c=309;s.charge_start=1000000000ULL;
    s.io.cookie=(void*)1;s.io.fuel_read=fuel;s.io.pmic_read=read8;
    s.io.muic_read=muic;s.io.pmic_write=write8;s.io.muic_write=write8;
    hw.cookie=(void*)2;hw.read32=gpio;
    struct i9100_power_state before=s;
    i9100_charge_snapshot(0,&hw,1,31000000000ULL);
    i9100_charge_snapshot(&s,0,1,31000000000ULL);assert(!reads);
    s.ready=0;i9100_charge_snapshot(&s,&hw,1,31000000000ULL);assert(!reads);s=before;
    for(unsigned n=0;n<=15;n++) {
        reads=0;fail=n;
        i9100_charge_snapshot(&s,&hw,n+1,31000000000ULL);
        volatile uint32_t *r=&i9100_charge_diag[4+n*36];
        assert(reads==15 && writes==0 && !memcmp(&s,&before,sizeof(s)));
        assert(r[0]==n+1 && r[4]==14 && r[5]==1 && r[8]==60 && r[17]==30);
        uint32_t valid=n ? (0x7fffU & ~(1U<<(n-1))) : 0x7fffU;
        assert(r[6]==(0x80000000U|valid));
        if(n)assert(r[17+n]==0xffffffffU);
        assert(r[33]==0xabcdf000U && r[34]==0xabcdf004U && r[35]==0xabcdf008U);
    }
    fail=reads=0;i9100_charge_snapshot(&s,&hw,99,0);
    assert(i9100_charge_diag[1]==17 && i9100_charge_diag[4]==17 && i9100_charge_diag[21]==0);
    assert(!writes && !memcmp(&s,&before,sizeof(s)));
    puts("PASS fixed non-IRQ reads, 15 read failures, publication/wrap and unchanged charger policy");
}
'''
(out/'check.c').write_text(source)
header=repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'
for cmd, name in [(['clang','-std=c99','-O1','-g','-fno-pie','-no-pie',
    '-fsanitize=address,undefined','-I'+str(header),str(out/'check.c'),'-o',str(out/'check')], 'compile'),
    ([str(out/'check')], 'result')]:
    result=subprocess.run(cmd,text=True,capture_output=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    if name=='result':print(result.stdout.strip())
print('EVIDENCE',out)
