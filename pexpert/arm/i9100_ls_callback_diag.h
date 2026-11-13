/* Observe the original LSD query callback without changing its reply policy.
 * User reads belong to the normal caller map, outside this record guard. */
#ifndef I9100_LS_CALLBACK_DIAG_H
#define I9100_LS_CALLBACK_DIAG_H
#define I9100_LS_CALLBACK_CAPACITY 64U
#define I9100_LS_CALLBACK_STRIDE 96U
#define I9100_LS_CALLBACK_REQUEST_BYTES 192U
extern volatile uint32_t i9100_ls_callback_diag[1+64*96];
void i9100_ls_callback_received(unsigned pid,unsigned thread,unsigned address,
    const unsigned char *data,unsigned size);
void i9100_ls_callback_clear(unsigned pid,unsigned thread);
unsigned i9100_ls_callback_lookup(unsigned pid,unsigned thread,uint32_t context[6]);
void i9100_ls_callback_event(const uint32_t fields[20],const unsigned char *request,
    const unsigned char *reply,const uint32_t frames[16]);
#ifdef I9100_LS_CALLBACK_IMPLEMENTATION
/* row: seq,version,stage,pid,thread,token,request-VA,reply-VA,vm-result,
 * request-copy-error,reply-copy-error,request-size,reply-name,kind,
 * user-PC,user-LR,user-SP,user-FP,callback-FP,stack-read-error;
 * then192B request prefix,48B reply prefix,8 {FP,LR} frame pairs.
 * Stages1 delivered,2 allocation completed,3 before deallocation,
 * 4 deallocation completed. Pending context is never used for mutations. */
volatile uint32_t i9100_ls_callback_diag[1+64*96];
static volatile UInt32 i9100_ls_callback_guard;
static uint32_t i9100_ls_callback_pending[8][8];
static unsigned i9100_ls_callback_controls;
static unsigned i9100_ls_callback_publish(const uint32_t fields[20],
    const unsigned char *request,const unsigned char *reply,const uint32_t frames[16])
{
    unsigned seq=i9100_ls_callback_diag[0]+1U;if(!seq)seq=1;
    volatile uint32_t *row=&i9100_ls_callback_diag[1+((seq-1U)&63U)*96];
    memset((void *)row,0,96*4);memcpy((void *)&row[1],&fields[1],19*4);
    if(request)memcpy((void *)&row[20],request,192);
    if(reply)memcpy((void *)&row[68],reply,48);
    if(frames)memcpy((void *)&row[80],frames,16*4);
    OSMemoryBarrier();row[0]=seq;i9100_ls_callback_diag[0]=seq;
    return seq;
}
void i9100_ls_callback_received(unsigned pid,unsigned thread,unsigned address,
    const unsigned char *data,unsigned size)
{
    static const unsigned char ndr[8]={0,0,0,0,1,0,0,0};
    if(!pid || !thread || address<4096 || address>=0x40000000U ||
       size!=352U || size>0x40000000U-address || !data)return;
    uint32_t header[6];memcpy(header,data,24);
    unsigned terminated=0;
    for(unsigned n=32;n<288;n++)if(!data[n]){terminated=1;break;}
    if(header[0]!=0x1112U || header[1]!=288U || !header[2] ||
       header[5]!=14012U || memcmp(data+24,ndr,8) || !terminated)return;
    unsigned kind=!memcmp(data+32,"com.apple.Preferences",sizeof("com.apple.Preferences"))?1U:
        !memcmp(data+32,"com.apple.AppStore",sizeof("com.apple.AppStore"))?2U:
        !memcmp(data+32,"com.apple.locationd",sizeof("com.apple.locationd"))?3U:0U;
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_callback_guard))return;
    unsigned slot=8;
    for(unsigned n=0;n<8;n++)if(i9100_ls_callback_pending[n][0]==pid &&
        i9100_ls_callback_pending[n][1]==thread){slot=n;break;}
    if(slot==8)for(unsigned n=0;n<8;n++)if(!i9100_ls_callback_pending[n][0]){slot=n;break;}
    if(slot==8)goto done;
    /* A later unrelated query clears association instead of attributing
     * its allocation to an earlier Preferences request. Keep two controls. */
    if(!kind || (kind==3 && i9100_ls_callback_controls>=2)){
        memset(i9100_ls_callback_pending[slot],0,8*4);goto done;
    }
    if(kind==3)++i9100_ls_callback_controls;
    uint32_t fields[20]={0,1,1,pid,thread,0,address,0,0,0,0,size,header[2],kind};
    unsigned token=i9100_ls_callback_publish(fields,data,0,0);
    uint32_t context[8]={pid,thread,token,address,0,size,header[2],kind};
    memcpy(i9100_ls_callback_pending[slot],context,sizeof(context));
done:
    OSMemoryBarrier();i9100_ls_callback_guard=0;
}
void i9100_ls_callback_clear(unsigned pid,unsigned thread)
{
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_callback_guard))return;
    for(unsigned n=0;n<8;n++)if(i9100_ls_callback_pending[n][0]==pid &&
        i9100_ls_callback_pending[n][1]==thread)
        memset(i9100_ls_callback_pending[n],0,8*4);
    OSMemoryBarrier();i9100_ls_callback_guard=0;
}
unsigned i9100_ls_callback_lookup(unsigned pid,unsigned thread,uint32_t context[6])
{
    unsigned token=0;
    if(!context || !OSCompareAndSwap(0U,1U,&i9100_ls_callback_guard))return 0;
    for(unsigned n=0;n<8;n++)if(i9100_ls_callback_pending[n][0]==pid &&
        i9100_ls_callback_pending[n][1]==thread){
        memcpy(context,&i9100_ls_callback_pending[n][2],6*4);token=context[0];break;
    }
    OSMemoryBarrier();i9100_ls_callback_guard=0;return token;
}
void i9100_ls_callback_event(const uint32_t fields[20],const unsigned char *request,
    const unsigned char *reply,const uint32_t frames[16])
{
    if(!fields || fields[1]!=1 || fields[2]<2 || fields[2]>4 ||
       !fields[3] || !fields[4] || !fields[5] ||
       !OSCompareAndSwap(0U,1U,&i9100_ls_callback_guard))return;
    for(unsigned n=0;n<8;n++)if(i9100_ls_callback_pending[n][0]==fields[3] &&
        i9100_ls_callback_pending[n][1]==fields[4] &&
        i9100_ls_callback_pending[n][2]==fields[5] &&
        i9100_ls_callback_pending[n][3]==fields[6]){
        (void)i9100_ls_callback_publish(fields,request,reply,frames);
        if(fields[2]==2 && !fields[8])i9100_ls_callback_pending[n][4]=fields[7];
        break;
    }
    OSMemoryBarrier();i9100_ls_callback_guard=0;
}
#endif
#endif
