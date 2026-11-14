/* Observe the native iOS6 Fig sandbox rendezvous (9393/9493) and player
 * creation (2020/2120) unchanged.
 * Callers provide a validated live kernel-message extent. No VM/port walk,
 * blocking lock, user copy, reply fabrication or timeout change occurs here.
 * The first 64 rows survive later process restarts and ordinary media traffic. */
#ifndef I9100_FIG_RPC_DIAG_H
#define I9100_FIG_RPC_DIAG_H
#define I9100_FIG_RPC_CAPACITY 64U
#define I9100_FIG_RPC_BYTES 128U
#define I9100_FIG_RPC_STRIDE 56U
extern volatile uint32_t i9100_fig_rpc_diag[1+64*56];
unsigned i9100_fig_rpc_record(unsigned,const unsigned char *,unsigned,unsigned,
    unsigned,unsigned,unsigned,const char *,unsigned,unsigned,unsigned,unsigned,
    unsigned,unsigned,unsigned,unsigned);
void i9100_fig_rpc_finish(unsigned,unsigned,unsigned,unsigned);
void i9100_fig_rpc_receive_done(unsigned,unsigned,unsigned);
#ifdef I9100_FIG_RPC_IMPLEMENTATION
volatile uint32_t i9100_fig_rpc_diag[1+64*56];
static volatile UInt32 i9100_fig_rpc_guard;
/* Each outstanding native request is associated only with its current
 * process/thread. A different delivery cannot inherit an old RPC identity. */
static uint32_t i9100_fig_rpc_pending[16][3];
unsigned i9100_fig_rpc_record(unsigned stage,const unsigned char *source,
    unsigned available,unsigned size,unsigned pid,unsigned thread,unsigned task,
    const char *name,unsigned enabled,unsigned option,unsigned result,
    unsigned address,unsigned peer,unsigned sender,unsigned euid,unsigned expected_id)
{
    if(enabled!=1 || !source || !name || !pid || !thread || !task ||
       stage<1 || stage>6 || available<24 || size<24 || size>available ||
       size>I9100_FIG_RPC_BYTES)return 0;
    unsigned role=!strcmp(name,"mediaserverd")?1U:!strcmp(name,"SpringBoard")?2U:0U;
    if(!role)return 0;
    uint32_t id,bits;memcpy(&id,source+20,4);memcpy(&bits,source,4);
    if(stage==6 && (expected_id==9393U || expected_id==9493U ||
       expected_id==2020U || expected_id==2120U))id=expected_id;
    else if(expected_id)return 0;
    if(id!=9393U && id!=9493U && id!=2020U && id!=2120U)return 0;
    if(!OSCompareAndSwap(0U,1U,&i9100_fig_rpc_guard))return 0;
    unsigned count=i9100_fig_rpc_diag[0];
    if(count>=I9100_FIG_RPC_CAPACITY){i9100_fig_rpc_guard=0;return 0;}
    unsigned seq=count+1;
    volatile uint32_t *r=&i9100_fig_rpc_diag[1+count*I9100_FIG_RPC_STRIDE];
    memset((void *)r,0,I9100_FIG_RPC_STRIDE*4);
    r[1]=1;r[2]=stage;r[3]=pid;r[4]=thread;r[5]=task;r[6]=role;r[7]=id;
    r[8]=size;r[9]=size;r[10]=bits;r[11]=option;r[12]=result;r[13]=address;
    r[14]=peer;r[20]=sender;r[21]=euid;
    memcpy((void *)&r[24],source,size);
    if(stage==2 && (id==9393U || id==2020U) && (option&2U)){
        unsigned slot=16;
        for(unsigned n=0;n<16;n++)
            if(i9100_fig_rpc_pending[n][0]==pid && i9100_fig_rpc_pending[n][1]==thread){slot=n;break;}
        if(slot==16)for(unsigned n=0;n<16;n++)
            if(!i9100_fig_rpc_pending[n][0]){slot=n;break;}
        if(slot<16){i9100_fig_rpc_pending[slot][0]=pid;
            i9100_fig_rpc_pending[slot][1]=thread;i9100_fig_rpc_pending[slot][2]=seq;}
    }
    for(unsigned n=0;n<16;n++)
        if(i9100_fig_rpc_pending[n][0]==pid && i9100_fig_rpc_pending[n][1]==thread){
            r[19]=i9100_fig_rpc_pending[n][2];break;
        }
    OSMemoryBarrier();r[0]=seq;i9100_fig_rpc_diag[0]=seq;
    OSMemoryBarrier();i9100_fig_rpc_guard=0;return seq;
}
void i9100_fig_rpc_finish(unsigned token,unsigned pid,unsigned thread,unsigned result)
{
    if(!token || token>I9100_FIG_RPC_CAPACITY ||
       !OSCompareAndSwap(0U,1U,&i9100_fig_rpc_guard))return;
    volatile uint32_t *r=&i9100_fig_rpc_diag[1+(token-1)*I9100_FIG_RPC_STRIDE];
    if(r[0]==token && r[3]==pid && r[4]==thread && !r[15]){
        r[16]=result;OSMemoryBarrier();r[15]=1;
    }
    OSMemoryBarrier();i9100_fig_rpc_guard=0;
}
void i9100_fig_rpc_receive_done(unsigned pid,unsigned thread,unsigned result)
{
    if(!i9100_fig_rpc_diag[0] || !OSCompareAndSwap(0U,1U,&i9100_fig_rpc_guard))return;
    for(unsigned n=0;n<16;n++){
        uint32_t *pending=i9100_fig_rpc_pending[n];
        if(pending[0]!=pid || pending[1]!=thread)continue;
        unsigned token=pending[2];
        if(token && token<=I9100_FIG_RPC_CAPACITY){
            volatile uint32_t *r=&i9100_fig_rpc_diag[1+(token-1)*I9100_FIG_RPC_STRIDE];
            if(r[0]==token && r[3]==pid && r[4]==thread && !r[17]){
                r[18]=result;OSMemoryBarrier();r[17]=1;
            }
        }
        memset(pending,0,sizeof(i9100_fig_rpc_pending[n]));break;
    }
    OSMemoryBarrier();i9100_fig_rpc_guard=0;
}
#endif
#endif
