/* Actual ipc_kmsg_put source and self-map destination, before kmsg free.
 * No rights, signal, copy ownership or original result changes. */
#ifndef I9100_LS_DELIVERY_H
#define I9100_LS_DELIVERY_H
#define I9100_LS_DELIVERY_BYTES 1400U
#define I9100_LS_DELIVERY_STRIDE 716U
volatile uint32_t i9100_ls_delivery_diag[1+8*I9100_LS_DELIVERY_STRIDE];
static volatile UInt32 i9100_ls_delivery_guard;
static unsigned i9100_ls_delivery_begin(const unsigned char *source,unsigned available,
    unsigned size,uint64_t address,unsigned pid,unsigned thread,unsigned task,
    const char *name,unsigned enabled,unsigned safe,unsigned sender)
{
    if(!safe || enabled!=1 || !source || !name || !pid || !thread || !task ||
       size<24 || size>I9100_LS_DELIVERY_BYTES || size>available ||
       address<4096 || address>=0x40000000ULL || size>0x40000000ULL-address)return 0;
    uint32_t id;memcpy(&id,source+20,4);
    if(!((!strcmp(name,"lsd") && (id==14008 || id==14012)) ||
         (!strcmp(name,"SpringBoard") && (id==14108 || id==14112))))return 0;
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_delivery_guard))return 0;
    unsigned slot=i9100_ls_delivery_diag[0];
    if(slot>=8){i9100_ls_delivery_guard=0;return 0;}
    volatile uint32_t *r=&i9100_ls_delivery_diag[1+slot*I9100_LS_DELIVERY_STRIDE];
    memset((void *)r,0,I9100_LS_DELIVERY_STRIDE*4);
    r[1]=pid;r[2]=thread;r[3]=task;r[4]=id;r[5]=(uint32_t)address;r[6]=size;
    r[7]=UINT32_MAX;r[8]=UINT32_MAX;r[9]=0;r[10]=1;
    r[12]=sender;
    memcpy((void *)&r[16],source,size);
    OSMemoryBarrier();i9100_ls_delivery_diag[0]=slot+1;
    OSMemoryBarrier();i9100_ls_delivery_guard=0;return slot+1;
}
static void i9100_ls_delivery_end(unsigned token,unsigned pid,unsigned thread,
    unsigned task,unsigned result,unsigned safe)
{
    if(!token || token>8)return;
    volatile uint32_t *r=&i9100_ls_delivery_diag[1+(token-1)*I9100_LS_DELIVERY_STRIDE];
    if(r[0] || r[1]!=pid || r[2]!=thread || r[3]!=task)return;
    r[7]=result;
    if(!result && safe){
        r[8]=copyin((user_addr_t)r[5],(void *)&r[16+350],r[6]);
        if(!r[8]){
            r[9]=1;
            r[11]=memcmp((const void *)&r[16],(const void *)&r[16+350],r[6])!=0;
        }
    }
    OSMemoryBarrier();r[0]=token;
}
#endif
