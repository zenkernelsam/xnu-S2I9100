#!/usr/bin/env python3
"""Exercise production bounded RAM-copy policy with mask/translation modeled."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                             sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    output = repo / 'out' / ('irq-snapshot-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    output.mkdir(parents=True)
    source = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
static unsigned mask=0x13, gets,sets,calls,blocked,checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
#define I9100_SNAPSHOT_MASK_GET(saved) do {gets++;(saved)=mask;mask|=0xc0;}while(0)
#define I9100_SNAPSHOT_MASK_SET(saved) do {sets++;mask=(saved);}while(0)
static int page_ok(unsigned page) {CHECK((mask&0xc0)==0xc0);calls++;return page!=blocked;}
#define I9100_SNAPSHOT_PAGE_OK(page) page_ok(page)
''' + '#include "' + str(repo / 'pexpert/arm/i9100_irq_snapshot.h') + '"\n' + r'''
int main(void) {
    unsigned char *ram=mmap((void *)0xc2000000U,8192,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(ram==(void *)0xc2000000U);
    for(unsigned i=0;i<8192;i++)ram[i]=i%251;
    unsigned char out[256];
    for(unsigned initial=0x13;initial<=0xd3;initial+=0x40) {
        mask=initial;gets=sets=calls=0;
        CHECK(i9100_irq_snapshot_read(ram+4094,out,8));
        CHECK(!memcmp(out,ram+4094,8));
        CHECK(mask==initial && gets==1 && sets==1 && calls==2);
        blocked=0xc2001000U;memset(out,0xaa,sizeof(out));calls=0;
        CHECK(!i9100_irq_snapshot_read(ram+4094,out,8));
        CHECK(mask==initial && calls==2);
        for(unsigned i=0;i<sizeof(out);i++)CHECK(out[i]==0xaa);
        blocked=0;
    }
    for(unsigned size=1;size<=256;size++) {
        CHECK(i9100_irq_snapshot_read(ram,out,size));
        CHECK(!memcmp(out,ram,size));
    }
    unsigned before=gets;
    CHECK(!i9100_irq_snapshot_read(ram,out,0));
    CHECK(!i9100_irq_snapshot_read(ram,out,257));
    CHECK(!i9100_irq_snapshot_read((void *)0x7fffffffU,out,1));
    CHECK(!i9100_irq_snapshot_read((void *)0xf0000000U,out,1));
    CHECK(!i9100_irq_snapshot_read((void *)0xefffffffU,out,2));
    CHECK(!i9100_irq_snapshot_read((void *)0xfffffff0U,out,32));
    CHECK(!i9100_irq_snapshot_read(ram,0,1));
    CHECK(gets==before);
    printf("PASS %u production range/page-boundary/mask-restore checks (translation modeled)\n",checks);
    return 0;
}
'''
    (output / 'test.c').write_text(source)
    binary = output / 'test'
    # The production KVA window occupies ASan's shadow gap in a 64-bit host.
    # UBSan covers this fixed-address fixture; the USB staging harness also
    # runs ASan and never maps synthetic kernel addresses.
    for label, command in [('build', ['gcc', '-O1', '-g', '-fsanitize=undefined',
                                      '-fno-sanitize-recover=all', output / 'test.c', '-o', binary]),
                           ('test', [binary])]:
        result = subprocess.run(command, text=True, capture_output=True)
        (output / (label + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        if result.stdout:
            print(result.stdout, end='', flush=True)
    print('Evidence:', output, flush=True)


if __name__ == '__main__':
    main()
