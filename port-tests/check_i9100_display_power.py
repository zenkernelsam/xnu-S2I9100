#!/usr/bin/env python3
"""Verify the production FIMD blank/restore owner without panel reprogramming."""
import os,sys,subprocess
from pathlib import Path
from datetime import datetime
if os.environ.get('I9100_CPU_CAPPED')!='1':
 os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
root=Path(__file__).resolve().parents[1];out=root/'out'/('display-power-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
source=r'''
#include <assert.h>
#include <stdio.h>
#include "i9100_display_power.h"
struct model {uint32_t reg;unsigned writes,syncs,drop;};
static uint32_t read_reg(void*c){return ((struct model*)c)->reg;}
static void write_reg(void*c,uint32_t v){struct model*m=c;m->writes++;if(!m->drop)m->reg=v;}
static void sync_reg(void*c){((struct model*)c)->syncs++;}
int main(void){
 struct model m={0x80010203U,0,0,0};struct i9100_display_power_state s={0};
 struct i9100_display_power_io io={&m,read_reg,write_reg,sync_reg};
 assert(i9100_display_power_change(&s,&io,3)==1&&!m.writes);
 assert(i9100_display_power_change(0,&io,0)==1&&!m.writes);
 assert(!i9100_display_power_change(&s,&io,0)&&m.reg==0x81000000U&&s.blanked&&s.saved_map==0x80010203U);
 assert(!i9100_display_power_change(&s,&io,0)&&m.writes==1&&s.saved_map==0x80010203U);
 assert(!i9100_display_power_change(&s,&io,2)&&m.reg==0x80010203U&&!s.blanked);
 assert(!i9100_display_power_change(&s,&io,1)&&m.writes==2);
 m.drop=1;assert(i9100_display_power_change(&s,&io,0)==4&&!s.blanked&&m.reg==0x80010203U);
 m.drop=0;assert(!i9100_display_power_change(&s,&io,0));
 m.reg=0x22000000U;unsigned writes=m.writes;
 assert(i9100_display_power_change(&s,&io,1)==3&&m.writes==writes&&s.blanked);
 m.reg=s.owned_map;m.drop=1;assert(i9100_display_power_change(&s,&io,1)==4&&s.blanked);
 m.drop=0;assert(!i9100_display_power_change(&s,&io,1)&&!s.blanked&&m.reg==0x80010203U);
 assert(m.syncs==m.writes);
 puts("PASS native WINMAP black mapping, preserved reserved bits, repeated requests, owner loss and failed-write restore");
}
'''
(out/'check.c').write_text(source)
for cmd,name in [(['clang','-std=c99','-O1','-g','-fno-pie','-no-pie','-fsanitize=address,undefined','-I'+str(root/'pexpert/arm'),str(out/'check.c'),'-o',str(out/'check')],'compile'),([str(out/'check')],'result')]:
 r=subprocess.run(cmd,text=True,capture_output=True);(out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 if name=='result':print(r.stdout.strip())
print('EVIDENCE',out)
