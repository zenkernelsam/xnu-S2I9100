/* i9100 Exynos4210 PE (Galaxy S2 GT-I9100). HD2-architecture adaptation.
 * Validates loader handoff, maps FB/UART, installs GIC/MCT dispatch,
 * provides direct-FB display backend and fatal reporter.
 * Numeric addresses are factual Exynos 4210 hardware data. */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <mach/mach_types.h>
#include <mach/host_info.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm/boot.h>
#include <pexpert/arm/protos.h>
#include <pexpert/arm/i9100_handoff.h>
#include <pexpert/arm/i9100_entry.h>
#include <IOKit/IOPlatformExpert.h>
#include "i9100_timer.h"
#include "i9100_pmu_reset.h"
#include "i9100_framebuffer_copy.h"
#include "../../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_home_observation.h"
#include "../../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_touch_observation.h"
#include <machine/machine_routines.h>
#include "i9100_panic_context.h"
#include "i9100_previous_boot.h"
#include <kern/debug.h>
#include <kern/clock.h>
#include <mach/vm_param.h>

extern int switch_to_serial_console(void);
extern host_t host_self(void);
extern kern_return_t host_statistics64(host_t host, host_flavor_t flavor,
                                       host_info64_t info,
                                       mach_msg_type_number_t *count);
extern uint64_t sane_size;
extern unsigned int vm_page_free_count,vm_page_free_reserved,vm_page_free_target;
#include <vm/pmap.h>
extern vm_offset_t io_map_spec(vm_map_offset_t phys_addr, vm_size_t size, unsigned int flags);
extern void bcopy(const void *src, void *dest, size_t len);
extern int snprintf(char *buffer, size_t size, const char *format, ...);
#include "i9100_process_observation.h"
#include "../../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/i9100_mali.h"
#define I9100_ABORT_IMPLEMENTATION 1
#include "i9100_abort_observation.h"
#include "i9100_irq_snapshot.h"
extern vm_offset_t pmap_pte(pmap_t map, vm_offset_t address);

#ifndef VM_WIMG_WTHRU
#define VM_WIMG_WTHRU         (VM_MEM_WRITE_THROUGH | VM_MEM_COHERENT | VM_MEM_GUARDED)
#endif

static volatile unsigned int *i9100_pixels;
static vm_offset_t i9100_fb_map;
static vm_offset_t i9100_fimd_map;
static unsigned int i9100_fb_physical;
static volatile unsigned int i9100_graphics_owner, i9100_fatal_active;
static unsigned int i9100_stage;
static volatile unsigned int i9100_console_count;
static volatile unsigned int i9100_fatal_category;
static volatile unsigned int i9100_fatal_state[8];
static unsigned int i9100f_cx, i9100f_cy;
static int i9100f_remapped, i9100f_wiped;
volatile int i9100_panel_console_enabled = 1;
static unsigned int i9100_panic_text_mode;
static unsigned int i9100_panic_header_frozen, i9100_fatal_callsite;
static unsigned int i9100_panic_text_len, i9100_panic_text_truncated;
static unsigned int i9100_panic_reason, i9100_panic_reason_known;
static char i9100_panic_text[768];

void ios7i9100_fast_putc(char c);
void ios7i9100_panic_text_putc(char c);
void i9100_usbcdc_putc(char c);
void i9100_usbcdc_setmap(void);
static void i9100_puts(const char *s);
static void i9100_puthex(unsigned int v);

/* Independent of USB/printf: PMU INFORM6/7 and a reserved top-DRAM
 * record survive a warm reset. Map once while VM services are usable;
 * fatal paths only write existing mappings and never allocate. */
static vm_offset_t i9100_trace_pmu, i9100_trace_cell;
struct I9100PreviousBoot i9100_previous_boot;
static unsigned i9100_panic_recovery_enabled, i9100_recovery_started;
static unsigned i9100_recovery_inside, i9100_recovery_reset;
static uint64_t i9100_recovery_begin_ticks;
void i9100_usbcdc_panic_begin(unsigned,unsigned,unsigned,unsigned,unsigned,
    unsigned,unsigned,unsigned,uint64_t);
void i9100_usbcdc_panic_poll(void);
void i9100_usbcdc_panic_disable(void);
static void i9100_panic_prepare(unsigned category,unsigned callsite,
    unsigned pc,unsigned lr,unsigned far,unsigned fsr,unsigned cpsr)
{
    if (i9100_recovery_started) return;
    i9100_recovery_started = 1;
    i9100_recovery_begin_ticks = I9100_panic_ticks();
    /* Route before formatting, so a later fault/reset cannot warm-boot back
     * into the failed kernel. These are the already validated S-Boot words. */
    if (i9100_panic_recovery_enabled && i9100_trace_pmu) {
        *(volatile unsigned *)(i9100_trace_pmu+0x808) = 0x12345678U;
        *(volatile unsigned *)(i9100_trace_pmu+0x80c) = 0x12345671U;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    i9100_usbcdc_panic_begin(category,callsite,pc,lr,far,fsr,cpsr,
        i9100_panic_recovery_enabled ? 180U : 0U,i9100_recovery_begin_ticks);
}
unsigned ios7i9100_panic_seconds_remaining(void)
{
    if (!i9100_panic_recovery_enabled || !i9100_recovery_begin_ticks) return 0;
    uint64_t now = I9100_panic_ticks();
    uint64_t elapsed = now-i9100_recovery_begin_ticks;
    uint64_t limit = (uint64_t)I9100_MCT_HZ*180U;
    return elapsed >= limit ? 0U : 180U-(unsigned)(elapsed/I9100_MCT_HZ);
}
void __attribute__((noreturn)) ios7i9100_panic_recovery_wait(void)
{
    __asm__ volatile("cpsid if" ::: "memory");
    i9100_recovery_inside = 1;
    for (;;) {
        if (i9100_panic_recovery_enabled && i9100_trace_pmu &&
            (!i9100_recovery_begin_ticks || !ios7i9100_panic_seconds_remaining())) {
            if (!i9100_recovery_reset) {
                i9100_recovery_reset = 1;
                i9100_pmu_reset_registers(i9100_trace_pmu,1);
            }
            for (;;) __asm__ volatile("nop");
        }
        i9100_usbcdc_panic_poll();
        for (unsigned n=0;n<128;n++) __asm__ volatile("nop");
    }
}
void ios7i9100_panic_safe_putc(char c)
{
    ios7i9100_panic_text_putc(c);
    i9100_usbcdc_putc(c);
    ios7i9100_fast_putc(c);
}
/* Raw frames only. Never symbolic pmap lookup, process locks or Debugger. */
void __attribute__((noreturn)) ios7i9100_panic_finish(void *frame)
{
    unsigned fp = (unsigned)(uintptr_t)frame;
    i9100_puts("\nI9100 PANIC BACKTRACE (raw, no locks)\n");
    for (unsigned n=0;fp && n<24;n++) {
        unsigned words[2];
        if ((fp&3U) || !i9100_irq_snapshot_read((const void *)(uintptr_t)fp,
            words,sizeof(words))) break;
        i9100_puthex(fp); i9100_puts(" : "); i9100_puthex(words[1]); i9100_puts("\n");
        if (words[0]<=fp || words[0]-fp>65536U) break;
        fp=words[0];
    }
    i9100_puts(i9100_panic_recovery_enabled ?
        "I9100 PANIC_WAIT seconds=180 route=DOWNLOAD\n" :
        "I9100 PANIC_WAIT reset=disabled\n");
    ios7i9100_panic_recovery_wait();
}
void __attribute__((noreturn)) ios7i9100_recovery_test_panic(void)
{
    panic("I9100 controlled panic: verify retained log and 180-second Download route");
    ios7i9100_panic_recovery_wait();
}
static int i9100_os_reboot_download;
static int i9100_halt_restart(unsigned int action)
{
    /* Called by halt_all_cpus after the ordinary BSD shutdown/sync path.
     * The old unregistered PE callback left reboot(0) spinning forever. */
    if (action != kPERestartCPU || !i9100_trace_pmu) return -1;
    (void)ml_set_interrupts_enabled(FALSE);
    i9100_pmu_reset_registers(i9100_trace_pmu, i9100_os_reboot_download);
    for (;;) __asm__ volatile("wfi");
}
static void i9100_trace(unsigned int stage, unsigned int reason,
    unsigned int pc, unsigned int lr, unsigned int far, unsigned int fsr)
{
    if (i9100_trace_pmu) {
        *(volatile unsigned int *)(i9100_trace_pmu + 0x818) = 0x77660000U | stage;
        *(volatile unsigned int *)(i9100_trace_pmu + 0x81c) = reason;
    }
    if (i9100_trace_cell) {
        volatile unsigned int *cell = (volatile unsigned int *)i9100_trace_cell;
        cell[0] = 0;
        cell[1] = stage; cell[2] = reason; cell[3] = pc;
        cell[4] = lr; cell[5] = far; cell[6] = fsr;
        __asm__ volatile("dsb sy" ::: "memory");
        cell[0] = 0xcafebabeU;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

/* A fault in the panic/debugger path must not re-enter VM, printf or Debugger.
 * Keep the primary header and show the first secondary fault below it. Even
 * the emergency output may fault, so close the gate before touching it. */
void ios7i9100_abort_during_fatal(unsigned int pc,unsigned int lr,
    unsigned int far,unsigned int fsr,unsigned int psr,unsigned int type)
{
    static volatile unsigned int entered;
    if(!i9100_fatal_active)return;
    __asm__ volatile("cpsid if" ::: "memory");
    if(!entered){
        entered=1;
        if(i9100_recovery_inside) {
            i9100_usbcdc_panic_disable();
            i9100_pixels=0;
        }
        i9100_puts("\nSECONDARY ABORT; panic output stopped\nPC=");i9100_puthex(pc);
        i9100_puts(" LR=");i9100_puthex(lr);i9100_puts(" TYPE=");i9100_puthex(type);
        i9100_puts("\nFAR=");i9100_puthex(far);i9100_puts(" FSR=");i9100_puthex(fsr);
        i9100_puts(" PSR=");i9100_puthex(psr);i9100_puts("\n");
    }
    ios7i9100_panic_recovery_wait();
}

void __attribute__((noreturn)) ios7i9100_nested_panic_stop(void)
{
    static volatile unsigned int entered;
    __asm__ volatile("cpsid if" ::: "memory");
    if(!entered){
        entered=1;
        i9100_puts("\nSECONDARY PANIC; panic output stopped\n");
    }
    ios7i9100_panic_recovery_wait();
}

void ios7i9100_stage(unsigned int n)
{
    (void)n;
}

static unsigned int i9100_callsite(void *r){ return (((unsigned int)r)&~1U)-4U; }

int ios7i9100_fatal_report(unsigned int cat, unsigned int cs,
    unsigned int pc, unsigned int lr, unsigned int far, unsigned int fsr,
    unsigned int mask, const char *msg)
{
    if(i9100_fatal_active) return 0;
    i9100_fatal_active=1;
    unsigned fatal_mask;
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(fatal_mask) :: "memory");
    i9100_panic_prepare(cat,cs,pc,lr,far,fsr,pc ? mask : fatal_mask);
    i9100_trace(0x8000U | (i9100_stage & 0xffU), cat, pc ? pc : cs, lr, far, fsr);
    i9100_fatal_callsite=cs;
    /* Reclaim an existing framebuffer before panic_prologue can take locks.
     * No IO mapping, allocation, timebase or IOKit calls on this path. */
    i9100_panel_console_enabled=1;
    i9100f_cx=i9100f_cy=0;i9100f_wiped=1;
    if(i9100_pixels)
        for(unsigned int n=0;n<480U*800U;n++)i9100_pixels[n]=0xff000000U;
    /* Fatal formatting uses the direct panel/USB sinks below. The normal
     * console switch can acquire a lock held by the interrupted context. */
    i9100_fatal_category=cat;
    i9100_fatal_state[0]=cat; i9100_fatal_state[1]=i9100_stage;
    i9100_fatal_state[2]=mask&15U; i9100_fatal_state[3]=pc;
    i9100_fatal_state[4]=lr; i9100_fatal_state[5]=far; i9100_fatal_state[6]=fsr;

    i9100_puts("\n\n*** KERNEL FATAL: ");
    if (msg) i9100_puts(msg);
    i9100_puts(" ***\nCAT="); i9100_puthex(cat);
    i9100_puts(" CALL="); i9100_puthex(cs);
    i9100_puts(" STAGE="); i9100_puthex(i9100_stage);
    i9100_puts(" PC="); i9100_puthex(pc);
    i9100_puts(" LR="); i9100_puthex(lr);
    i9100_puts(" FAR="); i9100_puthex(far);
    i9100_puts(" FSR="); i9100_puthex(fsr);
    i9100_puts("\n");
    if (i9100_fimd_map) *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;

    return 1;
}

int ios7i9100_fatal_context(unsigned callsite, const void *context, const char *message)
{
    struct I9100PanicContext registers;
    /* ARM trap.c supplies this exact layout. Test the current translation
     * first; an invalid context still records the known callsite/message. */
    if (context && i9100_irq_snapshot_read(context, &registers, sizeof(registers)))
        return ios7i9100_fatal_report(0xf103U,callsite,registers.pc,registers.lr,
            registers.far,registers.fsr,registers.cpsr,message);
    return ios7i9100_fatal_report(0xf103U,callsite,0,0,0,0,0,message);
}

void ios7i9100_panic_fault_registers(const void *context)
{
    struct I9100PanicContext registers;
    static const char *const names[13] = {"r0=","r1=","r2=","r3=","r4=","r5=",
        "r6=","r7=","r8=","r9=","r10=","r11=","r12="};
    if (!context || !i9100_irq_snapshot_read(context,&registers,sizeof(registers))) {
        i9100_puts("\nFAULT REGISTERS unavailable\n");
        return;
    }
    i9100_puts("\nFAULT REGISTERS\n");
    for (unsigned n=0;n<13;n++) {
        i9100_puts(names[n]);i9100_puthex(registers.r[n]);
        i9100_puts((n&3U)==3U ? "\n" : " ");
    }
    i9100_puts("sp=");i9100_puthex(registers.sp);
    i9100_puts(" lr=");i9100_puthex(registers.lr);
    i9100_puts(" pc=");i9100_puthex(registers.pc);
    i9100_puts("\ncpsr=");i9100_puthex(registers.cpsr);
    i9100_puts(" fsr=");i9100_puthex(registers.fsr);
    i9100_puts(" far=");i9100_puthex(registers.far);i9100_puts("\n");
}

void __attribute__((noreturn)) ios7i9100_final_boundary(unsigned int reason)
{
    i9100_puts("\n*** FINAL BOUNDARY: reason=");
    i9100_puthex(reason);
    i9100_puts(" ***\n");
    if (i9100_fimd_map) *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;
    if(reason) ios7i9100_fatal_report(0xf000U|(reason&0xfffU),
        i9100_callsite(__builtin_return_address(0)),0,0,0,0,0,"ENTRY GUARD");
    __asm__ volatile("cpsid if" ::: "memory");
    ios7i9100_panic_recovery_wait();
}

void ios7i9100_forget_early_maps(void){ i9100_pixels=0; i9100_fb_map=0; }

/* UART S3C: UART2 @0x13820000 (Micro-USB), UART0 @0x13800000 (QEMU / console). */
#define I9100_UART2_PA 0x13820000U
#define I9100_UART0_PA 0x13800000U
static vm_offset_t i9100_uart2_map;
static vm_offset_t i9100_uart0_map;
static int i9100_uart_mode = 0; /* 1 = UART0 (QEMU), 2 = UART2 (Micro-USB) */

static void i9100_check_uart_mode(void)
{
    if (i9100_uart_mode == 0) {
        boot_args *args = (boot_args *)PE_state.bootArgs;
        if (args && args->machineType != 0) {
            i9100_uart_mode = 2; /* Real hardware: always UART2 (Micro-USB) */
            return;
        }
        if (args && args->CommandLine[0]) {
            const char *cmd = args->CommandLine;
            for (const char *p = cmd; *p; p++) {
                if (p[0] == 'i' && p[1] == '9' && p[2] == '1' && p[3] == '0' && p[4] == '0' &&
                    p[5] == 'u' && p[6] == 'a' && p[7] == 'r' && p[8] == 't' && p[9] == '=') {
                    if (p[10] == '2') i9100_uart_mode = 2;
                    else i9100_uart_mode = 1;
                    return;
                }
            }
        }
        i9100_uart_mode = 1; /* Default to UART0 for QEMU */
    }
}

static void i9100_uart_putc_phys(char c)
{
    i9100_check_uart_mode();
    if (i9100_uart_mode == 2) {
        volatile unsigned int *stat = (volatile unsigned int *)(I9100_UART2_PA + 0x10);
        if (!(*stat & 2)) return;
        volatile unsigned int *utxh2 = (volatile unsigned int *)(I9100_UART2_PA + 0x20);
        *utxh2 = (unsigned int)(unsigned char)c;
    } else {
        volatile unsigned int *utxh0 = (volatile unsigned int *)(I9100_UART0_PA + 0x20);
        *utxh0 = (unsigned int)(unsigned char)c;
    }
}

static void i9100_uart_putc_raw(char c)
{
    i9100_check_uart_mode();
    if (i9100_uart_mode == 2) {
        if (i9100_uart2_map >= 0x80000000U) {
            volatile unsigned int *stat = (volatile unsigned int *)(i9100_uart2_map + 0x10);
            if (!(*stat & 2)) return;
            volatile unsigned int *utxh2 = (volatile unsigned int *)(i9100_uart2_map + 0x20);
            *utxh2 = (unsigned int)(unsigned char)c;
        }
    } else {
        if (i9100_uart0_map >= 0x80000000U) {
            volatile unsigned int *utxh0 = (volatile unsigned int *)(i9100_uart0_map + 0x20);
            *utxh0 = (unsigned int)(unsigned char)c;
        }
    }
}

void ios7i9100_debug_putc(char c)
{
    if (i9100_fatal_active) { i9100_usbcdc_putc(c); return; }
    if(i9100_console_count < 1024) i9100_console_count++;
    if((i9100_uart2_map >= 0x80000000U) || (i9100_uart0_map >= 0x80000000U))
        i9100_uart_putc_raw(c);
    else
        i9100_uart_putc_phys(c);
    i9100_usbcdc_putc(c);
}

static void i9100_puts(const char *s)
{
    while(*s) {
        ios7i9100_debug_putc(*s);
        ios7i9100_fast_putc(*s);
        s++;
    }
}

static void i9100_puthex(unsigned int v)
{
    int i; char h[8];
    for(i=0;i<8;i++){ unsigned int n=(v>>((7-i)*4))&15U; h[i]=(char)(n<10?('0'+n):('a'+n-10)); }
    for(i=0;i<8;i++) {
        ios7i9100_debug_putc(h[i]);
        ios7i9100_fast_putc(h[i]);
    }
}

static int i9100_getc(void){ return -1; }
static void i9100_uart_init(void){ }
void ios7i9100_kernel_continuing(void){ }
void ios7i9100_timer_observe(unsigned int t,unsigned int c,unsigned int pc);
int ios7i9100_panic_text_begin(unsigned int reason,unsigned int known,unsigned int primary)
{
    if(!primary || !i9100_fatal_active || i9100_panic_text_mode)return 0;
    i9100_panic_text_mode=1;i9100_panic_reason=reason;
    i9100_panic_reason_known=known;return 1;
}
void ios7i9100_panic_text_putc(char c)
{
    if(i9100_panic_text_mode!=1)return;
    if(i9100_panic_text_len<sizeof(i9100_panic_text))
        i9100_panic_text[i9100_panic_text_len++]=c;
    else i9100_panic_text_truncated=1;
}
void ios7i9100_panic_text_end(void)
{
    if(i9100_panic_text_mode!=1)return;
    i9100_panic_text_mode=2;
    if(!i9100_pixels)return;
    /* Preserve the primary cause while the normal debugger dumps many pages.
     * The top 256 rows are reserved; only the remaining body may scroll. */
    for(unsigned int n=0;n<480U*256U;n++)i9100_pixels[n]=0xff000000U;
    i9100f_cx=i9100f_cy=0;
    i9100_puts("MALI33 CALL=");i9100_puthex(i9100_fatal_callsite);
    i9100_puts(" CAT=");i9100_puthex(i9100_fatal_category);
    i9100_puts("\nFORMATTED PANIC");
    if(i9100_panic_reason_known){
        i9100_puts(" REASON=");i9100_puthex(i9100_panic_reason);
    }
    i9100_puts("\n");
    unsigned int n;
    for(n=0;n<i9100_panic_text_len && i9100f_cy<224U;n++)
        ios7i9100_fast_putc(i9100_panic_text[n]);
    if(n<i9100_panic_text_len || i9100_panic_text_truncated){
        i9100f_cx=0;i9100f_cy=224U;
        i9100_puts("[message truncated; full output on serial]");
    }
    i9100_panic_header_frozen=1;
    i9100f_cx=0;i9100f_cy=256U;
    __asm__ volatile("dsb sy" ::: "memory");
    if(i9100_fimd_map)*(volatile unsigned int *)(i9100_fimd_map+0x1a4)=3;
}

/* Compact runtime overlay: GPU, BAT and LIVE IRQ only. i9100perf=0 hides it.
 * Keep RAM observations and the working trace/input path independently of
 * what is displayed. Fatal reporting still owns its separate console. */
extern unsigned char iso_font[];
extern volatile uint32_t i9100_perf_sd_kib, i9100_perf_sd_ms;
extern volatile uint32_t i9100_perf_sd_div, i9100_perf_sd_errors;
extern volatile uint32_t i9100_power_valid,i9100_power_percent,i9100_power_mv;
extern volatile uint32_t i9100_power_connected,i9100_power_charging,i9100_power_revision_value;
extern volatile int32_t i9100_power_temp,i9100_power_reason;
static int i9100_perf_strip = 1;
static uint64_t i9100_perf_last_ticks;
static volatile uint32_t i9100_irq_frame_tick,i9100_irq_frame_seen;
static uint32_t i9100_perf_last_irqs;
static volatile uint32_t i9100_live_irq_count, i9100_live_irq_pc;
struct I9100FramePerformance {
    uint32_t frames, copy_us, gap_ms, irq_delta, sctlr, ram_cache, fb_cache;
};
volatile struct I9100FramePerformance i9100_frame_performance;
volatile struct I9100ProcessExit i9100_process_exit;
volatile struct I9100ProcessExit i9100_backboard_exit;
volatile struct I9100SignalRequest i9100_signal_pending[3], i9100_signal_exit;

void ios7i9100_stderr_bytes(unsigned pid, unsigned task, const char *bytes, unsigned size)
{
    unsigned mask;
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory");
    i9100_stderr_append(pid, task, bytes, size);
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory");
}

void ios7i9100_abort_publish(const struct I9100AbortTrace *record)
{
    unsigned mask;
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory");
    i9100_abort_store(record);
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory");
}

void ios7i9100_signal_request(unsigned pid, unsigned task, unsigned signal,
    unsigned context_pid, const char *name, unsigned pc, unsigned lr,
    unsigned caller_pc, unsigned irq, unsigned flavor)
{
    unsigned mask;
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory");
    i9100_signal_publish(&i9100_signal_pending[signal == 9 ? 1 : signal == 6 ? 2 : 0], pid, task, signal, context_pid,
        name, pc, lr, caller_pc, irq, flavor);
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory");
}

void ios7i9100_process_exit(unsigned role, unsigned pid, unsigned task,
    unsigned signal, unsigned exit_code,
    unsigned known, unsigned exception, unsigned code, unsigned subcode,
    unsigned free_pages)
{
    unsigned mask;
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory");
    if (role == 1) i9100_signal_latch(pid, task, signal);
    i9100_process_publish(role == 1 ? &i9100_process_exit : &i9100_backboard_exit,
                         pid, signal, exit_code, known,
                         exception, code, subcode, free_pages);
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory");
}

static void i9100_perf_text(unsigned y, const char *s)
{
    for (unsigned x = 0; x < 60; x++) {
        unsigned char ch = *s ? (unsigned char)*s++ : ' ';
        for (unsigned row = 0; row < 16; row++) {
            unsigned char bits = iso_font[ch * 16 + row];
            for (unsigned bit = 0; bit < 8; bit++)
                i9100_pixels[(y + row) * 480 + x * 8 + bit] =
                    (bits & (1U << bit)) ? 0xffffffffU : 0xff000000U;
        }
    }
}

static void i9100_gpu_text(void)
{
 char line[64];
 snprintf(line,sizeof(line),"GPU S%u E%d R%u C%u P%u J%u X%u A%x M%x G%u U%u/%d",
  i9100_mali_observation.phase,i9100_mali_observation.error,
  i9100_mali_observation.restore,i9100_mali_observation.clock_mhz,
  i9100_mali_observation.pixels,
  i9100_mali_observation.trial,i9100_mali_observation.textures,
  i9100_mali_observation.render_mask,i9100_mali_observation.mmu_fault,
  i9100_mali_observation.guard,i9100_mali_user_completed,i9100_mali_user_error);
 i9100_perf_text(752,line);
 if(i9100_mali_observation.error){
  snprintf(line,sizeof(line),"GPU B%04x:%08x IRQ%x/%x T%u V%u PP%x",
   i9100_mali_observation.bad_offset,i9100_mali_observation.actual,
   i9100_mali_observation.gp_irq,i9100_mali_observation.pp_irq,
   i9100_mali_observation.triangles,i9100_mali_observation.vertices,
   i9100_mali_observation.pp_mask);
  i9100_perf_text(736,line);
 }
}

/* Use the same three rows after every present and from the IRQ observer.
 * Otherwise a new UI frame erases LIVE IRQ until the next timer update. */
static void i9100_status_text(void)
{
 char line[64];
 i9100_gpu_text();
 if(i9100_power_valid)
  snprintf(line,sizeof(line),"BAT %u%% %umV T%d/10C USB%u CHG%u R%d REV%u",
   i9100_power_percent,i9100_power_mv,i9100_power_temp,
   i9100_power_connected,i9100_power_charging,i9100_power_reason,i9100_power_revision_value);
 else snprintf(line,sizeof(line),"BAT unknown R%d REV%u (charging status unverified)",
   i9100_power_reason,i9100_power_revision_value);
 i9100_perf_text(768,line);
 unsigned count=i9100_live_irq_count,pc=i9100_live_irq_pc;
 char live[]="LIVE IRQ 00000000 PC 00000000";
 for(unsigned n=0;n<8;n++){
  live[9+n]="0123456789abcdef"[(count>>((7U-n)*4))&15U];
  live[21+n]="0123456789abcdef"[(pc>>((7U-n)*4))&15U];
 }
 i9100_perf_text(784,live);
}
extern volatile int i9100_panel_console_enabled;
/* IRQ progress display: no allocation, I2C, IOKit calls or locks. A UI
 * stall with LIVE IRQ still advancing differs from an IRQ-masked hard hang.
 * Saved PC is the interrupted context, not proof that this PC caused a stall. */
void ios7i9100_timer_observe(unsigned t,unsigned count,unsigned pc)
{
 (void)t;
 if(!i9100_perf_strip || count%100U)return;
 i9100_live_irq_pc=pc;i9100_live_irq_count=count;
 if(!i9100_irq_frame_seen ||
    i9100_panel_console_enabled || !i9100_pixels || i9100_fatal_active)return;
 i9100_status_text();
 __asm__ volatile("dsb sy" ::: "memory");
 if(i9100_fimd_map)*(volatile unsigned int *)(i9100_fimd_map+0x1a4)=3;
}

/* Direct-FB backend: 480x800x32 BGRA CPU copy. */
static uint64_t i9100_graphics_seq = 1;
static void i9100_frame_copy(void *destination,const void *source,uint32_t bytes)
{
    bcopy(source,destination,bytes);
}

int ios7i9100_display_ready(void)
{
    return i9100_pixels && !i9100_fatal_active &&
        PE_state.video.v_width==480 && PE_state.video.v_height==800 &&
        PE_state.video.v_rowBytes==1920 && PE_state.video.v_depth==32;
}

int ios7i9100_display_present(const uint8_t *px, uint32_t bytes, uint32_t row,
                             uint32_t fmt, uint64_t *seq)
{
    if(!px||!seq) return 0;
    if(!(fmt==0x42475241U||fmt==0x52474241U)) return 0;
    if(!ios7i9100_display_ready()) return 0;
    uint64_t began = I9100_get_timebase();
    /* Use the ARM burst-copy implementation instead of 1.5M byte iterations
     * in the unoptimized kernel. Validate the full pitched source footprint. */
    if(!i9100_copy_frame((void *)(uintptr_t)i9100_pixels,px,bytes,row,i9100_frame_copy)) return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    uint64_t ended = I9100_get_timebase();
    struct I9100TimerSnapshot timer;
    I9100_timer_snapshot(&timer);
    i9100_frame_performance.frames++;
    i9100_frame_performance.copy_us = (uint32_t)((ended - began) / 24ULL);
    i9100_frame_performance.gap_ms = i9100_perf_last_ticks ?
        (uint32_t)((began - i9100_perf_last_ticks) / 24000ULL) : 0;
    i9100_frame_performance.irq_delta = i9100_perf_last_ticks ?
        timer.irq_count - i9100_perf_last_irqs : 0;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(i9100_frame_performance.sctlr));
    vm_offset_t ram_pte = pmap_pte(kernel_pmap, (vm_offset_t)px);
    vm_offset_t fb_pte = pmap_pte(kernel_pmap, (vm_offset_t)i9100_pixels);
    i9100_frame_performance.ram_cache = ram_pte ? *(uint32_t *)ram_pte & 0x1ccU : 0;
    i9100_frame_performance.fb_cache = fb_pte ? *(uint32_t *)fb_pte & 0x1ccU : 0;
    i9100_perf_last_ticks = began;
    i9100_irq_frame_tick=(uint32_t)began;i9100_irq_frame_seen=1;
    i9100_perf_last_irqs = timer.irq_count;
    if (i9100_perf_strip) i9100_status_text();
    __asm__ volatile("dsb sy" ::: "memory");
    *seq=i9100_graphics_seq++;
    if(!i9100_graphics_seq) i9100_graphics_seq=1;
    i9100_panel_console_enabled=0;
    if (i9100_fimd_map) {
        *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;
    }
    return 1;
}

int ios7i9100_display_completed(uint64_t s){ return s && s<i9100_graphics_seq; }

static int i9100_video_ok(const boot_args *a)
{
    unsigned long long ramend = (unsigned long long)a->physBase + a->memSize;
    return a->Revision==1 && a->Version==2 && a->virtBase==0x80000000U &&
        a->physBase==0x40000000U && a->memSize>=0x20000000U &&
        a->topOfKernelData>=a->physBase && !(a->topOfKernelData&0x3fffU) &&
        (unsigned long long)a->topOfKernelData+0x220000U<=ramend &&
        a->Video.v_width==480 && a->Video.v_height==800 &&
        a->Video.v_rowBytes==1920 && a->Video.v_depth==32 &&
        !(a->Video.v_baseAddr&3U) &&
        a->Video.v_baseAddr >= 0x40000000U && a->Video.v_baseAddr < 0x80000000U;
}

void ios7i9100_checkpoint(boot_args *a, unsigned int stage)
{
    if(i9100_fatal_active) return;
    if(stage==6){
        if(!i9100_video_ok(a)) ios7i9100_final_boundary(1);
        i9100_fb_physical=a->Video.v_baseAddr;
        i9100_pixels=(volatile unsigned int *)i9100_fb_physical;
        gPESocDispatch.uart_putc=ios7i9100_debug_putc;
        gPESocDispatch.uart_getc=i9100_getc;
        gPESocDispatch.uart_init=i9100_uart_init;
        i9100_puts("i9100 stage6 video ok\n");
    }
    if(stage!=i9100_stage+1U||stage<6||stage>10){
        if(!(stage==6&&i9100_stage==0)) ios7i9100_final_boundary(3);
    }
    i9100_stage=stage;
    i9100_trace(0x100U | stage, 0, 0, 0, 0, 0);
    if(stage==7) i9100_puts("i9100 stage7\n");
    else if(stage==8) i9100_puts("i9100 stage8\n");
    else if(stage==9) i9100_puts("i9100 stage9\n");
    else if(stage==10) i9100_puts("i9100 stage10\n");
}

void PE_init_SocSupport_stub(void)
{
    DTEntry chosen;
    ios7lab_i9100_handoff *h=0;
    unsigned int size=0;
    boot_args *args=(boot_args *)PE_state.bootArgs;
    vm_offset_t fb;
    static ios7lab_i9100_handoff i9100_default_handoff;
    i9100_trace_pmu=ml_io_map(0x10020000, 0x4000);
    i9100_trace_cell=ml_io_map(0x7ffe0000, 0x1000);
    int panic_recovery = 0;
    i9100_panic_recovery_enabled =
        PE_parse_boot_argn("i9100panicrecovery", &panic_recovery,
            sizeof(panic_recovery)) && panic_recovery == 1;
    int reboot_download = 0;
    i9100_os_reboot_download =
        PE_parse_boot_argn("i9100rebootdownload", &reboot_download,
            sizeof(reboot_download)) && reboot_download == 1;
    PE_halt_restart = i9100_halt_restart;
    if (DTLookupEntry(0,"/chosen",&chosen)==kSuccess) {
        struct I9100PreviousBoot *previous=0;
        unsigned previous_bytes=0;
        if (DTGetProperty(chosen,I9100_PREVIOUS_BOOT_PROPERTY,
                          (void **)&previous,&previous_bytes)==kSuccess &&
            previous && previous_bytes==sizeof(*previous) &&
            previous->magic==I9100_PREVIOUS_BOOT_MAGIC && previous->version==1 &&
            previous->bytes==sizeof(*previous) && !(previous->valid&~3U))
            memcpy(&i9100_previous_boot,previous,sizeof(*previous));
    }
    i9100_trace(0x108, 0, 0, 0, 0, 0);
    if(!i9100_video_ok(args)) ios7i9100_final_boundary(4);

    if(DTLookupEntry(0,"/chosen",&chosen)==kSuccess &&
       DTGetProperty(chosen,IOS7LAB_I9100_DT_PROPERTY,(void **)&h,&size)==kSuccess &&
       size==128&&h&&h->magic==IOS7LAB_I9100_MAGIC&&h->version==1&&
       h->bytes==128&&h->machine==0x4210U){
        /* loader-provided handoff accepted */
    }else{
        i9100_default_handoff.magic=IOS7LAB_I9100_MAGIC; i9100_default_handoff.version=1;
        i9100_default_handoff.bytes=128; i9100_default_handoff.flags=IOS7LAB_I9100_FLAGS;
        i9100_default_handoff.machine=0x4210U;
        i9100_default_handoff.disp_physical=args->Video.v_baseAddr;
        i9100_default_handoff.dma_address=args->Video.v_baseAddr;
        i9100_default_handoff.dma_stride=1920; i9100_default_handoff.dma_xy=0;
        i9100_default_handoff.width=480; i9100_default_handoff.height=800;
        i9100_default_handoff.row_bytes=1920; i9100_default_handoff.pixel_format=0x42475241U;
        i9100_default_handoff.framebuffer_bytes=1536000;
        i9100_default_handoff.memory_base=args->physBase;
        i9100_default_handoff.memory_bytes=args->memSize;
        i9100_default_handoff.top_of_kernel_data=args->topOfKernelData;
        i9100_default_handoff.checkpoint_count=5;
        h=&i9100_default_handoff;
    }
    (void)h;
    (void)PE_parse_boot_argn("i9100perf", &i9100_perf_strip, sizeof(i9100_perf_strip));

    fb=io_map_spec((vm_map_offset_t)i9100_fb_physical, 1536000, VM_WIMG_WTHRU);
    if(!fb) fb=ml_io_map(i9100_fb_physical, 1536000);
    i9100_uart2_map=ml_io_map(I9100_UART2_PA, 4096);
    i9100_uart0_map=ml_io_map(I9100_UART0_PA, 4096);
    i9100_usbcdc_setmap();
    if(!fb || (!i9100_uart2_map && !i9100_uart0_map)) ios7i9100_final_boundary(5);

    i9100_pixels=(volatile unsigned int *)fb;
    i9100_fb_map=fb;
    PE_state.video.v_baseAddr=fb;
    PE_state.video.v_rowBytes=1920; PE_state.video.v_width=480;
    PE_state.video.v_height=800; PE_state.video.v_depth=32;

    gPESocDispatch.uart_putc=ios7i9100_debug_putc;
    gPESocDispatch.uart_getc=i9100_getc;
    gPESocDispatch.uart_init=i9100_uart_init;
    gPESocDispatch.interrupt_init=I9100_interrupt_init;
    gPESocDispatch.timebase_init=I9100_timebase_init;
    gPESocDispatch.handle_interrupt=I9100_handle_interrupt;
    gPESocDispatch.get_timebase=I9100_get_timebase;
    gPESocDispatch.timer_value=I9100_timer_value;
    gPESocDispatch.timer_enabled=I9100_timer_enabled;

    {
        PE_Video cv=PE_state.video;
        cv.v_baseAddr=i9100_fb_physical|1U; cv.v_baseAddrHigh=0;
        cv.v_offset=0; cv.v_length=800U*1920U;
        cv.v_width=480; cv.v_height=800; cv.v_rowBytes=1920; cv.v_depth=32;
        cv.v_scale=kPEScaleFactor1x;
        initialize_screen(&cv,kPETextMode);
    }
    ios7i9100_checkpoint(args, 9);
    __asm__ volatile("" ::: "memory");

    /* Map FIMD controller and configure Window 0 */
    i9100_fimd_map = ml_io_map(0x11C00000, 0x10000);
    vm_offset_t sysreg = ml_io_map(0x10010000, 4096);
    if (sysreg) {
        *(volatile unsigned int *)(sysreg + 0x210) |= (1U << 1); /* LCD bypass */
    }

    if (i9100_fimd_map) {
        /* Read live buffers of all windows */
        unsigned int bufs[5];
        bufs[0] = *(volatile unsigned int *)(i9100_fimd_map + 0xA0);
        bufs[1] = *(volatile unsigned int *)(i9100_fimd_map + 0xA8);
        bufs[2] = *(volatile unsigned int *)(i9100_fimd_map + 0xB0);
        bufs[3] = *(volatile unsigned int *)(i9100_fimd_map + 0xB8);
        bufs[4] = *(volatile unsigned int *)(i9100_fimd_map + 0xC0);

        /* Clear any live buffer S-Boot used to black */
        int w;
        for (w = 0; w < 5; w++) {
            if (bufs[w] >= 0x40000000U && bufs[w] < 0x80000000U && bufs[w] != i9100_fb_physical) {
                vm_offset_t mb = ml_io_map(bufs[w], 1536000);
                if (mb) {
                    volatile unsigned int *p = (volatile unsigned int *)mb;
                    unsigned int i;
                    for (i = 0; i < 480 * 800; i++) p[i] = 0xFF000000U;
                }
            }
        }

        /* Disable overlay windows 1, 2, 3, 4 */
        *(volatile unsigned int *)(i9100_fimd_map + 0x24) = 0;
        *(volatile unsigned int *)(i9100_fimd_map + 0x28) = 0;
        *(volatile unsigned int *)(i9100_fimd_map + 0x2C) = 0;
        *(volatile unsigned int *)(i9100_fimd_map + 0x30) = 0;

        /* Clear PRTCON protect */
        *(volatile unsigned int *)(i9100_fimd_map + 0x0C) = 0;

        /* Protect shadow registers during configuration */
        *(volatile unsigned int *)(i9100_fimd_map + 0x34) = (1U << 10) | 1U;

        /* Repoint Window 0 to our framebuffer */
        *(volatile unsigned int *)(i9100_fimd_map + 0xA0) = i9100_fb_physical;
        *(volatile unsigned int *)(i9100_fimd_map + 0xD0) = i9100_fb_physical + 480 * 800 * 4;
        *(volatile unsigned int *)(i9100_fimd_map + 0x100) = (480 * 4) | (0 << 13);
        *(volatile unsigned int *)(i9100_fimd_map + 0x40) = 0;
        *(volatile unsigned int *)(i9100_fimd_map + 0x44) = (479U << 11) | 799U;
        *(volatile unsigned int *)(i9100_fimd_map + 0x48) = 480 * 800;

        /* WINCON0: 24bpp 888, word swap, window enable */
        unsigned int orig_w0 = *(volatile unsigned int *)(i9100_fimd_map + 0x20);
        unsigned int new_w0 = (orig_w0 & ~(0xFU << 2)) | (11U << 2) | (1U << 15) | 1U;
        *(volatile unsigned int *)(i9100_fimd_map + 0x20) = new_w0;

        /* Unprotect shadow: allow update at vsync */
        *(volatile unsigned int *)(i9100_fimd_map + 0x34) = 1U;

        /* Enable video output in VIDCON0 */
        *(volatile unsigned int *)(i9100_fimd_map + 0x00) |= 3U;

        /* Forward RGB order */
        *(volatile unsigned int *)(i9100_fimd_map + 0x2020) = 0;

        /* Trigger I80 command */
        *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;
    }

    /* Clear framebuffer to black */
    {
        unsigned int i;
        for (i = 0; i < 480 * 800; i++) i9100_pixels[i] = 0xFF000000U;
    }
    i9100f_wiped = 1;
    i9100f_cx = 0;
    i9100f_cy = 0;

    if (i9100_fimd_map) {
        *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;
    }

    i9100_puts("\n========================================\n");
    i9100_puts("  Darwin/XNU iOS 7 for Samsung Galaxy S2\n");
    i9100_puts("  Exynos 4210 Dual Cortex-A9 (GT-I9100)\n");
    i9100_puts("  AMOLED Display Console 480x800 OK!\n");
    i9100_puts("========================================\n\n");
}

/* Fast panel console */
#define I9100F_W 8U
#define I9100F_H 16U
#define I9100F_COLS 60U
#define I9100F_ROWS 50U
#define I9100F_FB_W 480U
#define I9100F_FB_H 800U
extern unsigned char iso_font[];
extern boolean_t pmap_initialized;

static void i9100f_blank_line(volatile unsigned int *fb, unsigned int y)
{
    unsigned int x, yy;
    for (yy = 0; yy < 16; yy++) {
        volatile unsigned int *row = fb + (y + yy) * 480U;
        for (x = 0; x < 480; x += 8) {
            row[x + 0] = 0xFF000000U;
            row[x + 1] = 0xFF000000U;
            row[x + 2] = 0xFF000000U;
            row[x + 3] = 0xFF000000U;
            row[x + 4] = 0xFF000000U;
            row[x + 5] = 0xFF000000U;
            row[x + 6] = 0xFF000000U;
            row[x + 7] = 0xFF000000U;
        }
    }
}

static void i9100f_scroll_up(volatile unsigned int *fb)
{
    /* Scroll up by 4 lines (64 pixels = 4 * 16) at once with burst bcopy */
    const unsigned int scroll_lines = 64U;
    const unsigned int first_row = i9100_panic_header_frozen ? 256U : 0U;
    const unsigned int copy_words = 480U * (800U - scroll_lines - first_row);

    bcopy((const void *)(uintptr_t)(fb + (480U * (first_row + scroll_lines))),
          (void *)(uintptr_t)(fb + 480U * first_row),
          copy_words * sizeof(unsigned int));

    /* Blank the bottom 64 lines */
    volatile unsigned int *bottom = fb + 480U * (800U - scroll_lines);
    unsigned int blank_words = 480U * scroll_lines;
    for (unsigned int i = 0; i < blank_words; i++) {
        bottom[i] = 0xFF000000U;
    }
}

static void i9100f_newline(volatile unsigned int *fb)
{
    i9100f_cx = 0;
    if (i9100f_cy + 16U >= 800U) {
        i9100f_scroll_up(fb);
        i9100f_cy = 800U - 64U;
    } else {
        i9100f_cy += 16U;
        i9100f_blank_line(fb, i9100f_cy);
    }
    if (i9100_fimd_map) *(volatile unsigned int *)(i9100_fimd_map + 0x1A4) = 3;
}

void ios7i9100_fast_putc(char c)
{
    volatile unsigned int *fb = i9100_pixels;
    unsigned int x, y;
    unsigned char uc = (unsigned char)c;
    if (!i9100_panel_console_enabled) return;

    if (!i9100f_remapped && pmap_initialized && !i9100_fatal_active) {
        vm_offset_t nfb = io_map_spec((vm_map_offset_t)i9100_fb_physical, 1536000, VM_WIMG_WTHRU);
        if (!nfb) nfb = ml_io_map(i9100_fb_physical, 1536000);
        if (nfb) { i9100_pixels = (volatile unsigned int *)nfb; fb = i9100_pixels; }
        vm_offset_t nfimd = ml_io_map(0x11C00000, 0x10000);
        if (nfimd) { i9100_fimd_map = nfimd; }
        i9100f_remapped = 1;
    }
    if (!fb) return;
    if (!i9100f_wiped) {
        unsigned int i, n = 480U * 800U;
        for (i = 0; i < n; i++) fb[i] = 0xFF000000U;
        i9100f_wiped = 1;
        i9100f_cx = 0; i9100f_cy = 0;
    }
    if(i9100_panic_header_frozen && i9100f_cy<256U){
        i9100f_cx=0;i9100f_cy=256U;
    }
    if (c == '\n') {
        i9100f_newline(fb);
    }
    else if (c == '\r') { i9100f_cx = 0; }
    else if (c == '\t') {
        i9100f_cx = (i9100f_cx + 32) & ~31U;
        if (i9100f_cx + 8 > 480) i9100f_newline(fb);
    }
    else if (uc >= 32 && uc < 127) {
        unsigned char *g = &iso_font[uc * 16];
        for (y = 0; y < 16; y++) {
            unsigned char bits = g[y];
            volatile unsigned int *row =
                fb + (i9100f_cy + y) * 480U + i9100f_cx;
            for (x = 0; x < 8; x++)
                row[x] = (bits & (1U << x)) ? 0xFFFFFFFFU : 0xFF000000U;
        }
        i9100f_cx += 8;
        if (i9100f_cx + 8 > 480) {
            i9100f_newline(fb);
        }
    }
}
#endif
