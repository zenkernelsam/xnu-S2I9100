#!/usr/bin/env python3
"""Check receive-capability enforcement in the production TEMPOWNER path."""
from datetime import datetime
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10',
            'ionice', '-c2', '-n7', sys.executable, __file__],
            dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    out = repo / 'out' / ('port-tempowner-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    source = (repo / 'osfmk/ipc/mach_port.c').read_text()
    start = source.index('static kern_return_t\nmach_port_set_attributes_impl(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    function = source[start:end]
    header = (repo / 'osfmk/ipc/ipc_port.h').read_text()
    macros = '\n'.join(line for line in header.splitlines()
        if re.match(r'#define\s+IP_(BIT_TEMPOWNER|TEMPOWNER|MARK_TEMPOWNER|CLEAR_TEMPOWNER)\b', line))
    flavor = re.search(r'#define\s+MACH_PORT_TEMPOWNER\s+(\d+)',
        (repo / 'osfmk/mach/port.h').read_text())[1]
    flags = re.search(r'statusp->mps_flags = ([^;]+);', source)[1]
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
typedef int kern_return_t;
typedef uint32_t mach_port_name_t, mach_msg_type_number_t;
typedef int *mach_port_info_t;
typedef struct {unsigned mpl_qlimit;} mach_port_limits_t;
typedef struct {unsigned imq_qlimit;} mqueue_t;
typedef struct port {unsigned ip_bits, ip_receiver_name; void *ip_receiver;
    mqueue_t ip_messages;} *ipc_port_t;
typedef void *ipc_space_t;
#define IS_NULL NULL
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE 5
#define KERN_INVALID_VALUE 18
#define KERN_INVALID_TASK 16
#define KERN_INVALID_NAME 15
#define KERN_INVALID_RIGHT 17
#define MACH_PORT_VALID(n) ((n) != 0 && (n) != UINT32_MAX)
#define MACH_PORT_LIMITS_INFO 1
#define MACH_PORT_DNREQUESTS_SIZE 3
#define MACH_PORT_LIMITS_INFO_COUNT 1
#define MACH_PORT_DNREQUESTS_SIZE_COUNT 1
#define MACH_PORT_QLIMIT_MAX 1024
#define MACH_PORT_STATUS_FLAG_TEMPOWNER 1
static struct port p;
static int space_owner, other_space, translated, unlocked, checks, lookup_result;
static unsigned oldbits;
#define CHECK(x) do {checks++; if (!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static kern_return_t ipc_port_translate_receive(ipc_space_t s, unsigned name, ipc_port_t *port) {
    translated++;
    if (lookup_result) return lookup_result;
    if (s != &space_owner || name != 0x1603) return KERN_INVALID_NAME;
    *port = &p; return KERN_SUCCESS;
}
static void ip_unlock(ipc_port_t port) {CHECK(port == &p);unlocked++;}
static void ipc_mqueue_set_qlimit(mqueue_t *q, unsigned limit) {q->imq_qlimit=limit;}
static kern_return_t ipc_port_request_grow(ipc_port_t port, int size) {(void)size;ip_unlock(port);return 0;}
static void reset(void) {
    p.ip_bits=0x800082a5U;oldbits=p.ip_bits;translated=unlocked=lookup_result=0;
}
''' + '#define MACH_PORT_TEMPOWNER ' + flavor + '\n' + macros + '\n' + function + '\n'
    harness += 'static unsigned observed_status(ipc_port_t port) {return ' + flags + ';}\n'
    harness += r'''
int main(void) {
    reset();CHECK(mach_port_set_attributes_impl(NULL,0x1603,4,NULL,0)==KERN_INVALID_TASK);
    CHECK(p.ip_bits==oldbits && translated==0 && unlocked==0);
    unsigned invalid[]={0,UINT32_MAX};
    for(unsigned i=0;i<2;i++) {
        reset();CHECK(mach_port_set_attributes_impl(&space_owner,invalid[i],4,NULL,0)==KERN_INVALID_RIGHT);
        CHECK(p.ip_bits==oldbits && translated==0 && unlocked==0);
    }
    reset();CHECK(mach_port_set_attributes_impl(&other_space,0x1603,4,NULL,0)==KERN_INVALID_NAME);
    CHECK(p.ip_bits==oldbits && translated==1 && unlocked==0);
    reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x999,4,NULL,0)==KERN_INVALID_NAME);
    CHECK(p.ip_bits==oldbits && translated==1 && unlocked==0);
    int rejected[]={KERN_INVALID_RIGHT,KERN_INVALID_TASK,KERN_FAILURE};
    for(unsigned i=0;i<3;i++) {
        reset();lookup_result=rejected[i];
        CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,4,NULL,0)==rejected[i]);
        CHECK(p.ip_bits==oldbits && unlocked==0);
    }
    reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,4,NULL,0)==0);
    CHECK(translated==1 && unlocked==1 && p.ip_bits==(oldbits|IP_BIT_TEMPOWNER));
    CHECK(observed_status(&p)==MACH_PORT_STATUS_FLAG_TEMPOWNER);
    CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,4,NULL,0)==0);
    CHECK(p.ip_bits==(oldbits|IP_BIT_TEMPOWNER) && unlocked==2);
    IP_CLEAR_TEMPOWNER(&p);CHECK(p.ip_bits==oldbits && observed_status(&p)==0);
    int unknown[]={0,2,5,6,7,255};
    for(unsigned i=0;i<6;i++) {
        reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,unknown[i],NULL,0)==KERN_INVALID_ARGUMENT);
        CHECK(p.ip_bits==oldbits && translated==0 && unlocked==0);
    }
    int limit=6;
    reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,1,&limit,1)==0);
    CHECK(p.ip_messages.imq_qlimit==6 && p.ip_bits==oldbits && unlocked==1);
    reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,1,NULL,0)==KERN_FAILURE);
    CHECK(translated==0 && p.ip_bits==oldbits);
    limit=1025;reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,1,&limit,1)==KERN_INVALID_VALUE);
    CHECK(translated==0 && p.ip_bits==oldbits);
    reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,3,NULL,0)==KERN_FAILURE);
    CHECK(translated==0 && p.ip_bits==oldbits);
    limit=8;reset();CHECK(mach_port_set_attributes_impl(&space_owner,0x1603,3,&limit,1)==0);
    CHECK(translated==1 && unlocked==1 && p.ip_bits==oldbits);
    printf("PASS %d production receive-right/attribute checks\n",checks);
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
