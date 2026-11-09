/* i9100 Exynos4210 single-core XNU IRQ/timebase: the SoC MCT global timer
 * (global free-running comparator 0) as the tick source, delivered through
 * the GIC on INTID 89 (SPI 57 + 32).
 * The MCT count is 24 MHz on Exynos 4210 / QEMU smdkc210 (fin_pll). */
#include <mach/mach_types.h>
#include <mach/arm/thread_status.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>
#include "i9100_timer.h"

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#define I9100_MCT_PA 0x10050000U
#define I9100_GIC_DIST_PA 0x10490000U
#define I9100_GIC_CPU_PA 0x10480000U
#define I9100_IO_BYTES 0x1000U

/* MCT global block registers. */
#define I9100_MCT_G_CTL 0x010U
#define I9100_MCT_G_CNT_L 0x100U
#define I9100_MCT_G_CNT_U 0x104U
#define I9100_MCT_G_CNT_WSTAT 0x110U
#define I9100_MCT_G_COMP0_L 0x200U
#define I9100_MCT_G_COMP0_U 0x204U
#define I9100_MCT_G_COMP0_ADD_INCR 0x208U
#define I9100_MCT_G_TCON 0x240U
#define I9100_MCT_G_INT_CSTAT 0x244U
#define I9100_MCT_G_INT_ENB 0x248U
#define I9100_MCT_G_WSTAT 0x24CU

#define I9100_MCT_CTL_ENABLE 0x1U
#define I9100_MCT_TCON_COMP0_ENABLE (1U << 0)
#define I9100_MCT_TCON_COMP0_AUTO_INC (1U << 1)
#define I9100_MCT_TCON_START (1U << 8)
#define I9100_MCT_WSTAT_COMP0_L (1U << 0)
#define I9100_MCT_WSTAT_COMP0_U (1U << 1)
#define I9100_MCT_WSTAT_COMP0_INCR (1U << 2)
#define I9100_MCT_WSTAT_TCON (1U << 16)

/* GIC distributor / CPU interface registers. */
#define I9100_GIC_DIST_CTRL 0x000U
#define I9100_GIC_DIST_ENABLE_SET 0x100U
#define I9100_GIC_DIST_ENABLE_CLEAR 0x180U
#define I9100_GIC_DIST_PENDING_CLEAR 0x280U
#define I9100_GIC_DIST_PRI 0x400U
#define I9100_GIC_DIST_TARGET 0x800U
#define I9100_GIC_DIST_CONFIG 0xc00U
#define I9100_GIC_CPU_CTRL 0x00U
#define I9100_GIC_CPU_PRIMASK 0x04U
#define I9100_GIC_CPU_INTACK 0x0cU
#define I9100_GIC_CPU_EOI 0x10U

#define I9100_TIMER_IRQ I9100_MCT_IRQ

extern void rtc_configure(uint64_t hz);
extern void rtclock_intr(arm_saved_state_t *regs);
extern void ios7i9100_timer_observe(uint32_t raw_ticks, uint32_t actual_irq_count, uint32_t pc);
extern void ios7i9100_debug_putc(char c);
extern void i9100_usbcdc_poll(void);
extern void i9100_usbcdc_control_init(void);

uint64_t clock_decrementer;
static vm_offset_t i9100_mct, i9100_gic_dist, i9100_gic_cpu;
static uint32_t controller_ready, timebase_ready, counter_observed, armed;
static uint32_t last_count_lo, last_match_lo, irq_count, last_irq_counter, last_vector;
static uint32_t spurious_irq_count;
static uint32_t timer_hz = I9100_MCT_HZ;

static uint32_t read_reg(vm_offset_t base, uint32_t off)
{ return *(volatile uint32_t *)(base+off); }
static void write_reg(vm_offset_t base, uint32_t off, uint32_t v)
{ *(volatile uint32_t *)(base+off)=v; }
static void drain(void) { __asm__ volatile("dsb sy" ::: "memory"); }

/* MCT register write with the WSTAT handshake. */
static void mct_write(uint32_t value, uint32_t offset, uint32_t wstat_mask)
{
    uint32_t i;
    write_reg(i9100_mct, offset, value);
    for (i = 0; i < 0x10000U; i++) {
        if (read_reg(i9100_mct, I9100_MCT_G_WSTAT) & wstat_mask) {
            write_reg(i9100_mct, I9100_MCT_G_WSTAT, wstat_mask);
            return;
        }
    }
    panic("i9100 MCT write stuck: value=0x%x offset=0x%x mask=0x%x", value, offset, wstat_mask);
}

static uint64_t mct_get_frc(void)
{
    uint32_t lo, hi, hi2;
    do {
        hi = read_reg(i9100_mct, I9100_MCT_G_CNT_U);
        lo = read_reg(i9100_mct, I9100_MCT_G_CNT_L);
        hi2 = read_reg(i9100_mct, I9100_MCT_G_CNT_U);
    } while (hi != hi2);
    return (((uint64_t)hi) << 32) | lo;
}

static void mct_frc_start(void)
{
    mct_write(I9100_MCT_TCON_START, I9100_MCT_G_TCON, I9100_MCT_WSTAT_TCON);
}

static void i9100_put_hex(uint32_t v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        uint32_t n = (v >> i) & 0xf;
        ios7i9100_debug_putc((char)(n < 10 ? ('0'+n) : ('a'+n-10)));
    }
}

void I9100_interrupt_init(void)
{
    uint32_t i, irq;
    boolean_t was_enabled;
    if(controller_ready) return;
    was_enabled=ml_set_interrupts_enabled(FALSE);
    i9100_gic_dist=ml_io_map(I9100_GIC_DIST_PA,I9100_IO_BYTES);
    i9100_gic_cpu=ml_io_map(I9100_GIC_CPU_PA,I9100_IO_BYTES);
    if(!i9100_gic_dist||!i9100_gic_cpu) panic("i9100 GIC IO mapping failed");

    write_reg(i9100_gic_dist, I9100_GIC_DIST_CTRL, 0);
    for(i=32; i<128; i+=32) {
        write_reg(i9100_gic_dist, I9100_GIC_DIST_ENABLE_CLEAR + (i/32)*4, 0xffffffffU);
        write_reg(i9100_gic_dist, I9100_GIC_DIST_PENDING_CLEAR + (i/32)*4, 0xffffffffU);
    }
    for(i=32; i<128; i+=4) write_reg(i9100_gic_dist, I9100_GIC_DIST_PRI + i, 0xa0a0a0a0U);
    for(i=32; i<128; i+=4) write_reg(i9100_gic_dist, I9100_GIC_DIST_TARGET + i, 0x01010101U);
    for(i=32; i<128; i+=16) write_reg(i9100_gic_dist, I9100_GIC_DIST_CONFIG + (i/16)*4, 0);

    irq=I9100_TIMER_IRQ;
    write_reg(i9100_gic_dist, I9100_GIC_DIST_TARGET + (irq/4)*4,
              (read_reg(i9100_gic_dist, I9100_GIC_DIST_TARGET + (irq/4)*4) & ~(0xffU << ((irq%4)*8)))
              | (1U << ((irq%4)*8)));
    write_reg(i9100_gic_dist, I9100_GIC_DIST_PRI + (irq/4)*4,
              (read_reg(i9100_gic_dist, I9100_GIC_DIST_PRI + (irq/4)*4) & ~(0xffU << ((irq%4)*8)))
              | (0x80U << ((irq%4)*8)));
    write_reg(i9100_gic_dist, I9100_GIC_DIST_CONFIG + (irq/16)*4,
              read_reg(i9100_gic_dist, I9100_GIC_DIST_CONFIG + (irq/16)*4) | (2U << ((irq%16)*2)));

    write_reg(i9100_gic_cpu, I9100_GIC_CPU_PRIMASK, 0xf0U);
    write_reg(i9100_gic_dist, I9100_GIC_DIST_CTRL, 1U);
    write_reg(i9100_gic_cpu, I9100_GIC_CPU_CTRL, 1U);
    drain();
    controller_ready=1;
    {
        unsigned int v[3];
        v[0]=read_reg(i9100_gic_dist, I9100_GIC_DIST_CTRL);
        v[1]=read_reg(i9100_gic_cpu, I9100_GIC_CPU_CTRL);
        v[2]=read_reg(i9100_gic_dist, I9100_GIC_DIST_TARGET + (irq/4)*4);
        ios7i9100_debug_putc('G'); ios7i9100_debug_putc('I'); ios7i9100_debug_putc('C');
        ios7i9100_debug_putc(' ');
        i9100_put_hex(v[0]); ios7i9100_debug_putc(' ');
        i9100_put_hex(v[1]); ios7i9100_debug_putc(' ');
        i9100_put_hex(v[2]);
        ios7i9100_debug_putc('\n');
    }
    ml_set_interrupts_enabled(was_enabled);
}

void I9100_timebase_init(void)
{
    uint64_t before, after;
    uint32_t i;
    boolean_t was_enabled=ml_set_interrupts_enabled(FALSE);
    if(!controller_ready) panic("i9100 timebase before GIC initialization");
    if(timebase_ready) { ml_set_interrupts_enabled(was_enabled); return; }
    i9100_mct=ml_io_map(I9100_MCT_PA,I9100_IO_BYTES);
    if(!i9100_mct) panic("i9100 MCT IO mapping failed");

    write_reg(i9100_mct, I9100_MCT_G_CTL, I9100_MCT_CTL_ENABLE);
    mct_frc_start();
    write_reg(i9100_mct, I9100_MCT_G_INT_ENB, 0);
    drain();

    before=mct_get_frc(); after=before;
    for(i=0;i<65536U;i++){ after=mct_get_frc(); if(after!=before) break; }
    if(after==before) panic("i9100 MCT counter did not advance");
    counter_observed=1; last_count_lo=(uint32_t)after;
    timebase_ready=1;
    clock_decrementer=I9100_MCT_HZ/100U; /* 10ms scheduler tick. */
    if(clock_decrementer==0) clock_decrementer=I9100_MCT_HZ/100U;
    if(clock_decrementer<I9100_MCT_MIN_MATCH_TICKS) clock_decrementer=I9100_MCT_MIN_MATCH_TICKS;
    gPEClockFrequencyInfo.timebase_frequency_hz=timer_hz;
    gPEClockFrequencyInfo.dec_clock_rate_hz=timer_hz;
    rtc_configure(timer_hz);
    {
        unsigned int v[4];
        v[0]=read_reg(i9100_mct, I9100_MCT_G_CTL);
        v[1]=read_reg(i9100_mct, I9100_MCT_G_TCON);
        v[2]=read_reg(i9100_mct, I9100_MCT_G_INT_ENB);
        v[3]=read_reg(i9100_mct, I9100_MCT_G_INT_CSTAT);
        ios7i9100_debug_putc('M'); ios7i9100_debug_putc('C'); ios7i9100_debug_putc('T');
        ios7i9100_debug_putc(' ');
        for(i=0;i<4;i++){ i9100_put_hex(v[i]); ios7i9100_debug_putc(i==3?'\n':' '); }
    }
    I9100_timer_enabled(TRUE);
    /* The panel and exception paths are established before maintenance. */
    i9100_usbcdc_control_init();
    ml_set_interrupts_enabled(was_enabled);
}

void I9100_timer_enabled(int enable)
{
    uint32_t ticks;
    uint64_t requested, frc, match;
    boolean_t was_enabled=ml_set_interrupts_enabled(FALSE);
    uint32_t irq=I9100_TIMER_IRQ;
    if(!timebase_ready) panic("i9100 timer before timebase initialization");
    requested=clock_decrementer;
    if(!enable || requested==~0ULL){
        write_reg(i9100_mct, I9100_MCT_G_INT_ENB, 0);
        write_reg(i9100_gic_dist, I9100_GIC_DIST_ENABLE_CLEAR + (irq/32)*4, 1U<<(irq%32));
        armed=0;
        ml_set_interrupts_enabled(was_enabled); return;
    }
    write_reg(i9100_mct, I9100_MCT_G_INT_ENB, 0);
    write_reg(i9100_mct, I9100_MCT_G_INT_CSTAT, 1U);
    write_reg(i9100_gic_dist, I9100_GIC_DIST_PENDING_CLEAR + (irq/32)*4, 1U<<(irq%32));
    drain();
    ticks=requested>0x7fffffffU?0x7fffffffU:(uint32_t)requested;
    if(ticks<I9100_MCT_MIN_MATCH_TICKS) ticks=I9100_MCT_MIN_MATCH_TICKS;
    frc=mct_get_frc();
    match=frc+ticks;
    mct_write((uint32_t)(match & 0xffffffffU), I9100_MCT_G_COMP0_L, I9100_MCT_WSTAT_COMP0_L);
    mct_write((uint32_t)(match >> 32), I9100_MCT_G_COMP0_U, I9100_MCT_WSTAT_COMP0_U);
    mct_write(ticks, I9100_MCT_G_COMP0_ADD_INCR, I9100_MCT_WSTAT_COMP0_INCR);
    mct_write(I9100_MCT_TCON_COMP0_ENABLE | I9100_MCT_TCON_COMP0_AUTO_INC | I9100_MCT_TCON_START,
        I9100_MCT_G_TCON, I9100_MCT_WSTAT_TCON);
    write_reg(i9100_mct, I9100_MCT_G_INT_ENB, 1);
    write_reg(i9100_gic_dist, I9100_GIC_DIST_CTRL, 1U);
    write_reg(i9100_gic_cpu, I9100_GIC_CPU_CTRL, 1U);
    write_reg(i9100_gic_dist, I9100_GIC_DIST_ENABLE_SET + (irq/32)*4, 1U<<(irq%32));
    drain();
    armed=1;
    last_match_lo=(uint32_t)(match & 0xffffffffU);
    ml_set_interrupts_enabled(was_enabled);
}

uint64_t I9100_get_timebase(void)
{
    if(!timebase_ready) return 0;
    return mct_get_frc();
}
int I9100_timebase_initialized(void)
{
    return timebase_ready != 0;
}
/* Fatal context must not call clock/VM/scheduler routines. These are existing
 * MMIO mappings and the free-running counter keeps ticking with IRQs masked. */
uint64_t I9100_panic_ticks(void)
{
    if (!timebase_ready || !i9100_mct) return 0;
    uint32_t hi = 0, lo = 0, after = 0;
    for (unsigned n = 0; n < 4; n++) {
        hi = read_reg(i9100_mct, I9100_MCT_G_CNT_U);
        lo = read_reg(i9100_mct, I9100_MCT_G_CNT_L);
        after = read_reg(i9100_mct, I9100_MCT_G_CNT_U);
        if (hi == after) return ((uint64_t)hi << 32) | lo;
    }
    return ((uint64_t)after << 32) | read_reg(i9100_mct, I9100_MCT_G_CNT_L);
}

uint64_t I9100_timer_value(void)
{
    boolean_t w=ml_set_interrupts_enabled(FALSE);
    int64_t rem=0;
    if(timebase_ready && armed) {
        uint64_t now=mct_get_frc();
        uint32_t lo=read_reg(i9100_mct, I9100_MCT_G_COMP0_L);
        uint32_t hi=read_reg(i9100_mct, I9100_MCT_G_COMP0_U);
        uint64_t m=(((uint64_t)hi)<<32)|lo;
        rem=m>now?(int64_t)(m-now):0;
    }
    ml_set_interrupts_enabled(w);
    return (uint64_t)rem;
}

void I9100_handle_interrupt(void *context)
{
    uint32_t ack, irq, pending;
    arm_saved_state_t *regs=(arm_saved_state_t *)context;
    if(!controller_ready) return;
    ack=read_reg(i9100_gic_cpu, I9100_GIC_CPU_INTACK);
    irq=ack & 0x3ffU;
    last_vector=irq;
    if(irq==1023U){ spurious_irq_count++; return; }

    pending=read_reg(i9100_mct, I9100_MCT_G_INT_CSTAT);
    if(irq==I9100_TIMER_IRQ || (pending & 1U)){
        irq_count++;
        write_reg(i9100_mct, I9100_MCT_G_INT_CSTAT, 1U);
        last_count_lo=(uint32_t)mct_get_frc();
        last_irq_counter=last_count_lo;
        write_reg(i9100_gic_cpu, I9100_GIC_CPU_EOI, ack);
        drain();
        /* Observe before etimer callbacks: a stuck callback must not hide
         * the final interrupt that entered it. */
        ios7i9100_timer_observe(last_count_lo, irq_count, regs->pc);
        i9100_usbcdc_poll();
        rtclock_intr(regs);
        return;
    }
    write_reg(i9100_gic_cpu, I9100_GIC_CPU_EOI, ack);
    drain();
}

void I9100_timer_snapshot(struct I9100TimerSnapshot *out)
{
    boolean_t was=ml_set_interrupts_enabled(FALSE);
    if(out){
        out->version=1U;
        out->valid_flags=(timebase_ready?1U:0)|(armed?2U:0);
        out->count_lo=last_count_lo;
        out->match_lo=last_match_lo;
        out->enable=armed;
        out->irq_status=timebase_ready?read_reg(i9100_mct, I9100_MCT_G_INT_CSTAT):0;
        out->irq_count=irq_count;
        out->last_irq_counter=last_irq_counter;
        out->last_vector=last_vector;
        out->spurious_irq_count=spurious_irq_count;
        out->frequency_hz=timer_hz;
        out->minimum_match_ticks=I9100_MCT_MIN_MATCH_TICKS;
        out->extended_ticks=timebase_ready?mct_get_frc():0;
    }
    ml_set_interrupts_enabled(was);
}
#endif
