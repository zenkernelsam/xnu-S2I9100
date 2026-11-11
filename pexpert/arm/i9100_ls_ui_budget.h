/* Development receive deadline for the witnessed UI AppStore query.
 * The real message still goes to lsd; no response or result is invented. */
#ifndef I9100_LS_UI_BUDGET_H
#define I9100_LS_UI_BUDGET_H
static unsigned i9100_ls_ui_pick(const mach_msg_header_t *h,unsigned available,
    int enabled,const char *caller,mach_msg_option_t option,
    mach_msg_size_t send_size,mach_msg_size_t receive_size,mach_msg_timeout_t timeout)
{
    static const unsigned char ndr[8]={0,0,0,0,1,0,0,0};
    static const char bundle[]="com.apple.AppStore";
    if(!h || !caller || available<sizeof(*h)+8U+256U ||
       enabled!=1 || strcmp(caller,"SpringBoard") || h->msgh_id!=14012 ||
       send_size!=sizeof(*h)+8U+256U || receive_size!=48U || timeout ||
       option!=(MACH_SEND_MSG|MACH_RCV_MSG) ||
       h->msgh_bits!=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_MAKE_SEND_ONCE))return 0;
    const unsigned char *body=(const unsigned char *)(h+1);
    return !memcmp(body,ndr,sizeof(ndr)) && !memcmp(body+8,bundle,sizeof(bundle))?5000U:0;
}
static unsigned i9100_ls_ui_apply(unsigned budget,int peer,
    mach_msg_option_t *option,mach_msg_timeout_t *timeout)
{
    if(budget!=5000U || !peer || !option || !timeout || *timeout ||
       *option!=(MACH_SEND_MSG|MACH_RCV_MSG))return 0;
    *option|=MACH_RCV_TIMEOUT;*timeout=budget;return 1;
}
#ifdef I9100_LS_UI_IMPLEMENTATION
/* Rolling {seq,pid,thread,phase,result,option,budget,id}; phases1 bound,
 *2 original send result,3 original final receive/copyout result. */
volatile uint32_t i9100_ls_ui_diag[1+32*8];
static volatile UInt32 i9100_ls_ui_guard;
static uint32_t i9100_ls_ui_pending[8][2];
static void i9100_ls_ui_note_locked(unsigned pid,unsigned thread,unsigned phase,
    unsigned result,unsigned option)
{
    unsigned seq=i9100_ls_ui_diag[0]+1U;if(!seq)seq=1;
    volatile uint32_t *r=&i9100_ls_ui_diag[1+((seq-1U)&31U)*8];
    r[0]=0;r[1]=pid;r[2]=thread;r[3]=phase;r[4]=result;r[5]=option;r[6]=5000;r[7]=14012;
    OSMemoryBarrier();r[0]=seq;i9100_ls_ui_diag[0]=seq;
}
static void i9100_ls_ui_note(unsigned pid,unsigned thread,unsigned phase,
    unsigned result,unsigned option)
{
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_ui_guard))return;
    i9100_ls_ui_note_locked(pid,thread,phase,result,option);
    OSMemoryBarrier();i9100_ls_ui_guard=0;
}
static void i9100_ls_ui_begin(unsigned pid,unsigned thread)
{
    if(!pid || !thread || !OSCompareAndSwap(0U,1U,&i9100_ls_ui_guard))return;
    for(unsigned n=0;n<8;n++)if(i9100_ls_ui_pending[n][0]==thread &&
        i9100_ls_ui_pending[n][1]==pid){i9100_ls_ui_guard=0;return;}
    for(unsigned n=0;n<8;n++)if(!i9100_ls_ui_pending[n][0]){
        i9100_ls_ui_pending[n][0]=thread;i9100_ls_ui_pending[n][1]=pid;break;
    }
    OSMemoryBarrier();i9100_ls_ui_guard=0;
}
static void i9100_ls_ui_done(unsigned pid,unsigned thread,unsigned result,unsigned option)
{
    if(!OSCompareAndSwap(0U,1U,&i9100_ls_ui_guard))return;
    for(unsigned n=0;n<8;n++)if(i9100_ls_ui_pending[n][0]==thread &&
        i9100_ls_ui_pending[n][1]==pid){
        i9100_ls_ui_pending[n][0]=0;i9100_ls_ui_pending[n][1]=0;
        i9100_ls_ui_note_locked(pid,thread,3,result,option);break;
    }
    OSMemoryBarrier();i9100_ls_ui_guard=0;
}
#endif
#endif
