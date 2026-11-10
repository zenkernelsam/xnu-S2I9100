#!/usr/bin/env python3
"""Exercise production exec environment append/count/ARG_MAX invariants."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset',['taskset','-c','0-13','nice','-n10','ionice','-c2','-n7',sys.executable,__file__],
               dict(os.environ,I9100_CPU_CAPPED='1'))
repo=Path(__file__).resolve().parents[1]
source=(repo/'bsd/kern/kern_exec.c').read_text()
def function(name):
    start=source.index(name+'(');begin=source.index('{',start);end=begin+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return 'static int '+source[start:end]+'\n'
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uintptr_t user_addr_t;
typedef int boolean_t;
#define TRUE 1
#define UIO_SYSSPACE 0
#define UIO_SEG_IS_USER_SPACE(x) ((x)!=0)
#define CAST_USER_ADDR_T(x) ((user_addr_t)(uintptr_t)(x))
#define CAST_DOWN(t,x) ((t)(uintptr_t)(x))
struct image_params {int ip_argspace,ip_strspace,ip_envc,ip_argc;char *ip_strendp,*ip_endargv;};
static volatile uint32_t i9100_shared_directory_added[2];
static int copystr(const char *from,char *to,int cap,size_t *len){
 size_t n=strlen(from)+1;*len=n>(size_t)cap?(size_t)cap:n;memcpy(to,from,*len);return n>(size_t)cap?ENAMETOOLONG:0;
}
static int copyinstr(user_addr_t from,char *to,int cap,size_t *len){return copystr((const char *)from,to,cap,len);}
'''
tests=r'''
static char data[512];
static struct image_params setup(const char *env,int argspace){
 memset(data,0xcc,sizeof(data));memcpy(data,"argv\0",5);char *end=data+5;
 int count=0;if(env){size_t n=strlen(env)+1;memcpy(end,env,n);end+=n;count=1;}
 return (struct image_params){argspace,(int)(sizeof(data)-(end-data)),count,1,end,data+5};
}
int main(void){
 const char *value="IPHONE_SHARED_RESOURCES_DIRECTORY=/var/mobile";
 unsigned cases=0;int n=(int)strlen(value)+1;
 for(unsigned p=4;p<=8;p+=4)for(unsigned role=0;role<2;role++){
  const char *name=role?"kbd":"SpringBoard";unsigned len=(unsigned)strlen(name);
  struct image_params a=setup("LANG=en_US.UTF-8",300),before=a;char saved[512];memcpy(saved,data,sizeof(data));
  assert(!i9100_exec_shared_directory(&a,p,name,len));assert(a.ip_envc==2&&a.ip_argc==1);
  assert(a.ip_strendp==before.ip_strendp+n && a.ip_strspace==before.ip_strspace-n);
  assert(a.ip_argspace==before.ip_argspace-n-(int)p);assert(!strcmp(before.ip_strendp,value));
  assert(!memcmp(saved,data,before.ip_strendp-data));cases++;
  a=setup(0,300);before=a;memcpy(saved,data,sizeof(data));
  assert(!i9100_exec_shared_directory(&a,p,name,len));assert(a.ip_envc==1&&!strcmp(a.ip_endargv,value));
  assert(!memcmp(saved,data,5)&&a.ip_argspace==before.ip_argspace-n-(int)p);cases++;
  const char *existing[]={"IPHONE_SHARED_RESOURCES_DIRECTORY=/custom","IPHONE_SHARED_RESOURCES_DIRECTORY="};
  for(unsigned k=0;k<2;k++){
   a=setup(existing[k],300);before=a;memcpy(saved,data,sizeof(data));
   assert(!i9100_exec_shared_directory(&a,p,name,len));assert(!memcmp(data,saved,sizeof(data))&&a.ip_envc==before.ip_envc&&a.ip_argspace==before.ip_argspace);cases++;
  }
  for(int space=0;space<n+(int)(3*p);space++){
   a=setup(0,space);before=a;memcpy(saved,data,sizeof(data));
   assert(!i9100_exec_shared_directory(&a,p,name,len));assert(!memcmp(data,saved,sizeof(data))&&a.ip_envc==0&&a.ip_argspace==space&&a.ip_strendp==before.ip_strendp);cases++;
  }
  for(int space=0;space<n;space++){
   a=setup(0,300);a.ip_strspace=space;before=a;memcpy(saved,data,sizeof(data));
   assert(!i9100_exec_shared_directory(&a,p,name,len));assert(!memcmp(data,saved,sizeof(data))&&a.ip_envc==0&&a.ip_strspace==space&&a.ip_strendp==before.ip_strendp);cases++;
  }
 }
 const char *others[]={"launchd","SpringBoard.helper","XSpringBoard","KBD"};
 for(unsigned k=0;k<4;k++){
  struct image_params a=setup(0,300);char saved[512];memcpy(saved,data,sizeof(data));
  assert(!i9100_exec_shared_directory(&a,4,others[k],strlen(others[k])));assert(!memcmp(data,saved,sizeof(data))&&a.ip_envc==0);cases++;
 }
 printf("PASS %u production exec environment cases: pointer/string counts, explicit overrides, ARG_MAX and unrelated executables; ASan/UBSan\n",cases);
}
'''
out=repo/'out'/('shared-directory-test-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True,exist_ok=False)
code=out/'test.c';code.write_text(prefix+function('exec_add_user_string')+function('i9100_exec_shared_directory')+tests)
binary=out/'test'
subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(code),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],text=True,capture_output=True,check=True);(out/'result.txt').write_text(r.stdout+r.stderr)
print(r.stdout,end='');print('Artifacts:',out)
