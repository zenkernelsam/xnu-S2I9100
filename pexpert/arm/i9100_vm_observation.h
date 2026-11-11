/* First backboard VM calls, retained across its exit. Diagnostic metadata
 * only; the original syscall receives unchanged arguments and returns. */
#ifndef I9100_VM_OBSERVATION_H
#define I9100_VM_OBSERVATION_H
#define I9100_VM_OBSERVATION_CAP 256U
#define I9100_VM_OBSERVATION_STRIDE 28U
extern volatile uint32_t i9100_backboard_vm_diag[4+256*28];
/* QuartzCore's witnessed 4KiB allocation calls have a separate budget so
 * early dyld/malloc mappings cannot exhaust their evidence slots. */
extern volatile uint32_t i9100_ios6_slab_vm_diag[4+64*28];
#ifdef I9100_VM_OBSERVATION_IMPLEMENTATION
volatile uint32_t i9100_backboard_vm_diag[4+256*28];
volatile uint32_t i9100_ios6_slab_vm_diag[4+64*28];
static unsigned i9100_vm_row_enter(volatile uint32_t *table, unsigned capacity,
    unsigned pid, unsigned thread, unsigned code,
    unsigned pc, unsigned lr, uint64_t address, uint64_t length,
    unsigned protection, unsigned flags, unsigned fd, uint64_t position,
    unsigned argument_error, const uint32_t raw[7], unsigned nargs)
{
    if (table[3]) return 0;
    if (!table[1]) (void)OSCompareAndSwap(0U,pid,&table[1]);
    if (table[1] != pid) return 0;
    unsigned slot = table[0];
    if (slot >= capacity || !OSCompareAndSwap(slot,slot+1U,&table[0])) {
        OSIncrementAtomic((volatile SInt32 *)&table[2]);
        return 0;
    }
    volatile uint32_t *row = &table[4+slot*28];
    for (unsigned n=0;n<28;n++) row[n]=0;
    row[1]=pid;row[2]=thread;row[3]=code;row[4]=pc;row[5]=lr;
    row[6]=(uint32_t)address;row[7]=(uint32_t)(address>>32);
    row[8]=(uint32_t)length;row[9]=(uint32_t)(length>>32);
    row[10]=protection;row[11]=flags;row[12]=fd;
    row[13]=(uint32_t)position;row[14]=(uint32_t)(position>>32);
    row[15]=argument_error;row[27]=nargs;
    for (unsigned n=0;n<7;n++) row[20+n]=raw[n];
    return slot+1U;
}
static uint64_t i9100_vm_enter(unsigned pid, unsigned thread, unsigned code,
    unsigned pc, unsigned lr, uint64_t address, uint64_t length,
    unsigned protection, unsigned flags, unsigned fd, uint64_t position,
    unsigned argument_error, const uint32_t raw[7], unsigned nargs)
{
    if (!pid || !thread || i9100_backboard_vm_diag[3]) return 0;
    if (!i9100_backboard_vm_diag[1])
        (void)OSCompareAndSwap(0U,pid,&i9100_backboard_vm_diag[1]);
    if (i9100_backboard_vm_diag[1] != pid) return 0;
    unsigned slab = 0;
    unsigned slide = lr - 0x32ce23ddU;
    /* Supplied iOS6 syscall197 returns at392ccc40, QuartzCore calls at
     *32ce23d8 return (Thumb)32ce23dd. Both verified in the actual cache. */
    if (code == 197U && nargs == 7U && !argument_error &&
        !(slide & 4095U) && slide < 0x40000000U-0x392ccc40U &&
        pc == 0x392ccc40U+slide && !address && length == 4096U &&
        protection == 3U && flags == 0x1002U && fd == 0xffffffffU && !position)
        slab = i9100_vm_row_enter(i9100_ios6_slab_vm_diag,64U,pid,thread,code,pc,lr,
            address,length,protection,flags,fd,position,argument_error,raw,nargs);
    unsigned normal = i9100_vm_row_enter(i9100_backboard_vm_diag,
        I9100_VM_OBSERVATION_CAP,pid,thread,code,pc,lr,address,length,
        protection,flags,fd,position,argument_error,raw,nargs);
    return ((uint64_t)slab << 32) | normal;
}
static void i9100_vm_row_leave(volatile uint32_t *table, unsigned capacity,
                           unsigned sequence, unsigned pid, unsigned thread,
                           unsigned error, unsigned low, unsigned high)
{
    if (!sequence || sequence>capacity) return;
    volatile uint32_t *row=&table[4+(sequence-1U)*28];
    if (row[1]!=pid || row[2]!=thread || row[0]) return;
    row[16]=error;row[17]=low;row[18]=high;row[19]=1;
    OSMemoryBarrier();row[0]=sequence;
}
static void i9100_vm_leave(uint64_t token, unsigned pid, unsigned thread,
                           unsigned error, unsigned low, unsigned high)
{
    i9100_vm_row_leave(i9100_backboard_vm_diag,I9100_VM_OBSERVATION_CAP,
        (unsigned)token,pid,thread,error,low,high);
    i9100_vm_row_leave(i9100_ios6_slab_vm_diag,64U,
        (unsigned)(token>>32),pid,thread,error,low,high);
}
#endif
#endif
