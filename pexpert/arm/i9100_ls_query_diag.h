/* Observations of the original LS14012/14112 path, with no RPC changes.
 * A separate ring prevents later icon registrations from hiding a query.
 * Callers must supply an already validated kernel message extent. */
#ifndef I9100_LS_QUERY_DIAG_H
#define I9100_LS_QUERY_DIAG_H
#define I9100_LS_QUERY_CAPACITY 32U
#define I9100_LS_QUERY_BYTES 352U
#define I9100_LS_QUERY_STRIDE 104U
extern volatile uint32_t i9100_ls_query_diag[1+I9100_LS_QUERY_CAPACITY*I9100_LS_QUERY_STRIDE];
extern volatile uint32_t i9100_ls_query_alloc_diag[1+32*12];
extern volatile uint32_t i9100_ls_query_alloc_failure_diag[1+8*12];
unsigned i9100_ls_query_record(unsigned stage,const unsigned char *source,
    unsigned available,unsigned size,unsigned pid,unsigned thread,const char *name,
    unsigned enabled,unsigned sender,unsigned euid,unsigned option,unsigned result,unsigned address,unsigned expected_id);
void i9100_ls_query_finish(unsigned token,unsigned pid,unsigned thread,unsigned result);
void i9100_ls_query_allocation(unsigned pid,unsigned thread,unsigned target,
    unsigned address,unsigned size_low,unsigned size_high,unsigned flags,
    unsigned result,unsigned backend,unsigned before,unsigned after);
#ifdef I9100_LS_QUERY_IMPLEMENTATION
/* seq,pid,thread,stage,id,message-size,captured,bits,option,result,sender,euid,
 * completion-published,completion-result,destination,version; then352B. */
volatile uint32_t i9100_ls_query_diag[1+I9100_LS_QUERY_CAPACITY*I9100_LS_QUERY_STRIDE];
volatile uint32_t i9100_ls_query_alloc_diag[1+32*12];
volatile uint32_t i9100_ls_query_alloc_failure_diag[1+8*12];
static volatile UInt32 i9100_ls_query_guard,i9100_ls_query_alloc_guard;
unsigned i9100_ls_query_record(unsigned stage,const unsigned char *source,
    unsigned available,unsigned size,unsigned pid,unsigned thread,const char *name,
    unsigned enabled,unsigned sender,unsigned euid,unsigned option,unsigned result,unsigned address,unsigned expected_id)
{
    if(enabled!=1 || !source || !name || !pid || !thread || stage<1 || stage>6 ||
       available<24 || size<24 || size>I9100_LS_QUERY_BYTES || size>available ||
       (strcmp(name,"lsd") && strcmp(name,"locationd") && strcmp(name,"SpringBoard")))return 0;
    uint32_t id,bits;memcpy(&id,source+20,4);memcpy(&bits,source,4);
    if(stage==6 && (expected_id==14012 || expected_id==14112))id=expected_id;
    else if(expected_id)return 0;
    if(id!=14012 && id!=14112)return 0;
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_query_guard))return 0;
    unsigned seq=i9100_ls_query_diag[0]+1U;if(!seq)seq=1;
    volatile uint32_t *r=&i9100_ls_query_diag[1+((seq-1U)&31U)*I9100_LS_QUERY_STRIDE];
    memset((void *)r,0,I9100_LS_QUERY_STRIDE*4);
    r[1]=pid;r[2]=thread;r[3]=stage;r[4]=id;r[5]=size;r[6]=size;r[7]=bits;
    r[8]=option;r[9]=result;r[10]=sender;r[11]=euid;r[14]=address;r[15]=1;
    memcpy((void *)&r[16],source,size);
    OSMemoryBarrier();r[0]=seq;i9100_ls_query_diag[0]=seq;
    OSMemoryBarrier();i9100_ls_query_guard=0;return seq;
}
void i9100_ls_query_finish(unsigned token,unsigned pid,unsigned thread,unsigned result)
{
    if(!token || !OSCompareAndSwap(0U,1U,&i9100_ls_query_guard))return;
    volatile uint32_t *r=&i9100_ls_query_diag[1+((token-1U)&31U)*I9100_LS_QUERY_STRIDE];
    if(r[0]==token && r[1]==pid && r[2]==thread && !r[12]){
        r[13]=result;OSMemoryBarrier();r[12]=1;
    }
    OSMemoryBarrier();i9100_ls_query_guard=0;
}
void i9100_ls_query_allocation(unsigned pid,unsigned thread,unsigned target,
    unsigned address,unsigned size_low,unsigned size_high,unsigned flags,
    unsigned result,unsigned backend,unsigned before,unsigned after)
{
    /* Called only from the existing opt-in lsd/4160B observer after its
     * original VM backend and copyout return; no extra user-memory reads. */
    if(!pid || !thread || size_low!=4160 || !OSCompareAndSwap(0U,1U,&i9100_ls_query_alloc_guard))return;
    unsigned seq=i9100_ls_query_alloc_diag[0]+1U;if(!seq)seq=1;
    uint32_t data[12]={seq,pid,thread,target,address,size_low,size_high,flags,result,backend,before,after};
    volatile uint32_t *r=&i9100_ls_query_alloc_diag[1+((seq-1U)&31U)*12];
    r[0]=0;memcpy((void *)&r[1],&data[1],11*4);OSMemoryBarrier();r[0]=seq;
    OSMemoryBarrier();i9100_ls_query_alloc_diag[0]=seq;
    if(result){
        unsigned count=i9100_ls_query_alloc_failure_diag[0];
        if(count<8){
            r=&i9100_ls_query_alloc_failure_diag[1+count*12];r[0]=0;
            memcpy((void *)&r[1],&data[1],11*4);OSMemoryBarrier();r[0]=seq;
            OSMemoryBarrier();i9100_ls_query_alloc_failure_diag[0]=count+1;
        }
    }
    OSMemoryBarrier();i9100_ls_query_alloc_guard=0;
}
#endif
#endif
