/* lsd cache operations only. Observe existing syscall results and keep fd
 * provenance from successful opens; no file, argument or return changes. */
#ifndef I9100_LS_FILES_H
#define I9100_LS_FILES_H
volatile uint32_t i9100_ls_file_diag[1+64*112];
static volatile UInt32 i9100_ls_file_guard;
struct i9100_ls_fd_record {unsigned pid,fd;char path[192];};
static struct i9100_ls_fd_record i9100_ls_fds[8];
static int i9100_ls_file_match(const char *path)
{
    static const char caches[]="/var/mobile/Library/Caches";
    static const char app[]="/Applications/AppStore.app";
    if(!path)return 0;
    unsigned c=sizeof(caches)-1U,a=sizeof(app)-1U;
    return (!strncmp(path,caches,c) && (!path[c] || path[c]=='/')) ||
           (!strncmp(path,app,a) && (!path[a] || path[a]=='/'));
}
static void i9100_ls_file_publish(unsigned pid,unsigned thread,unsigned code,
    unsigned error,unsigned result,unsigned fd,unsigned flags,unsigned mode,
    unsigned count,const char *path,const char *path2,unsigned path_error,
    unsigned path2_error,unsigned pc,unsigned lr,unsigned kind)
{
    if(!pid || !thread || !OSCompareAndSwap(0U,1U,&i9100_ls_file_guard))return;
    const char *selected=path;
    if(!selected){
        for(unsigned n=0;n<8;n++)if(i9100_ls_fds[n].pid==pid && i9100_ls_fds[n].fd==fd){
            selected=i9100_ls_fds[n].path;break;
        }
    }
    if(!i9100_ls_file_match(selected))goto done;
    unsigned sequence=i9100_ls_file_diag[0]+1U;if(!sequence)sequence=1;
    volatile uint32_t *row=&i9100_ls_file_diag[1+((sequence-1U)&63U)*112];
    memset((void *)row,0,112*4);
    row[1]=pid;row[2]=thread;row[3]=code;row[4]=error;row[5]=result;row[6]=fd;
    row[7]=flags;row[8]=mode;row[9]=count;row[10]=path_error;row[11]=path2_error;
    row[12]=pc;row[13]=lr;row[14]=1;row[15]=1;
    strncpy((char *)&row[16],selected,191);
    if(path2)strncpy((char *)&row[64],path2,191);
    OSMemoryBarrier();row[0]=sequence;i9100_ls_file_diag[0]=sequence;
    if(kind==1 && !error){
        for(unsigned n=0;n<8;n++)if(i9100_ls_fds[n].pid==pid && i9100_ls_fds[n].fd==result)
            i9100_ls_fds[n].pid=0;
        for(unsigned n=0;n<8;n++)if(!i9100_ls_fds[n].pid){
            i9100_ls_fds[n].pid=pid;i9100_ls_fds[n].fd=result;
            strncpy(i9100_ls_fds[n].path,selected,191);i9100_ls_fds[n].path[191]=0;break;
        }
    }else if(kind==2 && !error){
        for(unsigned n=0;n<8;n++)if(i9100_ls_fds[n].pid==pid && i9100_ls_fds[n].fd==fd)
            i9100_ls_fds[n].pid=0;
    }
done:
    OSMemoryBarrier();i9100_ls_file_guard=0;
}
#endif
