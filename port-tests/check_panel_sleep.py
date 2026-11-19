#!/usr/bin/env python3
"""Exercise actual U1 GPIO SPI and retained-state panel sleep production code."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from datetime import datetime
from pathlib import Path
import hashlib,json,subprocess
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('panel-sleep-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_panel_spi.h"
#include "i9100_panel_sleep.h"
struct wire {
 uint32_t regs[128],initial[128];unsigned lock,enters,leaves,edge,command,read_edges,words[256],count,
  transaction,reading,drop_mode,write_fail,mode,fail_read,reads,id_change;uint8_t ids[3];
};
static uint32_t rd(void*cookie,unsigned off){struct wire*w=cookie;assert(w->lock);
 assert(off==0x180||off==0x184||off==0x1a0||off==0x1a4);
 uint32_t value=w->regs[off/4];
 if(off==0x184 && w->reading){
  unsigned n=w->read_edges;
  uint8_t response=w->command>=0xda&&w->command<=0xdc?w->ids[w->command-0xda]:(uint8_t)w->mode;
  if(w->command==0xda && w->id_change && w->reads>1)response^=1;
  if(n>8 && n<=16){unsigned bit=(response>>(16U-n))&1U;value=(value&~I9P_SI)|(bit?I9P_SI:0);}
 }
 return value;
}
static void wr(void*cookie,unsigned off,uint32_t value){struct wire*w=cookie;assert(w->lock);
 assert(off==0x180||off==0x184||off==0x1a0||off==0x1a4);
 uint32_t previous=w->regs[off/4];
 if(w->write_fail==1 && off==0x180 && ((value>>12)&15)==0)return;
 if(w->write_fail==2 && off==0x184)value|=I9P_CLK;
 w->regs[off/4]=value;
 if(off==0x1a4 && (previous&I9P_CS) && !(value&I9P_CS)){
  w->transaction=1;w->edge=w->command=w->read_edges=w->reading=0;
 }
 if(off==0x184 && w->transaction && !(previous&I9P_CLK) && (value&I9P_CLK)){
  if(((w->regs[0x180/4]>>12)&15)==1){w->command=(w->command<<1)|((value>>3)&1U);++w->edge;}
  else{w->reading=1;++w->read_edges;}
 }
 if(off==0x180 && w->transaction && ((value>>12)&15)==0){w->reading=1;++w->reads;}
 if(off==0x1a4 && !(previous&I9P_CS) && (value&I9P_CS) && w->transaction){
  if(!w->write_fail)assert(w->edge==9);w->transaction=0;
  if(!w->reading){assert(w->count<256);w->words[w->count++]=w->command;
   if(!w->drop_mode){if(w->command==0x28)w->mode&=~4U;else if(w->command==0x10)w->mode&=~0x10U;
    else if(w->command==0x11)w->mode|=0x10U;else if(w->command==0x29)w->mode|=4U;}
  }
 }
 // Non-panel fields may change during a transfer; production restore must
 // retain the current unrelated GPIO bits and preserve gauge/reset pins.
 if(off==0x184)w->regs[off/4]|=1U<<6;
}
static void delay(void*cookie,unsigned us){assert(((struct wire*)cookie)->lock && us==1);}
static unsigned enter(void*cookie){struct wire*w=cookie;assert(!w->lock);w->lock=1;++w->enters;return 77;}
static void leave(void*cookie,unsigned token){struct wire*w=cookie;assert(w->lock&&token==77);w->lock=0;++w->leaves;}
static void initialize(struct wire*w,struct i9100_panel_spi*spi){
 memset(w,0,sizeof(*w));memset(spi,0,sizeof(*spi));
 w->regs[0x180/4]=0xaaaa111a;w->regs[0x184/4]=I9P_CLK|I9P_SI|0x80;
 w->regs[0x1a0/4]=0x55151100;w->regs[0x1a4/4]=0xb8;
 memcpy(w->initial,w->regs,sizeof(w->regs));w->mode=0x9c;w->ids[0]=0xa2;w->ids[1]=0x12;w->ids[2]=0x11;
 spi->io=(struct i9100_panel_spi_io){w,rd,wr,delay,enter,leave};
}
static void restored(struct wire*w){
 assert(!w->lock && w->enters==w->leaves);
 assert(w->regs[0x180/4]==w->initial[0x180/4] && w->regs[0x1a0/4]==w->initial[0x1a0/4]);
 assert((w->regs[0x1a4/4]&~I9P_CS)==(w->initial[0x1a4/4]&~I9P_CS));
 assert((w->regs[0x184/4]&I9P_CLK)==(w->initial[0x184/4]&I9P_CLK));
 assert(w->regs[0x184/4]&(1U<<6));
}
struct panel {struct wire wire;struct i9100_panel_spi spi;uint32_t scanout;unsigned waits,scan_writes,drop_scan;};
static int read_byte(void*cookie,unsigned reg,uint8_t*out){struct panel*p=cookie;
 if(p->wire.fail_read)return 0;return i9100_panel_spi_read_byte(&p->spi,reg,out);}
static int command(void*cookie,unsigned reg){struct panel*p=cookie;return i9100_panel_spi_word(&p->spi,reg);}
static uint32_t scan_read(void*cookie){return ((struct panel*)cookie)->scanout;}
static void scan_write(void*cookie,uint32_t value){struct panel*p=cookie;++p->scan_writes;
 if(p->drop_scan!=1 && !(p->drop_scan==2 && p->scan_writes==1))p->scanout=value;}
static void wait_ms(void*cookie,unsigned ms){struct panel*p=cookie;assert(!p->wire.lock && (ms==120 || ms==34));p->waits+=ms;}
static void panel_init(struct panel*p,struct i9100_panel_sleep*s){memset(p,0,sizeof(*p));memset(s,0,sizeof(*s));initialize(&p->wire,&p->spi);
 p->scanout=0x991003;s->io=(struct i9100_panel_sleep_io){p,read_byte,command,scan_read,scan_write,wait_ms};}
int main(){
 struct wire w;struct i9100_panel_spi spi;initialize(&w,&spi);
 assert(i9100_panel_spi_word(&spi,0x29));assert(w.count==1&&w.words[0]==0x29);restored(&w);
 assert(i9100_panel_spi_word(&spi,0x15a));assert(w.words[1]==0x15a);restored(&w);
 uint8_t value=0;assert(i9100_panel_spi_read_byte(&spi,0xda,&value)&&value==0xa2&&w.read_edges==16);restored(&w);
 unsigned enters=w.enters;assert(!i9100_panel_spi_word(&spi,0x200)&&w.enters==enters);
 w.regs[0x180/4]&=~(15U<<4);assert(!i9100_panel_spi_word(&spi,0x28)&&w.enters==w.leaves&&!w.lock);
 initialize(&w,&spi);w.write_fail=1;value=0x55;
 assert(!i9100_panel_spi_read_byte(&spi,0xda,&value)&&value==0x55&&!w.lock&&w.enters==w.leaves);
 initialize(&w,&spi);w.write_fail=2;assert(!i9100_panel_spi_word(&spi,0x28)&&!w.lock&&w.enters==w.leaves);
 struct panel p;struct i9100_panel_sleep s;panel_init(&p,&s);assert(!i9100_panel_probe(&s)&&s.qualified&&s.stage==I9P_AWAKE);
 uint32_t scan=p.scanout;assert(!i9100_panel_sleep_change(&s,0)&&s.stage==I9P_SLEEP&&p.scanout==(scan&~3U));
 assert(p.wire.words[0]==0x28&&p.wire.words[1]==0x10&&p.waits==154&&p.scan_writes==2);
 unsigned commands=s.commands;assert(!i9100_panel_sleep_change(&s,0)&&s.commands==commands);
 assert(!i9100_panel_sleep_change(&s,1)&&s.stage==I9P_AWAKE&&p.scanout==scan);
 assert(p.wire.words[2]==0x11&&p.wire.words[3]==0x29&&p.waits==274);restored(&p.wire);
 for(unsigned scenario=0;scenario<8;scenario++){
  panel_init(&p,&s);
  if(scenario==0)p.wire.ids[1]=0x13;
  if(scenario==1)p.wire.id_change=1;
  if(scenario==2)p.wire.mode=0;
  if(scenario==3)p.scanout&=~3U;
  if(scenario==4)p.wire.fail_read=1;
  if(scenario==5)p.wire.regs[0x1a0/4]&=~I9P_MUX4;
  if(scenario==6)p.wire.mode=0xff;
  if(scenario==7)p.scanout=0xffffffffU;
  assert(i9100_panel_probe(&s)&&!s.qualified&&!p.wire.count&&!p.scan_writes);
  if(scenario==0)assert(s.id[0]==0xa2 && s.id[1]==0x13 && s.id[2]==0x11 && s.identity_reads==0x3f);
  if(scenario==1)assert(s.id[0]==0xa2 && (s.identity_reads&0x40));
  if((scenario>=2 && scenario<=3) || scenario>=6)assert(s.identity_reads==0x3f);
  if(scenario==4 || scenario==5)assert(s.identity_reads==0);
 }
 panel_init(&p,&s);assert(!i9100_panel_probe(&s));p.wire.drop_mode=1;
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_MODE&&s.stage==I9P_FAULT&&!p.scan_writes);
 commands=s.commands;assert(!i9100_panel_sleep_change(&s,1)&&s.stage==I9P_AWAKE&&s.commands==commands&&s.recovery_attempts==1&&s.error==I9P_ERROR_MODE);
 // Panel was already awake: recovery sends no duplicate command and keeps
 // the first fault for diagnosis; a later failure cannot repeat recovery.
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_MODE&&s.stage==I9P_FAULT);
 commands=s.commands;assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_MODE&&s.commands==commands&&s.recovery_attempts==1);
 panel_init(&p,&s);assert(!i9100_panel_probe(&s));p.drop_scan=1;
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_SCANOUT&&s.stage==I9P_FAULT);
 panel_init(&p,&s);assert(!i9100_panel_probe(&s));p.drop_scan=2;
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_SCANOUT&&s.stage==I9P_FAULT&&p.scan_writes==1);
 p.drop_scan=0;assert(!i9100_panel_sleep_change(&s,1)&&s.stage==I9P_AWAKE&&s.recovery_attempts==1&&s.recovery_commands==2&&s.error==I9P_ERROR_SCANOUT);
 panel_init(&p,&s);assert(!i9100_panel_probe(&s)&&!i9100_panel_sleep_change(&s,0));p.scanout^=0x100;
 commands=s.commands;assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_SCANOUT&&s.commands==commands);
 assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_SCANOUT&&s.commands==commands&&s.recovery_attempts==1);
 panel_init(&p,&s);assert(!i9100_panel_probe(&s));p.drop_scan=2;
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_SCANOUT);
 p.drop_scan=0;p.wire.ids[1]=0x13;commands=s.commands;
 assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_IDENTITY&&s.commands==commands&&s.error==I9P_ERROR_SCANOUT&&s.cleanup_error==I9P_ERROR_IDENTITY);
 panel_init(&p,&s);assert(!i9100_panel_probe(&s));p.drop_scan=2;
 assert(i9100_panel_sleep_change(&s,0)==I9P_ERROR_SCANOUT);
 p.drop_scan=0;p.wire.drop_mode=1;commands=s.commands;
 assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_MODE&&s.recovery_commands==1&&s.commands==commands+1);
 commands=s.commands;assert(i9100_panel_sleep_change(&s,1)==I9P_ERROR_MODE&&s.commands==commands);
 puts("PASS production 9bit mode3 commands/read dummy cycles, GPIO field restore, real-id/mode qualifications, retained scanout sleep/wake, command order, delays and IO/readback/foreign owner failures");
}
'''
(out/'model.c').write_text(code)
for n,cmd in enumerate((['clang','-std=c99','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-fno-sanitize-recover=all','-I'+str(repo/'pexpert/arm'),str(out/'model.c'),'-o',str(out/'model')],[str(out/'model')])):
 result=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(result.stdout+result.stderr)
 if result.returncode:print((result.stdout+result.stderr)[-3500:],file=sys.stderr);result.check_returncode()
 if result.stdout:print(result.stdout.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,sources={name:hashlib.sha256((repo/'pexpert/arm'/name).read_bytes()).hexdigest() for name in ('i9100_panel_spi.h','i9100_panel_sleep.h')},
    limit='GPIO register/9bit panel/clock are facades. Production transport and sleep state machine execute; no physical sleep/energy proved.'),indent=2)+'\n')
print('EVIDENCE',out,flush=True)
