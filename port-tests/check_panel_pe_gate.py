#!/usr/bin/env python3
"""Run the real PE power wrapper against production panel/blank state code."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import hashlib,importlib.util,json,subprocess
repo=Path(__file__).resolve().parents[1];path=repo/'pexpert/arm/pe_exynos4210_i9100.c';text=path.read_text()
start=text.index('int ios7leo_display_power(uint32_t state)');begin=text.index('{',start);end=begin+1;depth=1
while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
blocked_start=text.index('unsigned ios7i9100_display_refresh_blocked(void)');blocked_begin=text.index('{',blocked_start)
blocked_end=blocked_begin+1;depth=1
while depth:depth+=(text[blocked_end]=='{')-(text[blocked_end]=='}');blocked_end+=1
out=repo/'out'/('panel-pe-gate-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_panel_sleep.h"
#include "i9100_display_power.h"
static unsigned i9100_fimd_map=1,i9100_panel_gpio=1,processor_avail_count=1,irq,interrupts=1;
static unsigned i9100_panel_sleep_enabled=1,mode=0x9c,fail_mode,commands,delay_ms,map_writes;
static uint32_t scanout=0x1003,mapping=0x80000000U;
static struct i9100_panel_sleep i9100_panel_sleep;
static struct i9100_display_power_state i9100_display_power_state;
static volatile uint32_t i9100_display_power_guard,i9100_display_power_diag[12];
static int ml_at_interrupt_context(void){return irq;}static int ml_get_interrupts_enabled(void){return interrupts;}
static uint32_t i9100_display_map_read(void*c){(void)c;return mapping;}
static void i9100_display_map_write(void*c,uint32_t value){(void)c;++map_writes;mapping=value;}
static void i9100_display_map_sync(void*c){(void)c;}
static unsigned publishes;static void i9100_panel_diag_publish(void){++publishes;}
static int read_byte(void*c,unsigned reg,uint8_t*value){(void)c;
 *value=reg==0xda?0xa1:reg==0xdb?0x12:reg==0xdc?0x11:(uint8_t)mode;return 1;}
static int command(void*c,unsigned cmd){(void)c;++commands;
 if(!fail_mode){if(cmd==0x28)mode&=~4U;else if(cmd==0x10)mode&=~0x10U;else if(cmd==0x11)mode|=0x10U;else if(cmd==0x29)mode|=4U;}return 1;}
static uint32_t scan_read(void*c){(void)c;return scanout;}
static void scan_write(void*c,uint32_t value){(void)c;scanout=value;}
static void wait_ms(void*c,unsigned ms){(void)c;assert(!irq&&interrupts);delay_ms+=ms;}
static void reset(void){memset(&i9100_panel_sleep,0,sizeof(i9100_panel_sleep));memset(&i9100_display_power_state,0,sizeof(i9100_display_power_state));
 i9100_panel_sleep.io=(struct i9100_panel_sleep_io){0,read_byte,command,scan_read,scan_write,wait_ms};
 mode=0x9c;fail_mode=commands=delay_ms=map_writes=irq=0;interrupts=processor_avail_count=1;
 mapping=0x80000000U;scanout=0x1003;i9100_display_power_guard=0;i9100_panel_sleep_enabled=1;}
'''+text[blocked_start:blocked_end]+'\n'+text[start:end]+r'''
int main(void){reset();assert(!ios7leo_display_power(1)&&i9100_panel_sleep.qualified&&!commands);
 assert(!ios7leo_display_power(0)&&i9100_panel_sleep.stage==I9P_SLEEP&&i9100_display_power_state.blanked&&scanout==0x1000);
 assert(ios7i9100_display_refresh_blocked());
 assert(!ios7leo_display_power(1)&&i9100_panel_sleep.stage==I9P_AWAKE&&!i9100_display_power_state.blanked&&mapping==0x80000000U);
 assert(!ios7i9100_display_refresh_blocked());
 for(unsigned scenario=0;scenario<3;scenario++){reset();if(scenario==0)irq=1;if(scenario==1)interrupts=0;if(scenario==2)processor_avail_count=2;
  assert(ios7leo_display_power(0)==1&&!commands&&!map_writes&&!i9100_display_power_guard);}
 reset();fail_mode=1;assert(ios7leo_display_power(0)==1&&i9100_display_power_state.blanked&&i9100_panel_sleep.stage==I9P_FAULT);
 assert(ios7i9100_display_refresh_blocked());
 unsigned count=commands,writes=map_writes;assert(ios7leo_display_power(1)==1&&commands==count&&map_writes==writes);
 reset();i9100_panel_sleep_enabled=0;assert(!ios7leo_display_power(0)&&!commands&&i9100_display_power_state.blanked);
 assert(ios7i9100_display_refresh_blocked());
 assert(!ios7leo_display_power(1)&&!commands&&!i9100_display_power_state.blanked);
 assert(!ios7i9100_display_refresh_blocked());
 reset();i9100_display_power_guard=1;assert(ios7leo_display_power(0)==2&&!commands&&!map_writes);
 puts("PASS actual PE wrapper: explicit panel mode, legacy path, IRQ/SMP refusal, preserved black fault, no blind unblank, successful qualified sleep/wake");}
'''
(out/'model.c').write_text(code)
for n,cmd in enumerate((['clang','-std=c99','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-Wno-int-to-void-pointer-cast',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(repo/'pexpert/arm'),str(out/'model.c'),'-o',str(out/'model')],[str(out/'model')])):
 result=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(result.stdout+result.stderr)
 if result.returncode:print((result.stdout+result.stderr)[-3000:],file=sys.stderr);result.check_returncode()
 if result.stdout:print(result.stdout.strip(),flush=True)
spec=importlib.util.spec_from_file_location('panel_host',repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py')
host=importlib.util.module_from_spec(spec);spec.loader.exec_module(host)
import struct
raw=struct.pack('<16I',1,2,1,1,3,0,0,0,2,1,0x1003,0x1000,0xa1,0x12,0x11,0x3f)
assert host.decode_panel_sleep(raw,raw[:8])['consistent']
assert host.decode_panel_sleep(raw,raw[:8])['identity_reads']==0x3f
assert not host.decode_panel_sleep(raw,struct.pack('<2I',1,4))['consistent']
bad=bytearray(raw);struct.pack_into('<I',bad,60,0x80)
try:host.decode_panel_sleep(bad)
except ValueError:pass
else:raise AssertionError('Malformed ID-read mask accepted')
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
 limit='Actual PE wrapper/production sleep state; clock/MMIO/transport model, no hardware energy or display evidence'),indent=2)+'\n')
print('EVIDENCE',out,flush=True)
