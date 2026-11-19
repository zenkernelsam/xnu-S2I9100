/* SPDX-License-Identifier: BSD-2-Clause. Actual control+worker+UART/core. */
#define main bt_worker_reference_main
#include "i9100_bt_worker_test.cpp"
#undef main
#include "i9100_bt_control.h"
#include "bt-control-sdk/i9100_bt_kernel_control_model.h"
struct i9100_model_mbuf {i9100_model_mbuf *next;std::vector<uint8_t> data;};
static kern_ctl_reg registration;
static i9100_bt_control *control;
static unsigned kernel_registered,kernel_connections,mbufs_freed,register_error,deregister_error,enqueue_busy,copy_error;
static int current_pid=35;
static uint64_t current_unique=12345;
static unsigned current_uid=501;
static const char *current_name="BTServer";
static bool wrong_uuid;
static bool connect_rx_race;
static std::vector<uint8_t> socket_rx;
void IOLockLock(IOLock *p){assert(IOLockTryLock(p));}
extern "C" size_t strlcpy(char *dst,const char *src,size_t n)
{size_t len=strlen(src);if(n){size_t count=len<n-1?len:n-1;memcpy(dst,src,count);dst[count]=0;}return len;}
extern "C" int proc_selfpid(){return current_pid;}
extern "C" uint64_t proc_selfuniqueid(){return current_unique;}
extern "C" void proc_selfname(char *p,int n){assert(n>0);strlcpy(p,current_name,(size_t)n);}
extern "C" proc_t proc_self(){return &current_pid;}
extern "C" int proc_rele(proc_t p){assert(p==&current_pid);return 0;}
extern "C" void proc_getexecutableuuid(proc_t p,unsigned char *out,unsigned long n)
{assert(p==&current_pid&&n==16);const uint8_t uuid[]={0xaf,0x01,0x50,0xd5,0x08,0x5e,0x32,0x65,0x98,0xed,0xff,0x07,0x2a,0x23,0xd1,0x46};
 memcpy(out,uuid,n);if(wrong_uuid)out[0]^=1;}
extern "C" kauth_cred_t kauth_cred_get(){return &current_uid;}
extern "C" unsigned kauth_cred_getuid(kauth_cred_t c){assert(c==&current_uid);return current_uid;}
extern "C" int ctl_register(kern_ctl_reg *r,kern_ctl_ref *out)
{assert(!kernel_registered&&!strcmp(r->ctl_name,"com.apple.uart.bluetooth")&&r->ctl_flags==4U&&r->ctl_sendsize==32768U&&r->ctl_recvsize==32768U);
 if(register_error)return EEXIST;registration=*r;kernel_registered=1;*out=&registration;return 0;}
extern "C" int ctl_deregister(kern_ctl_ref ref)
{assert(ref==&registration&&kernel_registered);if(kernel_connections||deregister_error)return EBUSY;kernel_registered=0;return 0;}
extern "C" int ctl_enqueuedata(kern_ctl_ref ref,uint32_t unit,void *data,size_t bytes,uint32_t flags)
{assert(ref==&registration&&unit==1U&&kernel_registered&&kernel_connections&&data&&bytes&&flags==0U);
 if(enqueue_busy)return ENOBUFS;const uint8_t *p=(const uint8_t *)data;socket_rx.insert(socket_rx.end(),p,p+bytes);return 0;}
extern "C" void mbuf_freem(mbuf_t m)
{while(m){auto *next=m->next;delete m;mbufs_freed++;m=next;}}
extern "C" mbuf_t mbuf_next(mbuf_t m){assert(m);return m->next;}
extern "C" size_t mbuf_len(mbuf_t m){assert(m);return m->data.size();}
extern "C" int mbuf_copydata(mbuf_t m,size_t offset,size_t count,void *out)
{if(copy_error)return EFAULT;assert(!offset);size_t copied=0;while(m){size_t take=m->data.size();assert(take<=count-copied);
 memcpy((uint8_t *)out+copied,m->data.data(),take);copied+=take;m=m->next;}assert(copied==count);return 0;}
extern "C" int __real_i9100_bt_worker_stream_open(i9100_bt_worker *,const void *);
extern "C" int __wrap_i9100_bt_worker_stream_open(i9100_bt_worker *w,const void *client)
{int e=__real_i9100_bt_worker_stream_open(w,client);
 if(!e&&connect_rx_race){uint8_t event[]={4,0xff,1,0xab};peer.fifo.insert(peer.fifo.end(),event,event+4);ns++;bt_loop->tick();
  assert(!worker_status().fault&&worker_status().stream.rx_queued==4U&&socket_rx.empty());}return e;}
static mbuf_t fragment(const uint8_t *p,unsigned bytes,mbuf_t next=nullptr)
{return new i9100_model_mbuf{next,std::vector<uint8_t>(p,p+bytes)};}
static void control_setup(i9100_peripheral_native **c,i9100_radio_native **r,unsigned host_reset=0)
{
 start_context(c,r);auto p=wp();p.allow_host_reset=host_reset;assert(!i9100_bt_worker_create(1,*r,&p,firmware,sizeof(firmware),&integrity,&output,&worker));
 register_error=deregister_error=enqueue_busy=copy_error=mbufs_freed=0;current_pid=35;current_unique=12345;current_uid=501;current_name="BTServer";wrong_uuid=connect_rx_race=false;socket_rx.clear();
}
static void ready_control()
{assert(!i9100_bt_worker_start(worker));for(unsigned n=0;n<700U&&!worker_status().ready;n++){ns+=1000000;bt_loop->tick();}assert(worker_status().ready);}
static sockaddr_ctl address(){sockaddr_ctl a={32,32,2,43,1,{}};return a;}
static struct i9100_bt_control_status control_status()
{struct i9100_bt_control_status s;assert(!i9100_bt_control_status(control,&s));return s;}
static void native_attributes(void *token,const char *name,int expected)
{
 const char *hex=getenv(name);if(!hex)return;assert(strlen(hex)==88U);uint8_t raw[44];
 for(unsigned n=0;n<44;n++){char pair[3]={hex[2*n],hex[2*n+1],0};char *end=nullptr;
  unsigned long value=strtoul(pair,&end,16);assert(end==pair+2&&value<=255U);raw[n]=(uint8_t)value;}
 assert(registration.ctl_setopt(&registration,1,token,0x802c7414,raw,44)==expected);
}
static void native_reset_handoff(i9100_peripheral_native *&c,i9100_radio_native *&r)
{
 control_setup(&c,&r,1);assert(!i9100_bt_control_create(1,worker,&control));ready_control();
 auto a=address();void *token=nullptr;assert(!registration.ctl_connect(&registration,&a,&token));kernel_connections=1;
 peer.post_reset_packets=3;peer.post_reset_mtu=512;
 uint8_t request[]={1,3,0x0c,0};const char *hex=getenv("I9100_BT_RESET_REQUEST_HEX");
 if(hex){assert(strlen(hex)==8U);for(unsigned n=0;n<4;n++){char pair[3]={hex[2*n],hex[2*n+1],0};request[n]=(uint8_t)strtoul(pair,nullptr,16);}}
 assert(!memcmp(request,"\1\3\14\0",4));unsigned before=peer.commands;uint64_t started=ns;
 auto m=fragment(request,2,fragment(request+2,2));assert(!registration.ctl_send(&registration,1,token,m,0));
 ns++;bt_loop->tick();assert(peer.commands==before+1U&&!worker_status().ready&&socket_rx.empty());
 m=fragment(request,4);assert(registration.ctl_send(&registration,1,token,m,0)==EWOULDBLOCK&&peer.commands==before+1);
 ns++;bt_loop->tick();assert(worker_status().boot.protocol_epoch==2U&&!worker_status().acl_ready&&socket_rx.empty());
 for(unsigned n=0;n<150U&&!worker_status().ready;n++){ns+=1000000;bt_loop->tick();}
 auto state=worker_status();assert(state.ready&&state.acl_ready&&state.acl.protocol_epoch==2&&state.acl.available[0]==3);
 const uint8_t actual[]={4,0x0e,4,1,3,0x0c,0};
 assert(socket_rx.size()==7U&&!memcmp(socket_rx.data(),actual,7)&&peer.commands==before+6U&&state.boot.generation==1);
 assert(ns-started<1500000000ULL&&state.boot.host_reset_confirmed==1U);
 const char *directory=getenv("I9100_BT_HANDOFF_OUTPUT");if(directory){char name[1024];
  int n=snprintf(name,sizeof(name),"%s/native-handoff.json",directory);assert(n>0&&(unsigned)n<sizeof(name));FILE *f=fopen(name,"w");assert(f);
  fprintf(f,"{\"schema\":1,\"kind\":\"i9100-bt-native-handoff\",\"result\":\"PASS\",\"model_fixture\":true,\"hardware_tested\":false,\"request_hex\":\"%02x%02x%02x%02x\",\"reply_hex\":\"",request[0],request[1],request[2],request[3]);
  for(uint8_t byte:socket_rx)fprintf(f,"%02x",byte);
  fprintf(f,"\",\"commands\":%u,\"elapsed_ns\":%llu,\"physical_generation\":%u,\"protocol_epoch\":%u,\"acl_mtu\":%u,\"acl_packets\":%u}\n",peer.commands-before,
   (unsigned long long)(ns-started),state.boot.generation,state.acl.protocol_epoch,state.acl.mtu[0],state.acl.packets[0]);
  assert(!fclose(f));}
 assert(!registration.ctl_disconnect(&registration,1,token));kernel_connections=0;
 assert(!i9100_bt_control_destroy(&control)&&!control);finish_worker(c,r);
}
int main()
{
 assert(!bt_worker_reference_main());i9100_peripheral_native *c=nullptr;i9100_radio_native *r=nullptr;
 control_setup(&c,&r);unsigned before=allocations;
 assert(i9100_bt_control_create(0,worker,&control)==I9100_BTS_DISABLED&&!control&&allocations==before);
 assert(!i9100_bt_control_create(1,worker,&control)&&control&&kernel_registered&&worker_status().stream_attached);
 assert(i9100_bt_worker_destroy(&worker)==I9100_BT_BOOT_BUSY&&worker);
 auto a=address();void *token=nullptr;
 assert(registration.ctl_connect(&registration,&a,&token)==EBUSY&&!token);ready_control();
 current_name="rogue";assert(registration.ctl_connect(&registration,&a,&token)==EPERM&&!token);current_name="BTServer";
 current_uid=0;assert(registration.ctl_connect(&registration,&a,&token)==EPERM);current_uid=501;
 wrong_uuid=true;assert(registration.ctl_connect(&registration,&a,&token)==EPERM);wrong_uuid=false;
 connect_rx_race=true;assert(!registration.ctl_connect(&registration,&a,&token)&&token);connect_rx_race=false;kernel_connections=1;
 ns++;bt_loop->tick();assert(socket_rx.size()==4U);socket_rx.clear();
 assert(control_status().accepted_connects==1U&&control_status().client_uniqueid==12345U);
 void *second=nullptr;a.sc_unit=2;assert(registration.ctl_connect(&registration,&a,&second)==EBUSY&&!second);a.sc_unit=1;
 uint8_t attrs[44];size_t count=43;
 assert(registration.ctl_getopt(&registration,1,token,0x402c7413,attrs,&count)==EMSGSIZE&&count==43U);count=44;
 assert(!registration.ctl_getopt(&registration,1,token,0x402c7413,attrs,&count)&&count==44);
 attrs[36]^=1;assert(registration.ctl_setopt(&registration,1,token,0x802c7414,attrs,44)==ENOTSUP);attrs[36]^=1;
 assert(!registration.ctl_setopt(&registration,1,token,0x802c7414,attrs,44));
 native_attributes(token,"I9100_BT_NATIVE_TERMIOS_HEX",0);
 native_attributes(token,"I9100_BT_NATIVE_REJECT_HEX",ENOTSUP);
 static const uint8_t cmd[]={1,1,0x10,0};before=peer.commands;
 auto m=fragment(cmd,2,fragment(cmd+2,2));assert(!registration.ctl_send(&registration,1,token,m,0)&&mbufs_freed==2);
 enqueue_busy=1;ns++;bt_loop->tick();ns++;bt_loop->tick();assert(peer.commands==before+1U&&socket_rx.empty()&&worker_status().stream.rx_queued==15U);
 enqueue_busy=0;ns++;bt_loop->tick();assert(socket_rx.size()==15U&&socket_rx[0]==4U&&!rx_delivered&&control_status().rx_enqueued==19U);
 current_unique++;m=fragment(cmd,4);assert(registration.ctl_send(&registration,1,token,m,0)==EPERM&&mbufs_freed==3U);current_unique--;
 copy_error=1;m=fragment(cmd,4);assert(registration.ctl_send(&registration,1,token,m,0)==EFAULT&&peer.commands==before+1U);copy_error=0;
 assert(i9100_bt_control_destroy(&control)==I9100_BTS_BUSY&&control);
 memory[0x13800000U][16/4]=0;assert(registration.ctl_disconnect(&registration,1,token)!=0&&!control_status().client_active);kernel_connections=0;
 assert(i9100_bt_control_destroy(&control)!=0&&control&&worker_status().session_hold);
 memory[0x13800000U][16/4]=4;assert(!i9100_bt_control_destroy(&control)&&!control&&!kernel_registered);
 finish_worker(c,r);
 control_setup(&c,&r);register_error=1;
 assert(i9100_bt_control_create(1,worker,&control)==EEXIST&&!control&&!worker_status().stream_attached);register_error=0;
 finish_worker(c,r);
 control_setup(&c,&r);assert(!i9100_bt_control_create(1,worker,&control));deregister_error=1;
 assert(i9100_bt_control_destroy(&control)==EBUSY&&control&&worker_status().stream_attached);deregister_error=0;
 assert(!i9100_bt_control_destroy(&control)&&!control);finish_worker(c,r);
 control_setup(&c,&r);assert(!i9100_bt_control_create(1,worker,&control));ready_control();
 supply.verify_fail_at=supply.verify_calls+1U;a=address();token=nullptr;
 assert(registration.ctl_connect(&registration,&a,&token)!=0&&!token&&worker_status().fault&&worker_status().stream.closed);
 supply.verify_fail_at=0;assert(!i9100_bt_control_destroy(&control)&&!control);finish_worker(c,r);
 for(unsigned point=1;point<=2;point++){
  control_setup(&c,&r);fail_alloc=allocations+point;
  assert(i9100_bt_control_create(1,worker,&control)==I9100_BTS_IO&&!control&&!kernel_registered);fail_alloc=0;finish_worker(c,r);
 }
 native_reset_handoff(c,r);
 puts("PASS actual kernel-control callbacks through actual worker/UART/H4:mobile BTServer identity,unique pid,44B options,mbuf fragments,real bytes/backpressure,client death/failed quiesce,registration/allocation/deregistration cleanup; BSD/socket/electrical facades");return 0;
}
