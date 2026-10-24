/* I9100 Exynos4210 PE (Galaxy S2 GT-I9100). Adapted from the J1XLTE/HD2
 * architecture: validates loader handoff, maps FB/UART, installs the
 * external-GIC/MCT dispatch, provides a direct-FB display backend and a
 * fatal reporter.  Exports the shared ios7j1_* bring-up ABI so the common
 * arm_init/serial_console/bsd_init call sites link unchanged.
 * Numeric addresses are factual i9100 hardware data (Exynos 4210 manual,
 * QEMU smdkc210 model, uniLoader i9100_defconfig). */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <mach/mach_types.h>
#include <mach/host_info.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm/boot.h>
#include <pexpert/arm/protos.h>
#include <pexpert/arm/i9100_handoff.h>
#include <pexpert/arm/i9100_entry.h>
#include "i9100_timer.h"
#include <machine/machine_routines.h>
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

static volatile unsigned int *i9100_pixels;
static vm_offset_t i9100_fb_map;
static unsigned int i9100_fb_physical;
static volatile unsigned int i9100_graphics_owner,i9100_fatal_active;
static unsigned int i9100_stage;
static volatile unsigned int i9100_console_count;
static volatile unsigned int i9100_fatal_category;
static volatile unsigned int i9100_fatal_state[8];

void ios7j1_stage(unsigned int n)
{
 (void)n;
}
static unsigned int i9100_callsite(void *r){ return (((unsigned int)r)&~1U)-4U; }

int ios7j1_fatal_report(unsigned int cat, unsigned int cs,
 unsigned int pc,unsigned int lr,unsigned int far,unsigned int fsr,
 unsigned int mask,const char *msg)
{
 (void)cs;
 if(i9100_fatal_active) return 0;
 i9100_fatal_active=1;
 if(gPESocDispatch.uart_putc==ios7j1_debug_putc) (void)switch_to_serial_console();
 i9100_fatal_category=cat;
 i9100_fatal_state[0]=cat;i9100_fatal_state[1]=i9100_stage;
 i9100_fatal_state[2]=mask&15U;i9100_fatal_state[3]=pc;
 i9100_fatal_state[4]=lr;i9100_fatal_state[5]=far;i9100_fatal_state[6]=fsr;
 return 1;
}
void __attribute__((noreturn)) ios7j1_final_boundary(unsigned int reason)
{
 if(reason) ios7j1_fatal_report(0xf000U|(reason&0xfffU),
  i9100_callsite(__builtin_return_address(0)),0,0,0,0,0,"ENTRY GUARD");
 __asm__ volatile("cpsid if" ::: "memory");
 for(;;) __asm__ volatile("dsb sy\n\twfi" ::: "memory");
}
void ios7j1_forget_early_maps(void){ i9100_pixels=0; i9100_fb_map=0; }
/* UART2 S3C @0x13820000: TX-only debug putc (factual offsets; same
 * controller offset layout as J1's UART2).  Before SocSupport maps it,
 * write the physical address directly (early boot runs identity-mapped);
 * after, use the mapping. */
#define I9100_UART_PA 0x13820000U
static vm_offset_t i9100_uart_map;
static void i9100_uart_putc_phys(char c)
{
 volatile unsigned int *utxh=(volatile unsigned int *)(I9100_UART_PA+0x20);
 *utxh=(unsigned int)(unsigned char)c;
}
static void i9100_uart_putc_raw(char c)
{
 volatile unsigned int *utxh=(volatile unsigned int *)(i9100_uart_map+0x20);
 *utxh=(unsigned int)(unsigned char)c;
}
void ios7j1_debug_putc(char c)
{
 if(i9100_console_count<1024) i9100_console_count++;
 if(i9100_uart_map) i9100_uart_putc_raw(c);
 else i9100_uart_putc_phys(c);
}
static void i9100_puts(const char *s)
{
 while(*s) ios7j1_debug_putc(*s++);
}
static void i9100_puthex(unsigned int v)
{
 int i; char h[8];
 for(i=0;i<8;i++){ unsigned int n=(v>>((7-i)*4))&15U; h[i]=(char)(n<10?('0'+n):('a'+n-10)); }
 for(i=0;i<8;i++) ios7j1_debug_putc(h[i]);
}
/* Early fault reporter for bring-up (called from sleh_abort before any
 * handling that might itself fault). Lock-free, UART+FB only. */
static volatile unsigned int i9100_flt_n;
static volatile unsigned int i9100_flt_last_pc = 0xffffffffU;
void ios7j1_fault_report(unsigned int pc,unsigned int lr,unsigned int far,
                         unsigned int fsr,unsigned int cpsr,unsigned int reason)
{
 if(i9100_flt_n>=16) {
  if(pc==i9100_flt_last_pc) return;
  i9100_flt_last_pc=pc;
  i9100_puts("I9FLT+ pc=");
  i9100_puthex(pc);
  i9100_puts(" far=");
  i9100_puthex(far);
  i9100_puts(" fsr=");
  i9100_puthex(fsr);
  i9100_puts(" cpsr=");
  i9100_puthex(cpsr);
  i9100_puts(" reason=");
  i9100_puthex(reason);
  i9100_puts("\n");
  return;
 }
 i9100_flt_n++;
 i9100_puts("I9FLT n=");
 i9100_puthex(i9100_flt_n);
 i9100_puts(" pc=");
 i9100_puthex(pc);
 i9100_puts(" lr=");
 i9100_puthex(lr);
 i9100_puts(" far=");
 i9100_puthex(far);
 i9100_puts(" fsr=");
 i9100_puthex(fsr);
 i9100_puts(" cpsr=");
 i9100_puthex(cpsr);
 i9100_puts(" reason=");
 i9100_puthex(reason);
 i9100_puts("\n");
}
void ios7j1_fault_regs(unsigned int r0,unsigned int r1,unsigned int r2,
                       unsigned int r3)
{
 if(i9100_flt_n>=16) return;
 i9100_puts("I9REGS r0=");
 i9100_puthex(r0);
 i9100_puts(" r1=");
 i9100_puthex(r1);
 i9100_puts(" r2=");
 i9100_puthex(r2);
 i9100_puts(" r3=");
 i9100_puthex(r3);
 i9100_puts("\n");
}
static int i9100_getc(void){ return -1; }
static void i9100_uart_init(void){ }
void ios7j1_kernel_continuing(void){ }
void ios7j1_timer_observe(unsigned int t,unsigned int c){ (void)t;(void)c; }
int ios7j1_panic_text_begin(unsigned int r,unsigned int k,unsigned int p){ (void)r;(void)k;(void)p; return 0; }
void ios7j1_panic_text_putc(char c){ (void)c; }
void ios7j1_panic_text_end(void){ }

/* Direct-FB backend: 480x800x32 CPU copy, no DMA (always completed).
 * uniLoader hands the kernel a scratch FB at 0x5E000000. */
static uint64_t i9100_graphics_seq=1;
volatile int i9100_panel_console_enabled=1;
int ios7j1_display_ready(void)
{
 return i9100_pixels && !i9100_fatal_active &&
  PE_state.video.v_width==480 && PE_state.video.v_height==800 &&
  PE_state.video.v_rowBytes==1920 && PE_state.video.v_depth==32;
}
int ios7j1_display_present(const uint8_t *px,uint32_t bytes,uint32_t row,
                           uint32_t fmt,uint64_t *seq)
{
 unsigned int y;
 if(!px||!seq) return 0;
 if(!(fmt==0x42475241U||fmt==0x52475241U)) return 0;
 if(row<1920U||bytes<1536000U) return 0;
 if(!ios7j1_display_ready()) return 0;
 for(y=0;y<800;y++){
  const uint8_t *src=px+(uint64_t)y*row;
  uint8_t *dst=(uint8_t *)i9100_pixels+y*1920U;
  unsigned int i;
  for(i=0;i<1920U;i++) dst[i]=src[i];
 }
 *seq=i9100_graphics_seq++;
 if(!i9100_graphics_seq) i9100_graphics_seq=1;
 i9100_panel_console_enabled=0;
 return 1;
}
int ios7j1_display_completed(uint64_t s){ return s && s<i9100_graphics_seq; }

static int i9100_video_ok(const boot_args *a)
{
 unsigned long long end=(unsigned long long)a->Video.v_baseAddr+1536000U;
 unsigned long long ramend=(unsigned long long)a->physBase+a->memSize;
 return a->Revision==1 && a->Version==2 && a->virtBase==0x80000000U &&
  a->physBase==0x40000000U && a->memSize==0x30000000U &&
  a->topOfKernelData>=a->physBase && !(a->topOfKernelData&0x3fffU) &&
  (unsigned long long)a->topOfKernelData+0x220000U<=ramend &&
  a->Video.v_width==480 && a->Video.v_height==800 &&
  a->Video.v_rowBytes==1920 && a->Video.v_depth==32 &&
  !(a->Video.v_baseAddr&3U) && a->Video.v_baseAddr==0x5E000000U &&
  end<=(unsigned long long)0x5E000000U+0x200000U;
}
void ios7j1_checkpoint(boot_args *a, unsigned int stage)
{
 if(i9100_fatal_active) return;
 if(stage==6){
  if(!i9100_video_ok(a)) ios7j1_final_boundary(1);
  i9100_fb_physical=a->Video.v_baseAddr;
  i9100_pixels=(volatile unsigned int *)i9100_fb_physical;
  gPESocDispatch.uart_putc=ios7j1_debug_putc;
  gPESocDispatch.uart_getc=i9100_getc;
  gPESocDispatch.uart_init=i9100_uart_init;
  i9100_puts("I9100 stage6 video ok\n");
 }
 if(stage!=i9100_stage+1U||stage<6||stage>10){
  if(!(stage==6&&i9100_stage==0)) ios7j1_final_boundary(3);
 }
 i9100_stage=stage;
 if(stage==7) i9100_puts("I9100 stage7\n");
 else if(stage==8) i9100_puts("I9100 stage8\n");
 else if(stage==9) i9100_puts("I9100 stage9\n");
 else if(stage==10) i9100_puts("I9100 stage10\n");
}
void PE_init_SocSupport_stub(void)
{
 DTEntry chosen;
 ios7lab_i9100_handoff *h=0;
 unsigned int size=0;
 boot_args *args=(boot_args *)PE_state.bootArgs;
 vm_offset_t fb;
 static ios7lab_i9100_handoff i9100_default_handoff;
 if(!i9100_video_ok(args)) ios7j1_final_boundary(4);
 if(DTLookupEntry(0,"/chosen",&chosen)==kSuccess &&
    DTGetProperty(chosen,IOS7LAB_I9100_DT_PROPERTY,(void **)&h,&size)==kSuccess &&
    size==128&&h&&h->magic==IOS7LAB_I9100_MAGIC&&h->version==1&&
    h->bytes==128&&h->machine==IOS7LAB_I9100_MACHINE){
  /* loader-provided handoff accepted as-is */
 }else{
  i9100_default_handoff.magic=IOS7LAB_I9100_MAGIC;
  i9100_default_handoff.version=1;
  i9100_default_handoff.bytes=128;
  i9100_default_handoff.flags=IOS7LAB_I9100_FLAGS;
  i9100_default_handoff.machine=IOS7LAB_I9100_MACHINE;
  i9100_default_handoff.disp_physical=IOS7LAB_I9100_DISP_PHYSICAL;
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
 fb=ml_io_map(i9100_fb_physical,1536000);
 i9100_uart_map=ml_io_map(I9100_UART_PA,4096);
 if(!fb||!i9100_uart_map) ios7j1_final_boundary(5);
 {
  extern unsigned int cons_ops[], nconsops, cons_ops_index;
  unsigned int ttbr0, ttbr1, ttcr, l1e, l2e, pa;
  unsigned int *l1tab;
  i9100_puts("I9DATA cons_ops[0]=");
  i9100_puthex(cons_ops[0]);
  i9100_puts(" [1]=");
  i9100_puthex(cons_ops[2]);
  i9100_puts(" n=");
  i9100_puthex(nconsops);
  i9100_puts(" idx=");
  i9100_puthex(cons_ops_index);
  i9100_puts("\n");
  __asm__ volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
  __asm__ volatile("mrc p15,0,%0,c2,c0,1" : "=r"(ttbr1));
  __asm__ volatile("mrc p15,0,%0,c2,c0,2" : "=r"(ttcr));
  l1tab=(unsigned int *)((ttbr1&~0x3FFFU)-0x40000000U+0x80000000U);
  if((ttcr&7U)==0)
   l1tab=(unsigned int *)((ttbr0&~0x3FFFU)-0x40000000U+0x80000000U);
  l1e=l1tab[0x8066ece0U>>20];
  pa=0; l2e=0;
  if((l1e&3U)==1){
   unsigned int *l2=(unsigned int *)((l1e&~0x3FFU)-0x40000000U+0x80000000U);
   l2e=l2[(0x8066ece0U>>12)&0xFFU];
   if((l2e&3U)==2||(l2e&3U)==3)
    pa=(l2e&0xFFFFF000U)|(0x8066ece0U&0xFFFU);
  }else if((l1e&3U)==2){
   pa=(l1e&0xFFF00000U)|(0x8066ece0U&0xFFFFFU);
  }
  i9100_puts("I9DATA ttbr0=");
  i9100_puthex(ttbr0);
  i9100_puts(" ttbr1=");
  i9100_puthex(ttbr1);
  i9100_puts(" ttcr=");
  i9100_puthex(ttcr);
  i9100_puts(" l1=");
  i9100_puthex(l1e);
  i9100_puts(" l2=");
  i9100_puthex(l2e);
  i9100_puts(" pa=");
  i9100_puthex(pa);
  i9100_puts("\n");
 }
 i9100_pixels=(volatile unsigned int *)fb;
 i9100_fb_map=fb;
 PE_state.video.v_baseAddr=fb;
 PE_state.video.v_rowBytes=1920;PE_state.video.v_width=480;
 PE_state.video.v_height=800;PE_state.video.v_depth=32;
 gPESocDispatch.uart_putc=ios7j1_debug_putc;
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
 ios7j1_checkpoint(args,9);
 __asm__ volatile("" ::: "memory");
}

/* Fast panel console (same design as J1's): draws SET pixels only,
 * transparent background, wraps at 480x800, clears-on-full. */
#define I9F_W 8U
#define I9F_H 16U
#define I9F_COLS 60U
#define I9F_ROWS 50U
#define I9F_FB_W 480U
#define I9F_FB_H 800U
static unsigned int i9f_cx, i9f_cy;
static int i9f_remapped, i9f_wiped;extern unsigned char iso_font[];
extern boolean_t pmap_initialized;
static void i9f_blank_line(volatile unsigned int *fb, unsigned int y)
{
    unsigned int x, yy;
    for (yy = 0; yy < 16; yy++) {
        volatile unsigned int *row = fb + (y + yy) * 480U;
        for (x = 0; x < 480; x++) row[x] = 0xFF000000U;
    }
}
void ios7j1_fast_putc(char c)
{
    volatile unsigned int *fb = i9100_pixels;
    unsigned int x, y;
    unsigned char uc = (unsigned char)c;
    if (!i9100_panel_console_enabled) return;
    if (!i9f_remapped && pmap_initialized) {
        vm_offset_t nfb = ml_io_map(i9100_fb_physical, 1536000);
        if (nfb) { i9100_pixels = (volatile unsigned int *)nfb; fb = i9100_pixels; }
        i9f_remapped = 1;
    }
    if (!fb) return;
    if (!i9f_wiped) {
        unsigned int i, n = 480U * 800U;
        for (i = 0; i < n; i++) fb[i] = 0xFF000000U;
        i9f_wiped = 1;
        i9f_cx = 0; i9f_cy = 0;
    }
    if (c == '\n') {
        i9f_cx = 0; i9f_cy += 16;
        if (i9f_cy + 16 > 800) i9f_cy = 0;
        i9f_blank_line(fb, i9f_cy);
    }
    else if (c == '\r') { i9f_cx = 0; }
    else if (uc >= 32 && uc < 127) {
        unsigned char *g = &iso_font[uc * 16];
        for (y = 0; y < 16; y++) {
            unsigned char bits = g[y];
            volatile unsigned int *row =
                fb + (i9f_cy + y) * 480U + i9f_cx;
            for (x = 0; x < 8; x++)
                if (bits & (1U << x)) row[x] = 0xFFFFFFFFU;
        }
        i9f_cx += 8;
        if (i9f_cx + 8 > 480) {
            i9f_cx = 0; i9f_cy += 16;
            if (i9f_cy + 16 > 800) i9f_cy = 0;
            i9f_blank_line(fb, i9f_cy);
        }
    }
}
#endif
