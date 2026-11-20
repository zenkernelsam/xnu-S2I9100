#!/usr/bin/env python3
"""Exercise production IRQ overlay against owned sleep/blank transitions.

Production writer/status/IRQ methods run with raw framebuffer and FIMD RAM
facades. No panel, serial, new thread, reset or flash operation is performed.
"""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',
        sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,json,re,subprocess
repo=Path(__file__).resolve().parents[1]
source=repo/'pexpert/arm/pe_exynos4210_i9100.c';text=source.read_text(encoding='utf-8')
out=repo/'out'/('perf-overlay-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
before=hashlib.sha256(source.read_bytes()).hexdigest()
def function(name):
    match=re.search(r'^(?:static )?(?:unsigned|void) '+name+r'\([^;]*?\)\s*\{',text,re.M)
    if not match:raise ValueError('Missing actual production method '+name)
    start=match.start();begin=match.end()-1;end=begin+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
methods='\n'.join(function(name) for name in ('ios7i9100_display_refresh_blocked','i9100_perf_text','i9100_status_text','ios7i9100_timer_observe'))
# The actual barrier remains a host atomic fence; no decision is rewritten.
methods=methods.replace('__asm__ volatile("dsb sy" ::: "memory");','__sync_synchronize();')
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static uint32_t screen[480*800+32],fimd[4096];
static volatile unsigned int *i9100_pixels;
static uintptr_t i9100_fimd_map;
static unsigned char iso_font[256*16];
static int i9100_perf_strip=1,i9100_panel_console_enabled;
static unsigned i9100_irq_frame_seen=1,i9100_fatal_active;
static volatile unsigned i9100_live_irq_pc,i9100_live_irq_count;
static unsigned gpu_writes,pc_samples;
static unsigned i9100_power_valid=1,i9100_power_percent=50,i9100_power_mv=4000;
static unsigned i9100_power_connected=1,i9100_power_charging=1,i9100_power_revision_value=14;
static int i9100_power_temp=250,i9100_power_reason;
static unsigned i9100_display_power_guard,i9100_panel_sleep_enabled;
static struct {unsigned blanked;} i9100_display_power_state;
enum {I9P_UNQUALIFIED=0,I9P_AWAKE=1,I9P_SLEEP=3,I9P_FAULT=4};
static struct {unsigned qualified,stage;} i9100_panel_sleep;
static unsigned i9100_irq_pc_sample;
static void i9100_pc_sample_record(unsigned *s,unsigned pc,unsigned cpsr){(void)s;(void)pc;(void)cpsr;pc_samples++;}
static void i9100_gpu_text(void){gpu_writes++;}
'''+methods+r'''
static void reset(void){
 memset(screen,0xa5,sizeof(screen));memset(fimd,0,sizeof(fimd));memset(iso_font,255,sizeof(iso_font));
 i9100_pixels=screen;i9100_fimd_map=(uintptr_t)fimd;
 gpu_writes=pc_samples=0;i9100_perf_strip=1;i9100_irq_frame_seen=1;
 i9100_panel_console_enabled=i9100_fatal_active=i9100_display_power_guard=i9100_panel_sleep_enabled=0;
 i9100_display_power_state.blanked=0;i9100_panel_sleep.qualified=0;i9100_panel_sleep.stage=0;
 i9100_live_irq_pc=i9100_live_irq_count=0;
}
static void unchanged(void){for(unsigned n=0;n<480*800+32;n++)assert(screen[n]==0xa5a5a5a5U);assert(!gpu_writes&&!fimd[0x1a4/4]);}
int main(void){
 alarm(10);
 for(unsigned mode=0;mode<9;mode++){
  reset();
  if(mode==0)i9100_perf_strip=0;if(mode==1)i9100_irq_frame_seen=0;
  if(mode==2)i9100_panel_console_enabled=1;if(mode==3)i9100_fatal_active=1;
  if(mode==4)i9100_pixels=0;if(mode==5)i9100_display_power_state.blanked=1;
  if(mode==6)i9100_display_power_guard=1;
  if(mode==7){i9100_panel_sleep_enabled=1;i9100_panel_sleep.qualified=1;i9100_panel_sleep.stage=I9P_FAULT;}
  if(mode==8){i9100_display_power_state.blanked=1;i9100_panel_sleep_enabled=1;i9100_panel_sleep.qualified=1;i9100_panel_sleep.stage=I9P_SLEEP;}
  for(unsigned count=100;count<=10000;count+=100)ios7i9100_timer_observe(0,count,0x80001000U+count,0x13);
  unchanged();assert(i9100_live_irq_count==10000&&i9100_live_irq_pc==0x80001000U+10000);
 }
 reset();ios7i9100_timer_observe(0,99,0x80000100,0x13);unchanged();assert(!i9100_live_irq_count);
 ios7i9100_timer_observe(0,100,0x80000104,0x13);
 assert(gpu_writes==1&&fimd[0x1a4/4]==3&&i9100_live_irq_count==100);
 for(unsigned n=0;n<768U*480U;n++)assert(screen[n]==0xa5a5a5a5U);
 for(unsigned n=800U*480U;n<800U*480U+32;n++)assert(screen[n]==0xa5a5a5a5U);
 i9100_display_power_state.blanked=1;fimd[0x1a4/4]=0;unsigned original[480*800];memcpy(original,screen,sizeof(original));
 ios7i9100_timer_observe(0,200,0x80000200,0x13);
 assert(gpu_writes==1&&!fimd[0x1a4/4]&&!memcmp(original,screen,sizeof(original))&&i9100_live_irq_count==200);
 assert(!i9100_status_text()); // Presentation overlay shares the same ownership guard.
 i9100_display_power_state.blanked=0;ios7i9100_timer_observe(0,300,0x80000300,0x13);
 assert(gpu_writes==2&&fimd[0x1a4/4]==3&&i9100_live_irq_count==300);
 reset();i9100_perf_strip=0;ios7i9100_timer_observe(0,3700,0x80000370,0x13);
 unchanged();assert(pc_samples==1&&i9100_live_irq_count==3700);
 puts("PASS production IRQ/present overlay: owned blank/sleep/transition/fault writes and FIMD triggers stop, RAM PC/IRQ liveness persists, awake drawing resumes and framebuffer bounds preserved");
}
'''
(out/'check.c').write_text(code,encoding='utf-8')
for index,cmd in enumerate((['clang','-std=c99','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'check.c'),'-o',str(out/'check')],
    [str(out/'check')])):
    result=subprocess.run(cmd,capture_output=True,text=True)
    (out/f'command-{index}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:print((result.stdout+result.stderr)[-2500:],file=sys.stderr);result.check_returncode()
    if result.stdout:print(result.stdout.strip(),flush=True)
if hashlib.sha256(source.read_bytes()).hexdigest()!=before:raise ValueError('Live PE source changed during test')
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,source_sha256=before,
    limit='Actual production methods with raw framebuffer/FIMD and font/PC-model dependencies; no measured electrical savings or physical sleep acceptance'),indent=2)+'\n',encoding='utf-8')
print('EVIDENCE',out,flush=True)
