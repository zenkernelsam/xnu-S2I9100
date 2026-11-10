#!/usr/bin/env python3
"""Verify actual event rearm writes into the new receive buffer, not the old.

Compile the production touch function and a fixed historical control with
ASan/UBSan. Retain all artifacts; never access the phone or clean files.
"""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys


def extract(text):
    pos = text.rindex('filt_machporttouch(struct knote')
    start = text.rfind('static void', 0, pos)
    brace = text.index('{', pos)
    end = brace + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    if os.environ.get('I9100_CPU_CAPPED') != '1':
        os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                   sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
    repo = Path(__file__).resolve().parents[1]
    out = repo / 'out' / ('machport-rearm-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    old = subprocess.check_output(['git', 'show', '3ee54d0:osfmk/ipc/ipc_pset.c'], cwd=repo, text=True)
    new = (repo / 'osfmk/ipc/ipc_pset.c').read_text()
    prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct kevent64_s {uint64_t ident; int16_t filter; uint16_t flags; uint32_t fflags;
                  int64_t data; uint64_t udata; uint64_t ext[2];};
struct knote {struct kevent64_s kn_kevent; uint32_t kn_sfflags; int64_t kn_sdata;};
#define kn_ext kn_kevent.ext
#define kn_flags kn_kevent.flags
#define kn_data kn_kevent.data
#define kn_fflags kn_kevent.fflags
#define EVENT_REGISTER 1
#define EVENT_PROCESS 2
#define EV_CLEAR 0x20
#define panic(...) abort()
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n", __LINE__, #x);return 1;}}while(0)
'''
    suffix = r'''
int main(void) {
    unsigned char previous[1024], buffers[3][1024], packet[288];
    memset(previous, 0x5a, sizeof(previous)); memset(buffers, 0, sizeof(buffers));
    memset(packet, 0xa5, sizeof(packet));
    struct knote kn = {0};
    kn.kn_ext[0] = (uintptr_t)previous; kn.kn_ext[1] = 16;
    const unsigned capacities[] = {512, 1024, 288};
    for (unsigned n = 0; n < 3; n++) {
        struct kevent64_s request = {.ident=0x1403, .filter=-8, .flags=0x21,
                                    .fflags=0x7000006, .data=17};
        request.ext[0] = (uintptr_t)buffers[n]; request.ext[1] = capacities[n];
        filt_machporttouch(&kn, &request, EVENT_REGISTER);
        CHECK(kn.kn_sfflags == request.fflags && kn.kn_sdata == request.data);
        /* A real next receive consumes these extensions, and may need a
         * larger buffer after MACH_RCV_TOO_LARGE. The old code leaves16. */
        CHECK(kn.kn_ext[1] >= sizeof(packet));
        memcpy((void *)(uintptr_t)kn.kn_ext[0], packet, sizeof(packet));
        CHECK(!memcmp(buffers[n], packet, sizeof(packet)));
        for (unsigned i = 0; i < sizeof(previous); i++) CHECK(previous[i] == 0x5a);
        CHECK(kn.kn_ext[1] == capacities[n]);
        /* Returning the event preserves the current receive extensions,
         * including shrink, while EV_CLEAR retains its original behavior. */
        kn.kn_flags = EV_CLEAR; kn.kn_data = 23; kn.kn_fflags = 0x34;
        struct kevent64_s result = {0};
        filt_machporttouch(&kn, &result, EVENT_PROCESS);
        CHECK(result.ext[0] == request.ext[0] && result.ext[1] == capacities[n]);
        CHECK(result.data == 23 && result.fflags == 0x34);
        CHECK(kn.kn_data == 0 && kn.kn_fflags == 0);
    }
    puts("PASS grow, relocate and shrink receive buffers; previous buffer preserved");
    return 0;
}
'''
    for label, text in [('historical', old), ('current', new)]:
        source = out / (label + '.c')
        binary = out / label
        source.write_text(prefix + extract(text) + suffix)
        command = ['gcc', '-std=c99', '-O1', '-g', '-fsanitize=address,undefined',
                   '-fno-sanitize-recover=all', str(source), '-o', str(binary)]
        build = subprocess.run(command, text=True, capture_output=True)
        (out / (label + '-build.log')).write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        run = subprocess.run([str(binary)], text=True, capture_output=True)
        (out / (label + '-test.log')).write_text(run.stdout + run.stderr)
        if (label == 'current' and run.returncode) or (label == 'historical' and run.returncode != 1):
            raise RuntimeError(label + ': ' + run.stdout + run.stderr)
        print(label, 'expected failure reproduced' if label == 'historical' else run.stdout.strip(), flush=True)
    print('Artifacts:', out, flush=True)


if __name__ == '__main__':
    main()
