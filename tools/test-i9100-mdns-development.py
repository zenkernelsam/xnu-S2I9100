#!/usr/bin/env python3
"""Validate the separately authorized MDNS initialization boundary."""
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
    out = repo/'out'/('mdns-development-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True)
    code = '#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#define ENOPOLICY 103\n'
    code += '#include "'+str(repo/'security/i9100_mdns_development.h')+'"\n'
    code += '#include "'+str(repo/'security/i9100_tcc_development.h')+'"\n'
    code += r'''
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
    CHECK(i9100_mdns_development_allowed("mDNSResponder","Sandbox",1,0,1,103));
    /* The actually observed operation2 query remains outside this option. */
    CHECK(!i9100_mdns_development_allowed("mDNSResponder","Sandbox",2,0,1,103));
    CHECK(!i9100_mdns_development_allowed("mDNSResponder","Sandbox",0,0,1,103));
    const char *callers[]={"mDNSResponderHelper","mDNSResponderHel","mDNSResponder2",
        "tccd","SpringBoard","configd","launchd","mdnsresponder","",NULL};
    for(unsigned i=0;i<10;i++) CHECK(!i9100_mdns_development_allowed(callers[i],"Sandbox",1,0,1,103));
    const char *policies[]={"sandbox","Sandbox2","Seatbelt","AMFI","",NULL};
    for(unsigned i=0;i<6;i++) CHECK(!i9100_mdns_development_allowed("mDNSResponder",policies[i],1,0,1,103));
    for(int flag=-3;flag<5;flag++)
        CHECK(i9100_mdns_development_allowed("mDNSResponder","Sandbox",1,0,flag,103)==(flag==1));
    for(int call=-10;call<128;call++)
        CHECK(i9100_mdns_development_allowed("mDNSResponder","Sandbox",call,0,1,103)==(call==1));
    for(unsigned providers=0;providers<64;providers++)
        CHECK(i9100_mdns_development_allowed("mDNSResponder","Sandbox",1,providers,1,103)==(providers==0));
    for(int err=0;err<256;err++)
        CHECK(i9100_mdns_development_allowed("mDNSResponder","Sandbox",1,0,1,err)==(err==103));
    /* Independent options cannot silently broaden the previous TCC scope. */
    CHECK(!i9100_tcc_development_allowed("mDNSResponder","Sandbox",1,0,1,103));
    CHECK(i9100_tcc_development_allowed("tccd","Sandbox",0,0,1,103));
    CHECK(i9100_tcc_development_allowed("tccd","Sandbox",1,0,1,103));
    CHECK(!i9100_tcc_development_allowed("tccd","Sandbox",2,0,1,103));
    printf("PASS %u MDNS development-scope/cross-service checks\n",checks);
    return 0;
}
'''
    (out/'test.c').write_text(code)
    binary = out/'test'
    for name, command in [('build', ['gcc', '-std=c99', '-O1', '-g',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(out/'test.c'), '-o', str(binary)]),
        ('test', [str(binary)])]:
        result = subprocess.run(command, text=True, capture_output=True)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
        if result.stdout:
            print(result.stdout, end='', flush=True)
    print('Artifacts:', out, flush=True)


if __name__ == '__main__':
    main()
