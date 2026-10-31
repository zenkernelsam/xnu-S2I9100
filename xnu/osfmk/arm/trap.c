/*
 * Copyright 2013, winocm. <winocm@icloud.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 *   If you are going to use this software in any form that does not involve
 *   releasing the source to this project or improving it, let me know beforehand.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ARM trap handlers.
 */

#include <mach/mach_types.h>
#include <mach/mach_traps.h>
#include <mach/thread_status.h>
#include <mach_assert.h>
#include <mach_kdp.h>
#include <kern/thread.h>
#include <kern/kalloc.h>
#include <stdarg.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <stdarg.h>
#include <machine/machine_routines.h>
#include <arm/misc_protos.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/boot.h>
#include <pexpert/arm/protos.h>
#include <vm/vm_fault.h>
#include <vm/vm_kern.h>         /* For kernel_map */
#include <libkern/OSByteOrder.h>
#include <libsa/string.h>
#include <arm/armops.h>
#if defined(BOARD_CONFIG_QSD8250_LEO)
#include "ios7lab_fault_witness.h"
#endif
struct proc;
extern char *proc_name_address(void *p);
extern int proc_pid(struct proc *p);

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
static int i9100_iolog_enabled(void)
{
    static int v = -1;
    if (v < 0) {
        int t = 0;
        PE_parse_boot_argn("i9100iolog", &t, sizeof(t));
        v = t ? 1 : 0;
    }
    return v > 0;
}
#endif

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"


/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)

#include <kern/misc_protos.h>
#include <libkern/OSAtomic.h>
#include <sys/errno.h>
static volatile UInt32 ios7leo_dyld_phase;
static task_t ios7leo_dyld_task;
static arm_saved_state_t ios7leo_dyld_state;
static boolean_t ios7leo_dyld_matches(thread_t thread,arm_saved_state_t *state,uint32_t instruction)
{
    uint64_t info;
    if(!thread || !thread->task || !thread->task->bsd_info ||
       (state->cpsr & 0x3fU)!=0x10U || instruction!=0xfedeffe7U ||
       proc_pid(thread->task->bsd_info)!=1 ||
       strcmp(proc_name_address(thread->task->bsd_info),"launchd")!=0 ||
       thread->task->all_image_info_size!=0xa4U) return FALSE;
    info=thread->task->all_image_info_addr;
    /* Current pinned dyld offsets; no assumed ASLR slide or direct user dereference. */
    if(info<0x20bc8U+0x1000U || info>=0x40000000ULL ||
       info+0xa4U>0x40000000ULL || info+0x278d8U+512U>0x40000000ULL) return FALSE;
    return state->pc==info-0x20bc8U && state->r[0]==info+0x278d8U;
}

static boolean_t ios7leo_dyld_repeat(task_t task,arm_saved_state_t *state)
{
    if(ios7leo_dyld_phase!=2U) return FALSE;
    OSMemoryBarrier();
    return task==ios7leo_dyld_task && bcmp(state,&ios7leo_dyld_state,sizeof(*state))==0;
}
__attribute__((noinline)) static void ios7leo_dyld_witness(task_t task,arm_saved_state_t *state)
{
    char message[512];vm_size_t copied=0;unsigned used=0,index;int error;
    if(!OSCompareAndSwap(0U,1U,&ios7leo_dyld_phase)) return;
    ios7leo_dyld_task=task;ios7leo_dyld_state=*state;
    bzero(message,sizeof(message));
    error=copyinstr((user_addr_t)state->r[0],message,sizeof(message),&copied);
    message[sizeof(message)-1]=0;
    printf("Leo DYLD witness err=%d len=%u trunc=%u\n",error,(unsigned)copied,(unsigned)(error==ENAMETOOLONG));
    if(error==0 || error==ENAMETOOLONG) {
        while(used<sizeof(message)-1 && message[used]) {
            unsigned char ch=(unsigned char)message[used];
            if(ch<0x20U || ch>0x7eU) message[used]='?';
            ++used;
        }
        for(index=0;index<used;index+=48U) {
            unsigned count=used-index;if(count>48U)count=48U;
            printf("dyld> %.*s\n",(int)count,message+index);
        }
        if(!used)printf("dyld> <empty>\n");
    } else printf("dyld> <copy failed; no data printed>\n");
    OSMemoryBarrier();ios7leo_dyld_phase=2U;
}

#endif
/* IOS7LEO_DYLD_END */
typedef enum {
    SLEH_ABORT_TYPE_PREFETCH_ABORT = 3,
    SLEH_ABORT_TYPE_DATA_ABORT = 4,
} sleh_abort_reasons;

void doexception(int exc, mach_exception_code_t code,
                 mach_exception_subcode_t sub)
{
    mach_exception_data_type_t codes[EXCEPTION_CODE_MAX];

    codes[0] = code;
    codes[1] = sub;
    exception_triage(exc, codes, 2);
}

/**
 * ifsr_to_human
 *
 * Return a human readable representation of the IFSR bits.
 */
static char *ifsr_to_human(uint32_t ifsr)
{
    switch ((ifsr & 0xF)) {
    case 0:
        return "No function, reset value";
    case 1:
        return "Alignment fault";
    case 2:
        return "Debug event fault";
    case 3:
        return "Access flag fault on section";
    case 4:
        return "No function";
    case 5:
        return "Translation fault on section";
    case 6:
        return "Access flag fault on page";
    case 7:
        return "Translation fault on page";
    case 8:
        return "Precise external abort";
    case 9:
        return "Domain fault on section";
    case 10:
        return "No function";
    case 11:
        return "Domain fault on page";
    case 12:
        return "External abort on translation, level one";
    case 13:
        return "Permission fault on section";
    case 14:
        return "External abort on translation, level two";
    case 15:
        return "Permission fault on page";
    default:
        return "Unknown";
    }
    return "Unknown";
}

/**
 * sleh_fatal_exception
 */
void sleh_fatal_exception(abort_information_context_t * arm_ctx, char *message)
{
#if defined(BOARD_CONFIG_QSD8250_LEO)
    extern int ios7leo_fatal_report(unsigned int,unsigned int,unsigned int,
        unsigned int,unsigned int,unsigned int,unsigned int,const char *);
    /* Saved exception fields from the original vector context, no new probes.
     * Ordinary fatal handler/Debugger/Halt_system below stays byte-for-byte. */
    if(arm_ctx) ios7leo_fatal_report(0xf102U,
       ((unsigned int)__builtin_return_address(0)) & ~1U,
       arm_ctx->pc,arm_ctx->lr,arm_ctx->far,arm_ctx->fsr,15U,message);
    else ios7leo_fatal_report(0xf102U,
       ((unsigned int)__builtin_return_address(0)) & ~1U,0,0,0,0,0,message);
#endif
    debug_mode = TRUE;
    printf("Fatal exception: %s\n", message);
    printf("ARM register state: (saved state %p)\n"
           "  r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
           "  r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
           "  r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
           " r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
           "cpsr: 0x%08x fsr: 0x%08x far: 0x%08x\n", arm_ctx,
           arm_ctx->r[0], arm_ctx->r[1], arm_ctx->r[2], arm_ctx->r[3],
           arm_ctx->r[4], arm_ctx->r[5], arm_ctx->r[6], arm_ctx->r[7],
           arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10], arm_ctx->r[11],
           arm_ctx->r[12], arm_ctx->sp, arm_ctx->lr, arm_ctx->pc, arm_ctx->cpsr,
           arm_ctx->fsr, arm_ctx->far);
    printf("Current thread: %p\n", current_thread());

    uint32_t ttbcr, ttbr0, ttbr1;
    __asm__ __volatile__("mrc p15, 0, %0, c2, c0, 0":"=r"(ttbr0));
    __asm__ __volatile__("mrc p15, 0, %0, c2, c0, 1":"=r"(ttbr1));
    __asm__ __volatile__("mrc p15, 0, %0, c2, c0, 2":"=r"(ttbcr));

    printf("Control registers:\n"
           "  ttbcr: 0x%08x  ttbr0:  0x%08x  ttbr1:  0x%08x\n",
           ttbcr, ttbr0, ttbr1);
    Debugger("fatal exception");
    printf("We are hanging here ...\n");

    Halt_system();
}

/**
 * sleh_abort
 *
 * Handle prefetch and data aborts. (EXC_BAD_ACCESS IS NOT HERE YET)
 */
/* IOS7LEO_INSTALLD_WITNESS_BEGIN */
#if BOARD_CONFIG_QSD8250_LEO
static volatile UInt32 ios7leo_installd_observed;
__attribute__((noinline)) static void
ios7leo_installd_fault(thread_t thread, const abort_information_context_t *ctx,
                      kern_return_t fault_result)
{
    uint32_t instruction[4] = {0,0,0,0};
    uint32_t pc=ctx->pc, far=ctx->far, fsr=ctx->fsr, psr=ctx->cpsr;
    int error;
    if(!thread || !thread->task || !thread->task->bsd_info ||
       (psr & 0x1fU)!=0x10U ||
       strcmp(proc_name_address(thread->task->bsd_info), "installd")!=0 ||
       !OSCompareAndSwap(0U,1U,&ios7leo_installd_observed)) return;
    /* One existing failed data abort, after the original IRQ-enable point.
     * No recovery/retry or protection change; preserve exception delivery. */
    printf("Leo installd fault result=%d FSR=%08x FAR=%08x\n",
           (int)fault_result,(unsigned)fsr,(unsigned)far);
    printf("Leo installd PC=%08x CPSR=%08x WNR=%u\n",
           (unsigned)pc,(unsigned)psr,(unsigned)((fsr>>11)&1U));
    if(pc<0x1004U || pc>0x40000000U-12U) {
        printf("Leo installd instruction range rejected\n");
        return;
    }
    error=copyin((user_addr_t)(pc-4U),instruction,sizeof(instruction));
    printf("Leo installd instruction copyin=%d start=%08x bytes=16\n",
           error,(unsigned)(pc-4U));
    if(error==0)
        printf("Leo installd LE32 %08x %08x %08x %08x\n",
               (unsigned)instruction[0],(unsigned)instruction[1],
               (unsigned)instruction[2],(unsigned)instruction[3]);
}
#endif
/* IOS7LEO_INSTALLD_WITNESS_END */

#if BOARD_CONFIG_QSD8250_LEO
#include "ios7leo_data_abort037.h"
#include "IOS7LeoAppFault057.h"
#endif

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
/*
 * BRING-UP (J1): report a per-fault line only once per distinct page so a
 * progressing fault walk is visible on the console without flooding it.
 */
static int
j1_uda_pg_once(unsigned int pg)
{
    static unsigned int set[64];
    static int n = 0;
    int i;

    for (i = 0; i < n; i++)
        if (set[i] == pg)
            return 0;
    if (n < 64)
        set[n++] = pg;
    return 1;
}
#endif

static int __abort_count = 0;
void sleh_abort(void *context, int reason)
{
#if BOARD_CONFIG_QSD8250_LEO
    kern_return_t leo_installd_fault_result=KERN_SUCCESS;
    boolean_t leo_installd_failed=FALSE;
    ios7leo037_data_snapshot_t leo037_data_snapshot;
    kern_return_t leo037_data_error=KERN_SUCCESS;
    uint32_t leo037_data_bad_address=0U;
    leo037_data_snapshot.valid=0U;
#endif
#if defined(BOARD_CONFIG_QSD8250_LEO)
    kern_return_t ios7lab_exception_code=KERN_SUCCESS;
    uint32_t leo037_prefetch_bad_address=0U;
#endif
    uint32_t dfsr = 0, dfar = 0, ifsr = 0, ifar = 0, cpsr, exception_type =
        0, exception_subcode = 0;
    abort_information_context_t *arm_ctx =
        (abort_information_context_t *) context;
    thread_t thread = current_thread();

    /*
     * Make sure we get the correct registers only if required.
     */
#if 0
    kprintf("sleh_abort: pc %x lr %x far %x fsr %x psr %x\n", arm_ctx->pc, arm_ctx->lr, arm_ctx->far, arm_ctx->fsr, arm_ctx->cpsr);
#endif
    if (reason == SLEH_ABORT_TYPE_DATA_ABORT) {
        dfsr = arm_ctx->fsr;
        dfar = arm_ctx->far;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
        /*
         * BRING-UP (i9100): dump the first aborts so we can see the
         * faulting kernel VA + which thread/stack is involved when
         * the abort-livelock begins.
         */
        {
            static int abt_seen;
            if (abt_seen < 40) {
                abt_seen++;
                kdb_printf("SLEH-ABT#%d pc=0x%x far=0x%x fsr=0x%x cpsr=0x%x th=%p kstk=0x%x uss=%p\n",
                           abt_seen, arm_ctx->pc, dfar, dfsr, arm_ctx->cpsr,
                           thread, thread ? thread->kernel_stack : 0,
                           thread ? (vm_offset_t)thread->machine.uss : 0);
            }
        }
#endif
#if BOARD_CONFIG_QSD8250_LEO
        if((arm_ctx->cpsr&0x1fU)==0x10U) {
            ios7leo037_data_entry(&leo037_data_snapshot,arm_ctx,thread);
            leo037_data_bad_address=dfar;
        }
#endif
    } else if (reason == SLEH_ABORT_TYPE_PREFETCH_ABORT) {
        ifsr = arm_ctx->fsr;
        ifar = arm_ctx->far;
#if BOARD_CONFIG_QSD8250_LEO
        leo037_prefetch_bad_address=arm_ctx->pc;
#endif
    } else {
        sleh_fatal_exception(arm_ctx, "sleh_abort: weird abort");
    }

    /*
     * We do not want anything entering sleh_abort recursively.
     */
    if (__abort_count != 0) {
        sleh_fatal_exception(arm_ctx, "sleh_abort: recursive abort");
    }
    __abort_count++;

    /*
     * Panic if it's an alignment fault?
     */
    if ((ifsr == 1) || (dfsr == 1)) {
        sleh_fatal_exception(arm_ctx, "sleh_abort: alignment fault");
    }

    if (!kernel_map) {
        sleh_fatal_exception(arm_ctx,
                             "sleh_abort: kernel map is NULL, probably a fault before vm_bootstrap?");
    }

    if (!thread) {
        sleh_fatal_exception(arm_ctx, "sleh_abort: current thread is null?");
    }

    if(ml_at_interrupt_context()) {
        sleh_fatal_exception(arm_ctx, "sleh_abort: Abort in interrupt handler");
    }

    /*
     * See if the abort was in Kernel or User mode.
     */
    cpsr = arm_ctx->cpsr & 0x1F;

    /*
     * Kernel mode. (ARM Supervisor)
     */
    if (cpsr == 0x13) {
        switch (reason) {
            /*
             * Prefetch aborts always include the IFSR and IFAR.
             */
        case SLEH_ABORT_TYPE_PREFETCH_ABORT:{
                /*
                 * Die in a fire.
                 */
                vm_map_t map;
                kern_return_t code;

                /*
                 * Get the kernel thread map.
                 */
                map = kernel_map;

                /*
                 * Attempt to fault the page.
                 */
                __abort_count--;
                code =
                    vm_fault(map, vm_map_trunc_page(arm_ctx->pc),
                             (VM_PROT_EXECUTE | VM_PROT_READ), FALSE,
                             THREAD_UNINT, NULL, vm_map_trunc_page(0));

                if (code != KERN_SUCCESS) {

                    if (current_debugger) {
                        if (kdp_raise_exception(EXC_BREAKPOINT, 0, 0, arm_ctx))
                            return;
                    }

                    /*
                     * Still, die in a fire.
                     */
                    panic_context(0, (void *) arm_ctx,
                                  "Kernel prefetch abort. (faulting address: 0x%08x, saved state 0x%08x)\n"
                                  "  r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
                                  "  r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
                                  "  r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
                                  " r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
                                  "cpsr: 0x%08x fsr: 0x%08x far: 0x%08x\n",
                                  ifar, arm_ctx, arm_ctx->r[0], arm_ctx->r[1],
                                  arm_ctx->r[2], arm_ctx->r[3], arm_ctx->r[4],
                                  arm_ctx->r[5], arm_ctx->r[6], arm_ctx->r[7],
                                  arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10],
                                  arm_ctx->r[11], arm_ctx->r[12], arm_ctx->sp,
                                  arm_ctx->lr, arm_ctx->pc, arm_ctx->cpsr, ifsr,
                                  ifar);
                }
                return;
            }
        case SLEH_ABORT_TYPE_DATA_ABORT:{
                vm_map_t map;
                kern_return_t code;

                /*
                 * Get the current thread map.
                 */
                map = thread->map;

                /*

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                {
                    static unsigned int j1sr_abk = 0;
                    if (j1sr_abk < 30) { j1sr_abk++;
                        printf("J1SR abk: #%u dfar=%08x dfsr=%08x pc=%08x lr=%08x sp=%08x cpsr=%08x\n",
                            j1sr_abk, (unsigned)dfar, (unsigned)dfsr, (unsigned)arm_ctx->pc,
                            (unsigned)arm_ctx->lr, (unsigned)arm_ctx->sp, (unsigned)arm_ctx->cpsr);
                    }
                }
#endif
                 * Attempt to fault the page.
                 */
                __abort_count--;
                code =
                    vm_fault(map, vm_map_trunc_page(dfar),
                             (dfsr & 0x800) ? (VM_PROT_READ | VM_PROT_WRITE)
                             : (VM_PROT_READ), FALSE, THREAD_UNINT, NULL,
                             vm_map_trunc_page(0));

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                if (code == KERN_SUCCESS) {
                    /* Bring-up (J1): the mapping is present in the active page
                     * table (L1/L2 valid) yet the CPU keeps taking a
                     * translation fault. Force a full local TLBIALL to drop the
                     * stale TLB entry before retrying the instruction. */
                    extern void arm_tlb_flushID(void);
                    arm_tlb_flushID();
                }
#endif

                if (code != KERN_SUCCESS) {
                    /*
                     * Still, die in a fire.
                     */
                    code =
                        vm_fault(kernel_map, vm_map_trunc_page(dfar),
                                 (dfsr & 0x800) ? (VM_PROT_READ | VM_PROT_WRITE)
                                 : (VM_PROT_READ), FALSE, THREAD_UNINT, NULL,
                                 vm_map_trunc_page(0));
                    if (code != KERN_SUCCESS) {
                        /*
                         * Attempt to fault the page against the kernel map.
                         */
                        if (!thread->recover) {
                            panic_context(0, (void *) arm_ctx,
                                          "Kernel data abort. (faulting address: 0x%08x, saved state 0x%08x)\n"
                                          "  r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
                                          "  r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
                                          "  r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
                                          " r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
                                          "cpsr: 0x%08x fsr: 0x%08x far: 0x%08x\n",
                                          dfar, arm_ctx, arm_ctx->r[0], arm_ctx->r[1],
                                          arm_ctx->r[2], arm_ctx->r[3],
                                          arm_ctx->r[4], arm_ctx->r[5],
                                          arm_ctx->r[6], arm_ctx->r[7],
                                          arm_ctx->r[8], arm_ctx->r[9],
                                          arm_ctx->r[10], arm_ctx->r[11],
                                          arm_ctx->r[12], arm_ctx->sp,
                                          arm_ctx->lr, arm_ctx->pc,
                                          arm_ctx->cpsr, dfsr, dfar);
                        } else {
                            /*
                             * If there's a recovery routine, use it.
                             */
                            if (thread->map == kernel_map)
                                panic
                                    ("Attempting to use a recovery routine on a kernel map thread");

                            if (!thread->map)
                                sleh_fatal_exception(arm_ctx,
                                                     "Current thread has no thread map, what?");

                            arm_ctx->pc = thread->recover;
                            arm_ctx->cpsr &= ~(1 << 5);
                            thread->recover = NULL;
                            return;
                        }
                    }
                }
                return;
            }
        default:
            panic("sleh_abort: unknown kernel mode abort, type %d\n", reason);
        }
        /*
         * User mode (ARM User)
         */
    } else if (cpsr == 0x10) {
        switch (reason) {
            /*
             * User prefetch abort
             */
        case SLEH_ABORT_TYPE_PREFETCH_ABORT:{
                /*
                 * Attempt to fault it. Same as data except address comes from IFAR.
                 */
                vm_map_t map;
                kern_return_t code;

                /*
                 * Get the current thread map.
                 */
                map = thread->map;
                /*
                 * Attempt to fault the page.
                 */
                assert(get_preemption_level() == 0);
                __abort_count--;
                code =
                    vm_fault(map, vm_map_trunc_page(arm_ctx->pc),
                             (VM_PROT_EXECUTE | VM_PROT_READ), FALSE,
                             THREAD_UNINT, NULL, vm_map_trunc_page(0));

                /*
                 * Additionally, see if we can fault one page higher as the instruction
                 * may be on a page split boundary. libobjc and all require this???
                 *
                 * Prefaulting the instruction before allows the prefetch mechanism
                 * to not abort.
                 */
                if((arm_ctx->pc & 0xfff) >= 0xff0)
                    vm_fault(map, vm_map_trunc_page(arm_ctx->pc) + PAGE_SIZE,
                             (VM_PROT_EXECUTE | VM_PROT_READ), FALSE,
                             THREAD_UNINT, NULL, vm_map_trunc_page(0));

                if ((code != KERN_SUCCESS) && (code != KERN_ABORTED)) {
                    exception_type = EXC_BAD_ACCESS;
                    exception_subcode = 0;
#if defined(BOARD_CONFIG_QSD8250_LEO)
                    ios7lab_exception_code = code;
#endif

                    /*
                     * Debug only.
                     */
                    printf
                        (ANSI_COLOR_RED "%s[%d]: " ANSI_COLOR_YELLOW "usermode prefetch abort, EXC_BAD_ACCESS at 0x%08x in map %p (pmap %p) (%s)" ANSI_COLOR_RESET" \n",
                         proc_name_address(thread->task->bsd_info),
                         proc_pid(thread->task->bsd_info), arm_ctx->pc, map,
                         map->pmap, ifsr_to_human(ifsr));
#if BOARD_CONFIG_QSD8250_LEO
                    {
                        const abort_information_context_t *diag =
                            (reason == SLEH_ABORT_TYPE_DATA_ABORT && leo037_data_snapshot.valid)
                            ? &leo037_data_snapshot.entry : arm_ctx;
                        printf("APPCTX057 EXC_BAD_ACCESS PID=%d SRC=%s PC=%08x LR=%08x SP=%08x R0_R3=%08x,%08x,%08x,%08x PSR=%08x FSR=%08x FAR=%08x RESULT=%x\n",
                            proc_pid(thread->task->bsd_info),
                            (reason == SLEH_ABORT_TYPE_DATA_ABORT && leo037_data_snapshot.valid) ? "ENTRY" : "CURRENT",
                            (unsigned)diag->pc,(unsigned)diag->lr,(unsigned)diag->sp,
                            (unsigned)diag->r[0],(unsigned)diag->r[1],(unsigned)diag->r[2],(unsigned)diag->r[3],
                            (unsigned)diag->cpsr,(unsigned)diag->fsr,(unsigned)diag->far,(unsigned)code);
                    }
#endif
                    printf("Thread has ARM register state:\n"
                           "    r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
                           "    r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
                           "    r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
                           "   r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
                           "  cpsr: 0x%08x\n", arm_ctx->r[0], arm_ctx->r[1],
                           arm_ctx->r[2], arm_ctx->r[3], arm_ctx->r[4],
                           arm_ctx->r[5], arm_ctx->r[6], arm_ctx->r[7],
                           arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10],
                           arm_ctx->r[11], arm_ctx->r[12], arm_ctx->sp,
                           arm_ctx->lr, arm_ctx->pc, arm_ctx->cpsr);
                    printf("dyld_all_image_info_addr: 0x%08x   dyld_all_image_info_size: 0x%08x\n",
                            thread->task->all_image_info_addr, thread->task->all_image_info_size);

                } else {
                    /*
                     * Retry execution of instruction.
                     */
                    ml_set_interrupts_enabled(TRUE);
                    return;
                }
                break;
            }
            /*
             * User Data Abort
             */
        case SLEH_ABORT_TYPE_DATA_ABORT:{
                /*
                 * Attempt to fault it. Same as instruction except address comes from DFAR.
                 */
                vm_map_t map;
                kern_return_t code;

                /*
                 * Get the current thread map.
                 */
                map = thread->map;

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                {
                    static unsigned int uda_total = 0;
                    uda_total++;
                    if (0 && j1_uda_pg_once((unsigned)dfar >> 12))
                        printf("UDA: far=%08x page=%08x pc=%08x lr=%08x fsr=%08x cpsr=%08x\n",
                            (unsigned)dfar, (unsigned)(dfar & ~0xfffu),
                            (unsigned)arm_ctx->pc, (unsigned)arm_ctx->lr,
                            (unsigned)dfsr, (unsigned)arm_ctx->cpsr);
                    if ((uda_total & 511U) == 0 && i9100_iolog_enabled())
                        printf("UDA: total faults=%u\n", uda_total);
                }
#endif

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                {
                    static unsigned int u_far = ~0u, u_pc = 0, u_fsr = 0, u_cnt = 0;
                    if (dfar == u_far && arm_ctx->pc == u_pc && dfsr == u_fsr) {
                        if (++u_cnt == 8)
                            printf("UDA: LIVELOCK far=%08x pc=%08x fsr=%08x (fault not resolving)\n",
                                (unsigned)dfar, (unsigned)arm_ctx->pc, (unsigned)dfsr);
                    } else {
                        u_far = (unsigned)dfar; u_pc = (unsigned)arm_ctx->pc;
                        u_fsr = (unsigned)dfsr; u_cnt = 1;
                    }
                }
#endif

#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                {
                    static unsigned int j1sr_abu = 0;
                    if (j1sr_abu < 30) { j1sr_abu++;
                        printf("J1SR abu: #%u dfar=%08x dfsr=%08x pc=%08x lr=%08x sp=%08x cpsr=%08x\n",
                            j1sr_abu, (unsigned)dfar, (unsigned)dfsr, (unsigned)arm_ctx->pc,
                            (unsigned)arm_ctx->lr, (unsigned)arm_ctx->sp, (unsigned)arm_ctx->cpsr);
                    }
                }
#endif
                /*
                 * Attempt to fault the page.
                 */
                assert(get_preemption_level() == 0);
                __abort_count--;
                code =
                    vm_fault(map, vm_map_trunc_page(dfar),
                             (dfsr & 0x800) ? (VM_PROT_READ | VM_PROT_WRITE)
                             : (VM_PROT_READ), FALSE, THREAD_UNINT, NULL,
                             vm_map_trunc_page(0));
#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                if (code == KERN_SUCCESS) {
                    /* Same stale-TLB workaround as the kernel data-abort path:
                     * the mapping is present (probe: active page table, valid
                     * L1/L2) but the CPU re-faults. Drop the stale TLB entry. */
                    extern void arm_tlb_flushID(void);
                    arm_tlb_flushID();
                }
#endif
                if ((code != KERN_SUCCESS) && (code != KERN_ABORTED)) {
#if BOARD_CONFIG_QSD8250_LEO
                    leo_installd_fault_result=code;
                    leo_installd_failed=TRUE;
                    leo037_data_error=code;
                    if(leo037_data_snapshot.valid)
                        ios7leo037_data_after_vm(&leo037_data_snapshot,arm_ctx,thread);
#endif
#if BOARD_CONFIG_ARMPBA8
                    /* Bounded ordinary crash backtrace for the lab's UI process.
                     * Copy only frame links from the faulting thread's own stack;
                     * preserve the original exception path and stop on any error. */
                    {
                        static unsigned ios7lab_ui_crash_traces;
                        if (!strncmp(proc_name_address(thread->task->bsd_info), "SpringBoard", 12) &&
                            ios7lab_ui_crash_traces < 2 && arm_ctx->sp < 0x3fff0000U) {
                            uint32_t frame = arm_ctx->r[7];
                            uint32_t ceiling = arm_ctx->sp + 65536U;
                            unsigned depth;
                            ++ios7lab_ui_crash_traces;
                            for (depth = 0; depth < 10; ++depth) {
                                uint32_t link[2];
                                if ((frame & 3U) || frame < arm_ctx->sp || frame > ceiling - sizeof(link)) break;
                                if (copyin((void *)(uintptr_t)frame, link, sizeof(link))) break;
                                printf("IOS7LAB UI crash frame=%u return=%08x\n", depth, link[1]);
                                if (link[0] <= frame) break;
                                frame = link[0];
                            }
                        }
                    }
#endif
                    exception_type = EXC_BAD_ACCESS;
                    exception_subcode = 0;

                    /*
                     * Only for debug.
                     */
                    printf
                        (ANSI_COLOR_RED "%s[%d]: " ANSI_COLOR_BLUE "usermode data abort, EXC_BAD_ACCESS at 0x%08x in map %p (pmap %p) (%s)" ANSI_COLOR_RESET "\n",
                         proc_name_address(thread->task->bsd_info),
                         proc_pid(thread->task->bsd_info), dfar, map, map->pmap,
                         ifsr_to_human(dfsr));
#if BOARD_CONFIG_QSD8250_LEO
                    {
                        const abort_information_context_t *diag =
                            (reason == SLEH_ABORT_TYPE_DATA_ABORT && leo037_data_snapshot.valid)
                            ? &leo037_data_snapshot.entry : arm_ctx;
                        printf("APPCTX057 EXC_BAD_ACCESS PID=%d SRC=%s PC=%08x LR=%08x SP=%08x R0_R3=%08x,%08x,%08x,%08x PSR=%08x FSR=%08x FAR=%08x RESULT=%x\n",
                            proc_pid(thread->task->bsd_info),
                            (reason == SLEH_ABORT_TYPE_DATA_ABORT && leo037_data_snapshot.valid) ? "ENTRY" : "CURRENT",
                            (unsigned)diag->pc,(unsigned)diag->lr,(unsigned)diag->sp,
                            (unsigned)diag->r[0],(unsigned)diag->r[1],(unsigned)diag->r[2],(unsigned)diag->r[3],
                            (unsigned)diag->cpsr,(unsigned)diag->fsr,(unsigned)diag->far,(unsigned)code);
                    }
#endif
                    printf("Thread has ARM register state:\n"
                           "    r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
                           "    r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
                           "    r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
                           "   r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
                           "  cpsr: 0x%08x\n", arm_ctx->r[0], arm_ctx->r[1],
                           arm_ctx->r[2], arm_ctx->r[3], arm_ctx->r[4],
                           arm_ctx->r[5], arm_ctx->r[6], arm_ctx->r[7],
                           arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10],
                           arm_ctx->r[11], arm_ctx->r[12], arm_ctx->sp,
                           arm_ctx->lr, arm_ctx->pc, arm_ctx->cpsr);
                    printf("dyld_all_image_info_addr: 0x%08x   dyld_all_image_info_size: 0x%08x\n",
                            thread->task->all_image_info_addr, thread->task->all_image_info_size);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
                    {
                        extern boolean_t vm_map_lookup_entry(vm_map_t, vm_map_address_t, vm_map_entry_t *);
                        unsigned addrs[3] = { (unsigned)arm_ctx->pc, (unsigned)arm_ctx->lr, (unsigned)arm_ctx->sp };
                        const char *names[3] = { "pc", "lr", "sp" };
                        int ai;
                        for (ai = 0; ai < 3; ai++) {
                            vm_map_entry_t fe = (vm_map_entry_t)0;
                            unsigned a = addrs[ai] & ~1U;
                            if (vm_map_lookup_entry(map, (vm_map_address_t)a, &fe) && fe)
                                kprintf("JCRASH %s=%08x entry=[%08x-%08x] off=%llx obj=%p pager=%p\n",
                                        names[ai], a,
                                        (unsigned)fe->vme_start, (unsigned)fe->vme_end,
                                        (unsigned long long)fe->offset,
                                        fe->object.vm_object,
                                        fe->object.vm_object ? fe->object.vm_object->pager : 0);
                            else
                                kprintf("JCRASH %s=%08x unmapped\n", names[ai], a);
                        }
                        {
                            unsigned short iw[4];
                            if (copyin((user_addr_t)(arm_ctx->pc & ~1U), (char *)iw, 8) == 0)
                                kprintf("JCRASH insn=%04x %04x %04x %04x\n",
                                        iw[0], iw[1], iw[2], iw[3]);
                        }
                        {
                            extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
                            /* file offset 0xc127442 = real launch_data_free;
                             * VA that SHOULD map it through this entry: */
                            vm_map_entry_t fe2 = (vm_map_entry_t)0;
                            if (vm_map_lookup_entry(map,
                                (vm_map_address_t)((unsigned)arm_ctx->pc
                                                   & ~1U), &fe2) && fe2) {
                                unsigned trueva =
                                    (unsigned)fe2->vme_start +
                                    (0xc127442U -
                                     (unsigned)fe2->offset);
                                kprintf("JCRASH trueva=%08x pp=%x\n",
                                        trueva & ~0xfffU,
                                        pmap_find_phys(map->pmap,
                                        (addr64_t)(trueva & ~0xfffU)));
                                {
                                    unsigned short jw[8];
                                    if (copyin((user_addr_t)trueva,
                                               (char *)jw, 16) == 0)
                                        kprintf("JCRASH trueinsn=%04x "
                                                "%04x %04x %04x %04x %04x "
                                                "%04x %04x\n",
                                                jw[0], jw[1], jw[2], jw[3],
                                                jw[4], jw[5], jw[6], jw[7]);
                                }
                            }
                            kprintf("JCRASH phys pc=%08x pp=%x\n",
                                    (unsigned)arm_ctx->pc & ~0xfffU,
                                    pmap_find_phys(map->pmap,
                                    (addr64_t)((unsigned)arm_ctx->pc
                                               & ~0xfffU)));
                            /* descend sub-maps for pc AND lr */
                            {
                                unsigned probe2[2] = {
                                    (unsigned)arm_ctx->pc & ~1U,
                                    (unsigned)arm_ctx->lr & ~1U };
                                const char *pn2[2] = { "pc", "lr" };
                                int pi;
                                for (pi = 0; pi < 2; pi++) {
                                    vm_map_entry_t fe3 = (vm_map_entry_t)0;
                                    vm_map_t m3 = map;
                                    vm_map_address_t a3 = probe2[pi];
                                    int sub = 0;
                                    while (sub < 4 &&
                                           vm_map_lookup_entry(m3, a3,
                                                               &fe3) && fe3) {
                                        kprintf("JCRASH %s ent%d va=%08x "
                                                "[%08x-%08x] off=%llx sub=%d\n",
                                                pn2[pi], sub, (unsigned)a3,
                                                (unsigned)fe3->vme_start,
                                                (unsigned)fe3->vme_end,
                                                (unsigned long long)
                                                fe3->offset,
                                                fe3->is_sub_map);
                                        if (!fe3->is_sub_map) break;
                                        a3 = (a3 - fe3->vme_start) +
                                             fe3->offset;
                                        m3 = fe3->object.sub_map;
                                        sub++;
                                    }
                                    if (fe3 && !fe3->is_sub_map) {
                                        vm_object_t o3 =
                                            fe3->object.vm_object;
                                        int depth = 0;
                                        while (o3 && depth < 4) {
                                            kprintf("JCRASH %s obj%d=%p "
                                                    "int=%d pager=%p "
                                                    "poff=%llx shdw=%p\n",
                                                    pn2[pi], depth, o3,
                                                    o3->internal, o3->pager,
                                                    (unsigned long long)
                                                    o3->paging_offset,
                                                    o3->shadow);
                                            o3 = o3->shadow;
                                            depth++;
                                        }
                                    }
                                }
                                /* instructions at lr-8 and pc */
                                {
                                    unsigned short lw[8];
                                    if (copyin((user_addr_t)
                                               (probe2[1] - 8),
                                               (char *)lw, 16) == 0)
                                        kprintf("JCRASH lrinsn=%04x %04x "
                                                "%04x %04x %04x %04x\n",
                                                lw[0], lw[1], lw[2], lw[3],
                                                lw[4], lw[5]);
                                }
                            }
                            /* dump 16 bytes at PC inside its phys page */
                            {
                                ppnum_t pp = pmap_find_phys(map->pmap,
                                    (addr64_t)((unsigned)arm_ctx->pc
                                               & ~0xfffU));
                                if (pp) {
                                    unsigned char *kv =
                                        (unsigned char *)phys_to_virt(
                                            (unsigned long)pp * 4096UL)
                                        + ((unsigned)arm_ctx->pc & 0xfffU);
                                    kprintf("JCRASH pgbytes="
                                            "%02x%02x%02x%02x%02x%02x"
                                            "%02x%02x%02x%02x%02x%02x"
                                            "%02x%02x%02x%02x\n",
                                            kv[0], kv[1], kv[2], kv[3],
                                            kv[4], kv[5], kv[6], kv[7],
                                            kv[8], kv[9], kv[10], kv[11],
                                            kv[12], kv[13], kv[14], kv[15]);
                                }
                            }
                        }
                    }
#endif
                } else {
                    /*
                     * Retry execution of instruction.
                     */
                    ml_set_interrupts_enabled(TRUE);
                    return;
                }
                break;
            }
        default:
            exception_type = EXC_BREAKPOINT;
            exception_subcode = 0;
            break;
        }
        /*
         * Unknown mode.
         */
    } else {
        panic("sleh_abort: Abort in unknown mode, cpsr: 0x%08x\n", cpsr);
    }

    /*
     * If there was a user exception, handle it.
     */
    if (exception_type) {
        ml_set_interrupts_enabled(TRUE);
#if BOARD_CONFIG_QSD8250_LEO
        if(leo_installd_failed)
            ios7leo_installd_fault(thread,arm_ctx,leo_installd_fault_result);
        if (exception_type == EXC_BAD_ACCESS &&
            reason == SLEH_ABORT_TYPE_DATA_ABORT &&
            leo037_data_snapshot.valid &&
            leo037_data_error != KERN_SUCCESS &&
            leo037_data_error != KERN_ABORTED) {
            /* Mach BAD_ACCESS code is the actual VM failure; subcode is the
             * original bad address saved before VM fault handling.  This
             * repairs delivery metadata, not the initial data access. */
            ios7leo037_failed_data_witness(thread,&leo037_data_snapshot,
                leo037_data_error,leo037_data_bad_address);
            ios7leo057_failed_app_data(thread,&leo037_data_snapshot,
                leo037_data_error,leo037_data_bad_address);
            ios7leo037_publish_exception_state(thread,arm_ctx,
                SLEH_ABORT_TYPE_DATA_ABORT,
                leo037_data_snapshot.entry.fsr,leo037_data_bad_address);
            doexception(exception_type,leo037_data_error,leo037_data_bad_address);
        } else if (exception_type == EXC_BAD_ACCESS &&
            reason == SLEH_ABORT_TYPE_PREFETCH_ABORT &&
            cpsr == 0x10U &&
            ios7lab_exception_code != KERN_SUCCESS &&
            ios7lab_exception_code != KERN_ABORTED) {
            /* EXC_BAD_ACCESS code is the kern_return_t and subcode is the
             * bad address.  Capture remains first-SB-only, but the proven
             * LEO user-prefetch exception contract is deterministic for all
             * failed user prefetches. */
            if ((arm_ctx->cpsr & 0x1fU) == 0x10U &&
                ios7lab_exception_code != KERN_SUCCESS &&
                ios7lab_exception_code != KERN_ABORTED &&
                ios7lab_fault_witness_is_springboard(thread, arm_ctx))
                (void)ios7lab_fault_witness_capture(thread, arm_ctx, ifsr, ifar,
                                                     ios7lab_exception_code,
                                                     0U, 0U);
            ios7leo037_publish_exception_state(thread,arm_ctx,
                SLEH_ABORT_TYPE_PREFETCH_ABORT,ifsr,ifar);
            doexception(exception_type, ios7lab_exception_code, leo037_prefetch_bad_address);
        } else
#endif
        doexception(exception_type, exception_subcode, 0);
    }

    /*
     * Done.
     */
    return;
}

/* Keep the 031 witness in the trap translation unit so its fixed BSS/ring ABI
 * is independently reviewed with trap.o and cannot drift as a second TU. */
#if defined(BOARD_CONFIG_QSD8250_LEO)
#include "ios7lab_fault_witness.c"
#endif

/**
 * irq_handler
 *
 * Handle irqs and pass them over to the platform expert.
 */
boolean_t irq_handler(void *context)
{
    /*
     * Increase/decrease CPU interrupt level.
     */
    cpu_data_t *datap = current_cpu_datap();
    assert(datap);
#if defined(BOARD_CONFIG_QSD8250_LEO)
    ios7lab_irq_observe(context);
#endif
    datap->cpu_interrupt_level++;

    /*
     * Disable system preemption, dispatch the interrupt and go.
     */
    __disable_preemption();

    /*
     * Dispatch the interrupt.
     */
    boolean_t ret = pe_arm_dispatch_interrupt(context);

    /*
     * Go.
     */
    datap->cpu_interrupt_level--;

    __enable_preemption();

    return ret;
}

void irq_iokit_dispatch(uint32_t irq)
{
    cpu_data_t *datap = current_cpu_datap();
    if(datap->handler) {
        datap->handler(datap->target, NULL, datap->nub, irq);
    }
}

/**
 * sleh_undef
 *
 * Handle undefined instructions and VFP usage.
 */
void sleh_undef(arm_saved_state_t * state)
{
/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)
    boolean_t leo_dyld_match=FALSE,leo_dyld_skip_dump=FALSE;
    int leo_instruction_error=-1;
#endif
/* IOS7LEO_DYLD_END */

    uint32_t cpsr, exception_type = 0, exception_subcode = 0;
    arm_saved_state_t *arm_ctx = (arm_saved_state_t *) state;
    thread_t thread = current_thread();

    if (!thread) {
        panic("sleh_undef: current thread is NULL\n");
    }

    /*
     * See if the abort was in Kernel or User mode. 
     */
    cpsr = arm_ctx->cpsr & 0x1F;

    /*
     * Kernel mode. (ARM Supervisor) 
     */
    if (cpsr == 0x13) {
        /*
         * Fall through to bad kernel handler. 
         */
        panic_context(0, (void *) arm_ctx,
                      "Kernel undefined instruction. (saved state 0x%08x)\n"
                      "  r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
                      "  r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
                      "  r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
                      " r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
                      "cpsr: 0x%08x\n", arm_ctx, arm_ctx->r[0], arm_ctx->r[1],
                      arm_ctx->r[2], arm_ctx->r[3], arm_ctx->r[4],
                      arm_ctx->r[5], arm_ctx->r[6], arm_ctx->r[7],
                      arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10],
                      arm_ctx->r[11], arm_ctx->r[12], arm_ctx->sp, arm_ctx->lr,
                      arm_ctx->pc, arm_ctx->cpsr);
    } else if (cpsr == 0x10) {
        vm_map_t map;
        uint32_t instruction, thumb_offset;
        /*
         * Get the current thread map. 
         */
        map = thread->map;

        /*
         * Get the current instruction. Do not let the pmaps change.
         */
        spl_t spl = splhigh();
        thumb_offset = (arm_ctx->cpsr & (1 << 5)) ? 1 : 0;

/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)
        leo_instruction_error =
#endif
/* IOS7LEO_DYLD_END */
        copyin((uint8_t *) (arm_ctx->pc + thumb_offset), &instruction,
               sizeof(uint32_t));
        splx(spl);

        /* i should really fix this crap properly........ */

        /*
         * Check the instruction encoding to see if it's a coprocessor instruction.
         */
        instruction = OSSwapInt32(instruction);

        /*
         * dyld's faulting one. I really just need to redo all of the VFP detection
         * code, which will happen one day... I just hate myself for this.
         */
        if(instruction != 0xfedeffe7 && !thread->machine.vfp_enable)
        {
            /*
             * NEON instruction (lazy VFP enable; a second undef at the same
             * spot with VFP already on is a real undefined instruction).
             */
            thread->machine.vfp_dirty = 0;
            vfp_enable_exception(TRUE);
            vfp_context_load(&thread->machine.vfp_regs);
            /*
             * Continue user execution.
             */
            thread->machine.vfp_enable = TRUE;
            return;
        }


/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)
        leo_dyld_match=leo_instruction_error==0 && ios7leo_dyld_matches(thread,arm_ctx,instruction);
        leo_dyld_skip_dump=leo_dyld_match && ios7leo_dyld_repeat(thread->task,arm_ctx);
        if(!leo_dyld_skip_dump) {
#endif
/* IOS7LEO_DYLD_END */
        printf
            (ANSI_COLOR_RED "%s[%d]: " ANSI_COLOR_GREEN "usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x%08x in map %p (pmap %p)" ANSI_COLOR_RESET "\n",
             proc_name_address(thread->task->bsd_info),
             proc_pid(thread->task->bsd_info), arm_ctx->pc, map, map->pmap);
        printf("Thread has ARM register state:\n"
               "    r0: 0x%08x  r1: 0x%08x  r2: 0x%08x  r3: 0x%08x\n"
               "    r4: 0x%08x  r5: 0x%08x  r6: 0x%08x  r7: 0x%08x\n"
               "    r8: 0x%08x  r9: 0x%08x r10: 0x%08x r11: 0x%08x\n"
               "   r12: 0x%08x  sp: 0x%08x  lr: 0x%08x  pc: 0x%08x\n"
               "  cpsr: 0x%08x\n", arm_ctx->r[0], arm_ctx->r[1], arm_ctx->r[2],
               arm_ctx->r[3], arm_ctx->r[4], arm_ctx->r[5], arm_ctx->r[6],
               arm_ctx->r[7], arm_ctx->r[8], arm_ctx->r[9], arm_ctx->r[10],
               arm_ctx->r[11], arm_ctx->r[12], arm_ctx->sp, arm_ctx->lr,
               arm_ctx->pc, arm_ctx->cpsr);
        printf("dyld_all_image_info_addr: 0x%08x   dyld_all_image_info_size: 0x%08x\n",
            thread->task->all_image_info_addr, thread->task->all_image_info_size);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
        {
            extern void i9100_trap_dump(void);
            static unsigned trhist_dumped;
            if (trhist_dumped < 2) { trhist_dumped++; i9100_trap_dump(); }
        }
        if (i9100_iolog_enabled()) {
            extern boolean_t vm_map_lookup_entry(vm_map_t, vm_map_address_t, vm_map_entry_t *);
            unsigned addrs[3] = { (unsigned)arm_ctx->pc, (unsigned)arm_ctx->lr, (unsigned)arm_ctx->sp };
            const char *names[3] = { "pc", "lr", "sp" };
            int ai;
            for (ai = 0; ai < 3; ai++) {
                vm_map_entry_t fe = (vm_map_entry_t)0;
                unsigned a = addrs[ai] & ~1U;
                if (vm_map_lookup_entry(map, (vm_map_address_t)a, &fe) && fe)
                    kprintf("JUNDEF %s=%08x entry=[%08x-%08x] off=%llx obj=%p pager=%p\n",
                            names[ai], a,
                            (unsigned)fe->vme_start, (unsigned)fe->vme_end,
                            (unsigned long long)fe->offset,
                            fe->object.vm_object,
                            fe->object.vm_object ? fe->object.vm_object->pager : 0);
                else
                    kprintf("JUNDEF %s=%08x unmapped\n", names[ai], a);
            }
            {
                extern void *vm_shared_region_get(task_t);
                extern kern_return_t vm_shared_region_start_address(void *, unsigned long long *);
                extern void vm_shared_region_deallocate(void *);
                void *srg = vm_shared_region_get(thread->task);
                unsigned long long sa = 0;
                if (srg && vm_shared_region_start_address(srg, &sa) == KERN_SUCCESS && sa) {
                    vm_map_entry_t fe3 = (vm_map_entry_t)0;
                    if (vm_map_lookup_entry(map, (vm_map_address_t)sa, &fe3) && fe3)
                        kprintf("JUNDEF cache@[%x] entry=[%08x-%08x] off=%llx obj=%p\n",
                                (unsigned)sa,
                                (unsigned)fe3->vme_start, (unsigned)fe3->vme_end,
                                (unsigned long long)fe3->offset, fe3->object.vm_object);
                    else
                        kprintf("JUNDEF cache@[%x] unmapped in this process\n", (unsigned)sa);
                    {
                        unsigned char cw[16];
                        if (copyin((user_addr_t)sa, (char *)cw, 16) == 0)
                            kprintf("JUNDEF cachehdr=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                                    cw[0],cw[1],cw[2],cw[3],cw[4],cw[5],cw[6],cw[7]);
                        else
                            kprintf("JUNDEF cachehdr @%x copyin fail\n", (unsigned)sa);
                    }
                    {
                        char want[128]; unsigned char ih[8];
                        memset(want, 0, sizeof(want));
                        if (copyin((user_addr_t)arm_ctx->r[0], want, sizeof(want)-1) == 0) {
                            const char *tag = "Library not loaded: ";
                            char *wp = want;
                            while (*wp && strncmp(wp, tag, 20) != 0) wp++;
                            if (*wp) {
                                char *e;
                                wp += 20;
                                for (e = wp; *e && *e != '\n' && *e != '\r' && *e != '\t' && *e != ' '; e++) ;
                                *e = '\0';
                                if (copyin((user_addr_t)(sa+0x18), (char *)ih, 8) == 0) {
                                    unsigned ioff = *(unsigned *)ih;
                                    unsigned icnt = *(unsigned *)(ih+4);
                                    unsigned i, found = 0, tblfail = 0, pfail = 0, dumped = 0, pgd = 0;
                                    for (i = 0; i < icnt && i < 2048; i++) {
                                        unsigned char ent[32]; char ps[128];
                                        if (copyin((user_addr_t)(sa+ioff+i*32), (char *)ent, 32) != 0) {
                                            tblfail++; break;
                                        }
                                        {
                                            unsigned pfo = *(unsigned *)&ent[24];
                                            if (!pfo || pfo > 0x10000000) continue;
                                            memset(ps, 0, sizeof(ps));
                                            if (copyin((user_addr_t)(sa+pfo), ps, sizeof(ps)-1) != 0) {
                                                pfail++; continue;
                                            }
                                            if (dumped < 6 && (i < 3 || ps[0] == 0 || ps[0] != '/')) {
                                                unsigned hexb[8]; int hb;
                                                vm_map_entry_t fe4 = (vm_map_entry_t)0;
                                                for (hb = 0; hb < 8; hb++) hexb[hb] = ((unsigned char *)ps)[hb];
                                                kprintf("JUNDEF img[%u] pfo=%x addr=%llx str=<%s> hex=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                                                        i, pfo, *(unsigned long long *)ent, ps,
                                                        hexb[0],hexb[1],hexb[2],hexb[3],hexb[4],hexb[5],hexb[6],hexb[7]);
                                                {
                                                    extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
                                                    extern unsigned long gPhysBase, gVirtBase;
                                                    ppnum_t pn = pmap_find_phys(map->pmap, (addr64_t)(sa+pfo));
                                                    kprintf("JUNDEF   findphys va=%x pn=%x\n", (unsigned)(sa+pfo), (unsigned)pn);
                                                    if (pn) {
                                                        unsigned char *kvb = (unsigned char *)(((vm_offset_t)pn << PAGE_SHIFT) - gPhysBase + gVirtBase);
                                                        unsigned char *kv = kvb + ((unsigned)(sa+pfo) & 0xfff);
                                                        kprintf("JUNDEF   kv=%p pg=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                                                                kv, kv[0],kv[1],kv[2],kv[3],kv[4],kv[5],kv[6],kv[7]);
                                                        if (!pgd && (ps[0] == 0 || ps[0] != 47)) { pgd = 1;
                                                            int po;
                                                            for (po = 0; po < 4096; po += 32)
                                                                kprintf("PGD %04x %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
                                                                        po,
                                                                        kvb[po+0],kvb[po+1],kvb[po+2],kvb[po+3],kvb[po+4],kvb[po+5],kvb[po+6],kvb[po+7],
                                                                        kvb[po+8],kvb[po+9],kvb[po+10],kvb[po+11],kvb[po+12],kvb[po+13],kvb[po+14],kvb[po+15],
                                                                        kvb[po+16],kvb[po+17],kvb[po+18],kvb[po+19],kvb[po+20],kvb[po+21],kvb[po+22],kvb[po+23],
                                                                        kvb[po+24],kvb[po+25],kvb[po+26],kvb[po+27],kvb[po+28],kvb[po+29],kvb[po+30],kvb[po+31]);
                                                        }
                                                    }
                                                }
                                                if (vm_map_lookup_entry(map, (vm_map_address_t)(sa+pfo), &fe4) && fe4) {
                                                    kprintf("JUNDEF   va=%x ent=[%x-%x] off=%llx sub=%d obj=%p pager=%p\n",
                                                            (unsigned)(sa+pfo), (unsigned)fe4->vme_start,
                                                            (unsigned)fe4->vme_end, (unsigned long long)fe4->offset,
                                                            fe4->is_sub_map,
                                                            fe4->object.vm_object,
                                                            (!fe4->is_sub_map && fe4->object.vm_object) ? fe4->object.vm_object->pager : 0);
                                                    if (fe4->is_sub_map && fe4->object.sub_map) {
                                                        vm_map_entry_t fe5 = (vm_map_entry_t)0;
                                                        vm_map_address_t sva = (vm_map_address_t)(sa+pfo) - fe4->vme_start + (vm_map_address_t)fe4->offset;
                                                        if (vm_map_lookup_entry(fe4->object.sub_map, sva, &fe5) && fe5)
                                                            kprintf("JUNDEF     sub-ent=[%x-%x] off=%llx sub=%d obj=%p pager=%p\n",
                                                                    (unsigned)fe5->vme_start, (unsigned)fe5->vme_end,
                                                                    (unsigned long long)fe5->offset, fe5->is_sub_map,
                                                                    fe5->object.vm_object,
                                                                    (!fe5->is_sub_map && fe5->object.vm_object) ? fe5->object.vm_object->pager : 0);
                                                        else
                                                            kprintf("JUNDEF     sub-map NO-ENTRY @%x\n", (unsigned)sva);
                                                        {
                                                            vm_map_entry_t it;
                                                            int ne = 0;
                                                            for (it = fe4->object.sub_map->hdr.links.next;
                                                                 it != vm_map_to_entry(fe4->object.sub_map) && ne < 12;
                                                                 it = it->vme_next, ne++)
                                                                kprintf("JUNDEF     submap[%d]=[%x-%x] off=%llx sub=%d obj=%p\n",
                                                                        ne, (unsigned)it->vme_start, (unsigned)it->vme_end,
                                                                        (unsigned long long)it->offset, it->is_sub_map,
                                                                        it->object.vm_object);
                                                        }
                                                    }
                                                } else
                                                    kprintf("JUNDEF   va=%x NO-ENTRY\n", (unsigned)(sa+pfo));
                                                dumped++;
                                            }
                                            if (strncmp(ps, wp, 100) == 0) {
                                                found = 1;
                                                kprintf("JUNDEF LOOKUP HIT img[%u] addr=%llx pfo=%x\n",
                                                        i, *(unsigned long long *)ent, pfo);
                                                break;
                                            }
                                        }
                                    }
                                    kprintf("JUNDEF LOOKUP want=\"%s\" icnt=%u found=%u tblfail=%u pfail=%u\n",
                                            wp, icnt, found, tblfail, pfail);
                                } else
                                    kprintf("JUNDEF LOOKUP imgtable hdr copyin fail\n");
                            }
                        }
                    }
                } else {
                    kprintf("JUNDEF no shared region (srg=%p sa=%x)\n", srg, (unsigned)sa);
                }
                if (srg) vm_shared_region_deallocate(srg);
            }
            {
                unsigned short iw[8];
                if (copyin((user_addr_t)(arm_ctx->pc & ~1U), (char *)iw, 16) == 0)
                    kprintf("JUNDEF insn=%04x %04x %04x %04x %04x %04x %04x %04x\n",
                            iw[0], iw[1], iw[2], iw[3], iw[4], iw[5], iw[6], iw[7]);
            }
            {
                char msg[128];
                int ri;
                for (ri = 0; ri < 4; ri++) {
                    memset(msg, 0, sizeof(msg));
                    if (copyin((user_addr_t)arm_ctx->r[ri], msg, sizeof(msg)-1) == 0 &&
                        msg[0] >= 0x20 && msg[0] < 0x7f)
                        kprintf("JUNDEF r%d-str=\"%s\"\n", ri, msg);
                }
                {
                    unsigned stk[16]; int si, hit=0;
                    if (copyin((user_addr_t)arm_ctx->sp, (char *)stk, sizeof(stk)) == 0)
                        for (si = 0; si < 16 && hit < 4; si++) {
                            unsigned v = stk[si];
                            if (v < 0x10000 || v >= 0x40000000) continue;
                            memset(msg, 0, sizeof(msg));
                            if (copyin((user_addr_t)v, msg, sizeof(msg)-1) == 0 &&
                                msg[0] >= 0x20 && msg[0] < 0x7f) {
                                kprintf("JUNDEF sp+%d=%08x-str=\"%s\"\n", si*4, v, msg);
                                hit++;
                            }
                        }
                }
            }
        }
#endif
/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)
        }
#endif
/* IOS7LEO_DYLD_END */


        /*
         * xxx gate
         */
        exception_type = EXC_BAD_INSTRUCTION;
        exception_subcode = 0;
    } else if (cpsr == 0x17) {
        panic("sleh_undef: undefined instruction in system mode");
    }

    /*
     * If there was a user exception, handle it.
     */
    if (exception_type) {
        ml_set_interrupts_enabled(TRUE);
/* IOS7LEO_DYLD_BEGIN */
#if defined(BOARD_CONFIG_QSD8250_LEO)
        if(leo_dyld_match) ios7leo_dyld_witness(thread->task,arm_ctx);
#endif
/* IOS7LEO_DYLD_END */

        doexception(exception_type, exception_subcode, 0);
    }

    /*
     * Done.
     */
    return;
}

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
extern queue_head_t tasks;
decl_lck_mtx_data(extern,tasks_threads_lock)
int i9100_kill_pid;
extern void psignal(struct proc *, int);
void i9100_dump_thread_states(void)
{
    task_t task;
    lck_mtx_lock(&tasks_threads_lock);
    queue_iterate(&tasks, task, task_t, tasks) {
        if (task == kernel_task) continue;
        int pid = task->bsd_info ? proc_pid((struct proc *)task->bsd_info) : -1;
        if (pid <= 0) continue;
        if (i9100_kill_pid && pid == i9100_kill_pid) {
            kprintf("J1KILL pid=%d\n", pid);
            psignal((struct proc *)task->bsd_info, 9);
            i9100_kill_pid = 0;
        }
        kprintf("J1TASK pid=%d nthreads=%d\n", pid, task->thread_count);
        thread_t th;
        queue_iterate(&task->threads, th, thread_t, task_threads) {
            arm_saved_state_t *iss = th->machine.iss;
            kprintf("J1THR pid=%d th=%x st=%x wq=%x ev=%llx pc=%x lr=%x\n",
                pid, (unsigned)(uintptr_t)th, th->state,
                (unsigned)(uintptr_t)th->wait_queue,
                (unsigned long long)th->wait_event,
                iss ? iss->pc : 0, iss ? iss->lr : 0);
        }
    }
    lck_mtx_unlock(&tasks_threads_lock);
}
#endif
