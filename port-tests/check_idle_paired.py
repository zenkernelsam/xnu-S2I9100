#!/usr/bin/env python3
"""Exercise the production paired MCT/WFI publisher and current host decoder."""
import os,sys
if os.environ.get('I9100_CPU_CAPPED')!='1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,'-B',__file__],dict(os.environ,I9100_CPU_CAPPED='1'))
from pathlib import Path
from datetime import datetime
import importlib.util,json,subprocess,struct
repo=Path(__file__).resolve().parents[1];out=repo/'out'/('idle-paired-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "i9100_idle_observation.h"
int main(int argc,char**argv){assert(argc==2);struct i9100_idle_observation data={0};
 uint32_t wait[8]={1,4,123,0xfffffff0,1,240000,240000,0};
 i9100_idle_publish(&data,wait,0x200000100ULL,100,1);
 assert(data.version==1 && data.sequence==2 && data.wait_high==1 && data.wait_low==0xfffffff0 && data.waits==123);
 assert(data.mct_low==0x100 && data.mct_high==2 && data.frequency==24000000 && data.map_ready==1);
 char path[512];snprintf(path,sizeof(path),"%s/before.bin",argv[1]);FILE*f=fopen(path,"wb");assert(f && fwrite(&data,1,sizeof(data),f)==sizeof(data));fclose(f);
 struct i9100_idle_observation before=data;data.sequence|=1;
 i9100_idle_publish(&data,wait,0x300000000ULL,200,1);assert(data.sequence==(before.sequence|1));
 data=before;wait[1]|=1;i9100_idle_publish(&data,wait,0x300000000ULL,200,1);assert(!memcmp(&data,&before,sizeof(data)));
 wait[1]=6;wait[2]+=100;uint64_t next=((uint64_t)wait[4]<<32|wait[3])+144000000ULL;
 wait[3]=(uint32_t)next;wait[4]=next>>32;data.sequence=0xfffffffe;
 i9100_idle_publish(&data,wait,0x200000100ULL+240000000ULL,1100,1);assert(data.sequence==0);
 snprintf(path,sizeof(path),"%s/after.bin",argv[1]);f=fopen(path,"wb");assert(f && fwrite(&data,1,sizeof(data),f)==sizeof(data));fclose(f);
 i9100_idle_publish(&data,wait,0x200000100ULL+240000000ULL,1100,0);assert(!data.map_ready);
 puts("PASS production paired timer snapshot, same-IRQ values, odd publication refusal, map qualification and wrap");}
'''
(out/'check.c').write_text(code)
for n,cmd in enumerate((['clang','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-I'+str(repo/'pexpert/arm'),str(out/'check.c'),'-o',str(out/'check')],[str(out/'check'),str(out)])):
    r=subprocess.run(cmd,text=True,capture_output=True);(out/f'command-{n}.log').write_text(r.stdout+r.stderr)
    if r.returncode:print((r.stdout+r.stderr)[-2500:],file=sys.stderr);r.check_returncode()
    if r.stdout:print(r.stdout.strip())
spec=importlib.util.spec_from_file_location('host_idle',repo.parent/'uniloader-S2I9100/scripts/i9100/inspect-charge-health.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
before=m.decode_idle_paired((out/'before.bin').read_bytes());after=m.decode_idle_paired((out/'after.bin').read_bytes())
result=m.idle_interval(before,after);assert result['usable'] and result['wfi_wait_percent']==60 and result['elapsed_ns']==10_000_000_000
for raw in ((out/'before.bin').read_bytes(),(out/'after.bin').read_bytes()):
    checked=m.decode_idle_paired(raw,raw[:8]);assert checked['consistent'] and checked['publication_rechecked']
    version,sequence=struct.unpack_from('<2I',raw)
    for header in (struct.pack('<2I',version,sequence+2),struct.pack('<2I',version,sequence|1),struct.pack('<2I',0,sequence)):
        assert not m.decode_idle_paired(raw,header)['consistent']
    try:m.decode_idle_paired(raw,b'partial')
    except ValueError:pass
    else:raise AssertionError('Partial publication header accepted')
for scenario in ('partial','map','clock-reset','counter-reset','wait-exceeds-wall','irq-reset','unchanged','frequency'):
    actual=dict(after)
    if scenario in ('partial','map'):actual['consistent']=False
    if scenario=='clock-reset':actual['mct_ticks']=before['mct_ticks']-1
    if scenario=='counter-reset':actual['wait_ticks']=before['wait_ticks']-1
    if scenario=='wait-exceeds-wall':actual['wait_ticks']=before['wait_ticks']+241000000
    if scenario=='irq-reset':actual['irq']=before['irq']-1
    if scenario=='unchanged':actual=dict(before)
    if scenario=='frequency':actual['frequency']=1
    assert not m.idle_interval(before,actual)['usable'],scenario
assert not m.decode_idle_paired(bytes(48))['consistent']
partial=bytearray((out/'before.bin').read_bytes());struct.pack_into('<I',partial,4,3);assert not m.decode_idle_paired(partial)['consistent']
(out/'result.json').write_text(json.dumps(dict(result='PASS',interval=result,hardware=False),indent=2)+'\n')
print('PASS real paired-format host decode; measured interval fraction, reset/gap/partial/counter and bad frequency refusal')
spec=importlib.util.spec_from_file_location('host_idle_summary',repo.parent/'uniloader-S2I9100/scripts/i9100/summarize-idle-wait.py')
summary=importlib.util.module_from_spec(spec);spec.loader.exec_module(summary)
fixtures=summary.repo/'out'/('idle-owner-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'));fixtures.mkdir()
owner=fixtures/'original-owner';owner.mkdir();tag='0123456789abcdef'
lease=dict(pid=123,tag=tag,process_created_100ns=12345678)
(owner/'owner.json').write_text(json.dumps(lease)+'\n')
events=[dict(phase='READY',pid=123,tag=tag)]
paths=[]
for n,name in enumerate(('before.bin','after.bin')):
    directory=owner/('task-'+str(n).zfill(4));directory.mkdir();raw=(out/name).read_bytes()
    (directory/'idle-paired.bin').write_bytes(raw);(directory/'idle-paired-after.bin').write_bytes(raw[:8])
    data=dict(tag=tag,task=dict(id=str(n).zfill(4),action='charge'),charge=dict(idle_paired=m.decode_idle_paired(raw,raw[:8])))
    path=directory/'result.json';path.write_text(json.dumps(data)+'\n');paths.append(path)
    events.append(dict(phase='TASK_DONE',id=str(n).zfill(4),action='charge'))
events.extend([dict(phase='DONE'),dict(phase='CLOSED')])
(owner/'events.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in events))
first,e1=summary.read_capture(paths[0]);last,e2=summary.read_capture(paths[1])
assert summary.compare(first,e1,last,e2)['wfi_wait_percent']==60
for scenario in ('different-owner','different-lease','out-of-order','different-build'):
    altered=dict(e2);changed=dict(last)
    if scenario=='different-owner':altered['owner']='another-owner'
    if scenario=='different-lease':altered['owner_sha256']='other'
    if scenario=='out-of-order':altered['task_index']=e1['task_index']
    if scenario=='different-build':changed['tag']='fedcba9876543210'
    try:summary.compare(first,e1,changed,altered)
    except ValueError:pass
    else:raise AssertionError('Foreign boot/owner combined: '+scenario)
child=subprocess.run([sys.executable,'-B',str(summary.here/'summarize-idle-wait.py'),'--before',str(paths[0]),
    '--after',str(paths[1]),'--output',str(fixtures/'summary')],text=True,capture_output=True)
(fixtures/'summary-command.log').write_text(child.stdout+child.stderr);child.check_returncode()
assert json.loads((fixtures/'summary/result.json').read_text())['idle']['wfi_wait_percent']==60
(fixtures/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,scope='Production retained-file summarizer and same-owner qualification'),indent=2)+'\n')
print('PASS real original-owner result/binary/header/event validation and CLI; foreign owner/build, reversed task and partial publication refused')
print('HOST EVIDENCE',fixtures)
print('EVIDENCE',out)
