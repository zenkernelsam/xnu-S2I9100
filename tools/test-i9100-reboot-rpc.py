#!/usr/bin/env python3
"""Check the production launchd reboot observer against bounded Mach messages."""
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
    out = repo / 'out' / ('reboot-rpc-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    source = (repo / 'osfmk/ipc/mach_msg.c').read_text()
    start = source.index('void\nios7i9100_reboot_rpc_note(')
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
typedef uint32_t mach_msg_size_t, UInt32;
typedef struct {uint32_t msgh_bits,msgh_size,remote,local,reserved;int32_t msgh_id;} mach_msg_header_t;
typedef struct {uint32_t val[8];} audit_token_t;
typedef struct {uint32_t msgh_trailer_type,size,seq,sender[2];audit_token_t msgh_audit;} mach_msg_audit_trailer_t;
typedef struct ipc_kmsg {mach_msg_header_t *ikm_header;unsigned ikm_size;} *ipc_kmsg_t;
#define MACH_MSGH_BITS_COMPLEX 0x80000000U
#define MACH_MSG_TRAILER_FORMAT_0 0
volatile uint32_t i9100_reboot_rpc_diag[1+16*12];
static int enabled=1,pid=1,checks;
static int proc_selfpid(void) {return pid;}
static int i9100_xpc_boundary_enabled(void) {return enabled;}
static int OSCompareAndSwap(uint32_t old,uint32_t value,volatile uint32_t *p) {
    if (*p!=old) return 0;*p=value;return 1;
}
static void OSMemoryBarrier(void) {}
static void proc_name(int n,char *b,int size) {(void)n;snprintf(b,size,"watchdogd");}
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}} while(0)
''' + source[start:end] + r'''
int main(void) {
    struct {struct ipc_kmsg k;unsigned char payload[96];} b;
    memset(&b,0,sizeof(b));
    b.k.ikm_size=sizeof(b.payload);b.k.ikm_header=(void *)b.payload;
    mach_msg_header_t *h=b.k.ikm_header;
    CHECK(sizeof(*h)==24 && sizeof(mach_msg_audit_trailer_t)==52);
    h->msgh_id=401;h->msgh_size=40;h->msgh_bits=0x1513;
    unsigned char *body=(void *)(h+1);body[4]=1;
    uint32_t flags[2]={0,0x80000001U};memcpy(body+8,flags,8);
    mach_msg_audit_trailer_t *tr=(void *)(b.payload+40);tr->msgh_audit.val[5]=17;
    enabled=0;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);enabled=1;
    pid=2;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);pid=1;
    ios7i9100_reboot_rpc_note(NULL);CHECK(i9100_reboot_rpc_diag[0]==0);
    b.k.ikm_header=NULL;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);
    b.k.ikm_header=(void *)b.payload;
    b.k.ikm_size=91;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);b.k.ikm_size=96;
    b.k.ikm_header=(void *)&b.k;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);
    b.k.ikm_header=(void *)b.payload;
    h->msgh_size=39;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);h->msgh_size=40;
    h->msgh_id=501;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);h->msgh_id=401;
    h->msgh_bits|=MACH_MSGH_BITS_COMPLEX;ios7i9100_reboot_rpc_note(&b.k);
    CHECK(i9100_reboot_rpc_diag[0]==0);h->msgh_bits&=~MACH_MSGH_BITS_COMPLEX;
    for(unsigned i=0;i<8;i++) {
        unsigned char old=body[i];body[i]=old?0:1;
        ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);body[i]=old;
    }
    tr->msgh_trailer_type=1;ios7i9100_reboot_rpc_note(&b.k);CHECK(i9100_reboot_rpc_diag[0]==0);tr->msgh_trailer_type=0;
    for(unsigned i=0;i<32;i++) ios7i9100_reboot_rpc_note(&b.k);
    CHECK(i9100_reboot_rpc_diag[0]==16);
    for(unsigned i=0;i<16;i++) {
        volatile uint32_t *r=&i9100_reboot_rpc_diag[1+i*12];
        CHECK(r[0]==1 && r[1]==17 && r[2]==0 && r[3]==0x80000001U);
        CHECK(r[4]==401 && r[5]==40 && r[6]==0x1513 && r[11]==1);
        CHECK(memcmp((void *)&r[7],"watchdogd",9)==0);
    }
    CHECK(h->msgh_id==401 && h->msgh_bits==0x1513 && body[4]==1);
    CHECK(memcmp(body+8,flags,8)==0 && tr->msgh_audit.val[5]==17);
    printf("PASS %d production reboot-RPC bounds/publication checks\n",checks);
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
