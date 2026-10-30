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
void i9100_usbcdc_putc(char c);
void i9100_usbcdc_init(void);

void ios7j1_debug_putc(char c)
{
 if(i9100_console_count<1024) i9100_console_count++;
 if(i9100_uart_map) i9100_uart_putc_raw(c);
 else i9100_uart_putc_phys(c);
 i9100_usbcdc_putc(c);
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
 { extern void i9100_usbcdc_setmap(void); i9100_usbcdc_setmap(); }
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
extern addr64_t kvtophys(vm_offset_t);
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
    /* Guard: if the fb mapping was torn out of kernel_pmap, every console
     * print would fault inside this routine and recursively abort the
     * abort handler (observed livelock: DFAR=fb VA, DFSR=0x807).
     * Verify the PTE on every call; on loss, self-disable so the
     * UART sink keeps working. */
    if (pmap_initialized && kvtophys((vm_offset_t)fb) == 0) {
        i9100_panel_console_enabled = 0;
        i9100_pixels = 0;
        kdb_printf("J9FB: panel console va=%p lost mapping; disabling\n", (void*)fb);
        return;
    }
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

/* ---------------- I9100 USB CDC debug console (kernel side) -----------
 * Continuation of uniLoader's dwc2cdc4210 gadget: the DWC2 OTG device
 * @0x12480000 is adopted as-is when uniLoader enumerated it (the COM
 * port on the PC then stays open across the loader->kernel boundary),
 * or fully re-enumerated when it was not.  Pure polled slave mode,
 * driven from ios7j1_debug_putc: every kprintf char feeds a 4KB ring
 * and pumps EP1-IN; EP0 control traffic is serviced as a side effect.
 * No IRQs, no DMA, every wait bounded - a dead/absent/absorbed-by-QEMU
 * controller can never stall the kernel.  Panic text rides the same
 * putc, so the serial wire sees crash dumps live.
 */

#define I9100_DWC2_PA	0x12480000U
#define I9100_PHY_PA	0x125B0000U
#define I9100_PMU_USBPHY	0x10020704U

static vm_offset_t i9100_usb_map;

#define UREG(x) (*(volatile unsigned int *) \
		 (i9100_usb_map ? i9100_usb_map + (x) : I9100_DWC2_PA + (x)))
#define UGOTGCTL	UREG(0x000)
#define UGAHBCFG	UREG(0x008)
#define UGUSBCFG	UREG(0x00C)
#define UGRSTCTL	UREG(0x010)
#define UGINTSTS	UREG(0x014)
#define UGRXSTSP	UREG(0x020)
#define UGRXFSIZ	UREG(0x024)
#define UGNPTXFSIZ	UREG(0x028)
#define UGNPTXSTS	UREG(0x02C)
#define UGSNPSID	UREG(0x040)
#define UDPTXF(n)	UREG(0x104 + ((n) - 1) * 4)
#define UDCFG		UREG(0x800)
#define UDCTL		UREG(0x804)
#define UDSTS		UREG(0x808)
#define UDIEPMSK	UREG(0x810)
#define UDOEPMSK	UREG(0x814)
#define UDAINT		UREG(0x818)
#define UDAINTMSK	UREG(0x81C)
#define UIEP(n)		UREG(0x900 + (n) * 0x20)
#define UOEP(n)		UREG(0xB00 + (n) * 0x20)
#define UDIEPCTL(n)	UIEP(n)
#define UDIEPINT(n)	UREG(0x908 + (n) * 0x20)
#define UDIEPTSIZ(n)	UREG(0x910 + (n) * 0x20)
#define UDOEPCTL(n)	UOEP(n)
#define UDOEPINT(n)	UREG(0xB08 + (n) * 0x20)
#define UDOEPTSIZ(n)	UREG(0xB10 + (n) * 0x20)
#define UDTXFSTS(n)	UREG(0x918 + (n) * 0x20)
#define UFIFO(n)	UREG(0x1000 + (n) * 0x1000)
#define UPCGCCTL	UREG(0xE00)
#define UPHYREG(x)	(*(volatile unsigned int *) \
		 (i9100_usb_map ? (i9100_usb_map + 0x1B0000 - 0x12480000 + (x)) \
				: I9100_PHY_PA + (x)))

#define UGRST_AHBIDLE	(1u << 31)
#define UGRST_TXFNUM(n)	((n) << 6)
#define UGRST_TXFFLSH	(1u << 5)
#define UGRST_RXFFLSH	(1u << 4)
#define UGRST_CSFT	(1u << 0)
#define UGINT_WKUP	(1u << 31)
#define UGINT_OEP	(1u << 19)
#define UGINT_IEP	(1u << 18)
#define UGINT_ENUM	(1u << 13)
#define UGINT_USBRST	(1u << 12)
#define UGINT_RXFLVL	(1u << 4)
#define UEPCTL_EPENA	(1u << 31)
#define UEPCTL_EPDIS	(1u << 30)
#define UEPCTL_SD0	(1u << 28)
#define UEPCTL_SNAK	(1u << 27)
#define UEPCTL_CNAK	(1u << 26)
#define UEPCTL_STALL	(1u << 21)
#define UEPCTL_EPTYPE(x) ((x) << 18)
#define UEPCTL_ACT	(1u << 15)
#define UEPCTL_MPS(x)	((x) & 0x7ff)
#define UEPINT_XFC	(1u << 0)
#define UEPINT_AHBERR	(1u << 2)
#define UDCTL_SFTDIS	(1u << 1)
#define URX_PKTSTS(x)	(((x) >> 17) & 0xf)
#define URX_BYTECNT(x)	(((x) >> 4) & 0x7ff)

enum { USB_DEAD, USB_ADOPT, USB_OURS } ;
static int i9100_usb_mode;
static int i9100_usb_probed, i9100_usb_in, i9100_usb_busy1;
static int i9100_usb_configured, i9100_usb_hostopen, i9100_usb_enum;
static unsigned char i9100_usb_newaddr;
static unsigned char i9100_usb_setup[8];
static const unsigned char *i9100_usb_e0buf;
static unsigned int i9100_usb_e0len, i9100_usb_e0ask;
static int i9100_usb_e0st;
#define UE0_WAIT 0
#define UE0_DIN  1
#define UE0_SIN  2
#define UE0_SOUT 3
#define UE0_DOUT 4

#define UTXRING 4096
static char i9100_usb_ring[UTXRING];
static volatile unsigned int i9100_usb_h, i9100_usb_t;

static const unsigned char usb_dev_desc[] = {
	18, 1, 0, 2, 2, 0, 0, 64, 0x25, 0x05, 0xa7, 0xa4, 0x01, 0,
	1, 2, 3, 1 };
static const unsigned char usb_cfg_desc[] = {
	9, 2, 67, 0, 2, 1, 0, 0x80, 0xfa,
	9, 4, 0, 0, 1, 2, 2, 1, 0,
	5, 0x24, 0, 0x10, 0x01,
	5, 0x24, 1, 0, 1,
	4, 0x24, 2, 0x02,
	5, 0x24, 6, 0, 1,
	7, 5, 0x82, 3, 8, 0, 255,
	9, 4, 1, 0, 2, 10, 0, 0, 0,
	7, 5, 0x81, 2, 64, 0, 0,
	7, 5, 0x01, 2, 64, 0, 0 };
static const unsigned char usb_str0[] = { 4, 3, 9, 4 };
static const unsigned char usb_str1[] = {
	30, 3, 'i',0,'9',0,'1',0,'0',0,'0',0,' ',0,'u',0,'n',0,'i',0,
	'L',0,'o',0,'a',0,'d',0,'e',0,'r',0 };
static const unsigned char usb_str2[] = {
	26, 3, 'X',0,'N',0,'U',0,' ',0,'C',0,'D',0,'C',0,' ',0,'L',0,
	'O',0,'G',0 };
static const unsigned char usb_str3[] = {
	20, 3, 'S',0,'2',0,'I',0,'9',0,'1',0,'0',0,'0',0,'-',0,'1',0 };

static void i9100_usb_spin(unsigned int n)
{
	volatile unsigned int i;
	while (n--)
		for (i = 0; i < 200; i++)
			__asm__ __volatile__("nop");
}

static int i9100_usb_wait(volatile unsigned int *r, unsigned int m,
			  unsigned int v)
{
	unsigned int t = 100000;
	while (t--) {
		if ((*r & m) == v)
			return 0;
		i9100_usb_spin(1);
	}
	return -1;
}

static void i9100_usb_fifos(void)
{
	UGRXFSIZ = 0x200;
	UGNPTXFSIZ = (0x80 << 16) | 0x200;
	UDPTXF(1) = (0x80 << 16) | 0x280;
	UDPTXF(2) = (0x20 << 16) | 0x300;
	UGRSTCTL = UGRST_RXFFLSH;
	i9100_usb_wait(&UGRSTCTL, UGRST_RXFFLSH, 0);
	UGRSTCTL = UGRST_TXFNUM(0x10) | UGRST_TXFFLSH;
	i9100_usb_wait(&UGRSTCTL, UGRST_TXFFLSH, 0);
}

static void i9100_usb_e0arm(void)
{
	UDOEPTSIZ(0) = (3 << 29) | (1 << 19) | 24;
	UDOEPCTL(0) |= UEPCTL_EPENA;
}

static void i9100_usb_devinit(void)
{
	int i;

	UDCFG = (1 << 18) | 0;
	UDCTL |= UDCTL_SFTDIS;
	i9100_usb_spin(20);
	UDCTL &= ~UDCTL_SFTDIS;
	for (i = 0; i < 4; i++) {
		UDOEPCTL(i) = UEPCTL_EPDIS | UEPCTL_SNAK;
		UDIEPCTL(i) = UEPCTL_EPDIS | UEPCTL_SNAK;
	}
	UDIEPMSK = 7;
	UDOEPMSK = 0xB;
	UDAINTMSK = (1 << 16) | 1;
	i9100_usb_fifos();
	i9100_usb_e0st = UE0_WAIT;
	i9100_usb_e0len = 0;
	i9100_usb_busy1 = 0;
	i9100_usb_configured = 0;
	i9100_usb_hostopen = 0;
	i9100_usb_e0arm();
}

static void i9100_usb_hardinit(void)
{
	*(volatile unsigned int *)(i9100_usb_map ?
		i9100_usb_map + (0x10020704 - 0x12480000) :
		I9100_PMU_USBPHY) |= 1;
	UPHYREG(0) &= ~(0x39u);
	UPHYREG(4) = (UPHYREG(4) & ~0x14) | 0x3;
	UPHYREG(8) = (UPHYREG(8) & ~6) | 1;
	i9100_usb_spin(20);
	UPHYREG(8) &= ~7;
	i9100_usb_spin(20);

	i9100_usb_wait(&UGRSTCTL, UGRST_AHBIDLE, UGRST_AHBIDLE);
	UGRSTCTL = UGRST_CSFT;
	i9100_usb_wait(&UGRSTCTL, UGRST_CSFT, 0);
	UGUSBCFG = (1 << 30) | (1 << 14) | (0x5 << 10) | 0x7;
	i9100_usb_fifos();
	i9100_usb_devinit();
	i9100_usb_mode = USB_OURS;
}

static int i9100_usb_e0tx(unsigned int len)
{
	unsigned int words = (len + 3) >> 2, i;
	unsigned int w;

	if ((UGNPTXSTS & 0xffff) < words)
		return -1;
	UDIEPTSIZ(0) = (1 << 19) | len;
	for (i = 0; i < words; i++) {
		memcpy(&w, i9100_usb_e0buf + i * 4, 4);
		UFIFO(0) = w;
	}
	UDIEPCTL(0) |= UEPCTL_EPENA | UEPCTL_CNAK;
	i9100_usb_e0buf += len;
	i9100_usb_e0len -= len;
	return 0;
}

static void i9100_usb_e0reply(const unsigned char *b, unsigned int l,
			      unsigned int ask)
{
	i9100_usb_e0buf = b;
	i9100_usb_e0len = l < ask ? l : ask;
	i9100_usb_e0ask = ask;
	i9100_usb_e0st = UE0_DIN;
	i9100_usb_e0tx(i9100_usb_e0len > 64 ? 64 : i9100_usb_e0len);
}

static void i9100_usb_e0sin(void)
{
	i9100_usb_e0st = UE0_SIN;
	UDIEPTSIZ(0) = 1 << 19;
	UDIEPCTL(0) |= UEPCTL_EPENA | UEPCTL_CNAK;
}

static void i9100_usb_e0stall(void)
{
	UDIEPCTL(0) |= UEPCTL_STALL;
	UDOEPCTL(0) |= UEPCTL_STALL;
	i9100_usb_e0st = UE0_WAIT;
}

static void i9100_usb_e0setup(void)
{
	unsigned char rt = i9100_usb_setup[0], rq = i9100_usb_setup[1];
	unsigned int wv = i9100_usb_setup[2] | (i9100_usb_setup[3] << 8);
	unsigned int wl = i9100_usb_setup[6] | (i9100_usb_setup[7] << 8);
	static const unsigned char lc[7] =
		{ 0, 0xc2, 0x01, 0, 0, 0, 8 };

	if (rt == 0x80 && rq == 6) {
		switch (wv >> 8) {
		case 1:
			i9100_usb_e0reply(usb_dev_desc,
				sizeof(usb_dev_desc), wl);
			return;
		case 2:
			i9100_usb_e0reply(usb_cfg_desc,
				sizeof(usb_cfg_desc), wl);
			return;
		case 3:
			switch (wv & 0xff) {
			case 0: i9100_usb_e0reply(usb_str0, 4, wl); return;
			case 1: i9100_usb_e0reply(usb_str1, 30, wl); return;
			case 2: i9100_usb_e0reply(usb_str2, 26, wl); return;
			default: i9100_usb_e0reply(usb_str3, 20, wl); return;
			}
		}
		i9100_usb_e0stall();
		return;
	}
	if (rt == 0x00 && rq == 5) {
		i9100_usb_newaddr = wv & 0x7f;
		i9100_usb_e0sin();
		return;
	}
	if (rt == 0x00 && rq == 9) {
		UDIEPCTL(1) = UEPCTL_ACT | UEPCTL_EPTYPE(2) | (1 << 22) |
			UEPCTL_MPS(64) | UEPCTL_EPENA | UEPCTL_CNAK |
			UEPCTL_SD0;
		UDIEPCTL(2) = UEPCTL_ACT | UEPCTL_EPTYPE(3) | (2 << 22) |
			UEPCTL_MPS(8) | UEPCTL_SNAK;
		UDOEPCTL(1) = UEPCTL_ACT | UEPCTL_EPTYPE(2) |
			UEPCTL_MPS(64) | UEPCTL_EPENA | UEPCTL_CNAK;
		i9100_usb_configured = 1;
		i9100_usb_e0sin();
		return;
	}
	if (rt == 0x21 && rq == 0x20) {
		i9100_usb_e0st = UE0_DOUT;
		UDOEPTSIZ(0) = (1 << 19) | 7;
		UDOEPCTL(0) |= UEPCTL_EPENA | UEPCTL_CNAK;
		return;
	}
	if (rt == 0xa1 && rq == 0x21) {
		i9100_usb_e0reply(lc, 7, wl);
		return;
	}
	if (rt == 0x21 && rq == 0x22) {
		i9100_usb_hostopen = 1;
		i9100_usb_e0sin();
		return;
	}
	if (rq == 1 || rq == 3)
		i9100_usb_e0sin();
	else if (rt & 0x80)
		i9100_usb_e0reply((const unsigned char *)"\0\0", 2, wl);
	else
		i9100_usb_e0sin();
}

static void i9100_usb_e0indone(void)
{
	if (i9100_usb_e0st == UE0_DIN) {
		if (i9100_usb_e0len) {
			i9100_usb_e0tx(i9100_usb_e0len > 64 ? 64
							  : i9100_usb_e0len);
			return;
		}
		i9100_usb_e0st = UE0_SOUT;
		i9100_usb_e0arm();
		return;
	}
	if (i9100_usb_e0st == UE0_SIN) {
		if (i9100_usb_newaddr) {
			UDCFG = (UDCFG & ~(0x7f << 4)) |
				(i9100_usb_newaddr << 4);
			i9100_usb_newaddr = 0;
		}
		i9100_usb_e0st = UE0_WAIT;
	}
}

static void i9100_usb_rxlvl(void)
{
	unsigned int gs = UGRXSTSP, ps = URX_PKTSTS(gs), bc = URX_BYTECNT(gs);
	unsigned int i;

	if (ps == 6) {
		unsigned int lo = UGRXSTSP, hi = UGRXSTSP;
		memcpy(i9100_usb_setup, &lo, 4);
		memcpy(i9100_usb_setup + 4, &hi, 4);
	} else if (ps == 4) {
		i9100_usb_e0setup();
	} else if (ps == 2) {
		for (i = 0; i < (bc + 3) / 4; i++)
			(void)UGRXSTSP;
		if (i9100_usb_e0st == UE0_DOUT)
			i9100_usb_e0sin();
	} else if (ps == 3) {
		if (i9100_usb_e0st == UE0_SOUT)
			i9100_usb_e0st = UE0_WAIT;
		i9100_usb_e0arm();
	} else {
		for (i = 0; i < (bc + 3) / 4; i++)
			(void)UGRXSTSP;
	}
}

static void i9100_usb_drain(void)
{
	unsigned char pkt[64];
	unsigned int n = 0, i, w;

	if (i9100_usb_busy1 || !i9100_usb_configured ||
	    !i9100_usb_hostopen || i9100_usb_h == i9100_usb_t)
		return;
	while (n < sizeof(pkt) && i9100_usb_t != i9100_usb_h) {
		pkt[n++] = i9100_usb_ring[i9100_usb_t];
		i9100_usb_t = (i9100_usb_t + 1) & (UTXRING - 1);
	}
	if ((UDTXFSTS(1) & 0xffff) < (n + 3) / 4)
		return;
	UDIEPTSIZ(1) = (1 << 19) | n;
	for (i = 0; i < (n + 3) / 4; i++) {
		w = pkt[i * 4] | (pkt[i * 4 + 1] << 8) |
		    (pkt[i * 4 + 2] << 16) |
		    ((i * 4 + 3 < n ? pkt[i * 4 + 3] : 0) << 24);
		UFIFO(1) = w;
	}
	UDIEPCTL(1) |= UEPCTL_EPENA | UEPCTL_CNAK;
	i9100_usb_busy1 = 1;
}

static void i9100_usb_poll(void)
{
	unsigned int st;

	if (!i9100_usb_probed)
		return;
	st = UGINTSTS;
	if (st & UGINT_USBRST) {
		UGINTSTS = UGINT_USBRST;
		i9100_usb_newaddr = 0;
		i9100_usb_mode = USB_OURS;
		i9100_usb_devinit();
	}
	if (st & UGINT_ENUM) {
		UGINTSTS = UGINT_ENUM;
		i9100_usb_enum = 1;
		UDIEPCTL(0) = (UDIEPCTL(0) & ~3u) | UEPCTL_CNAK;
		UDOEPCTL(0) = (UDOEPCTL(0) & ~3u) | UEPCTL_CNAK;
		i9100_usb_e0arm();
	}
	if (st & UGINT_RXFLVL) {
		UGINTSTS = UGINT_RXFLVL;
		i9100_usb_rxlvl();
	}
	if (st & UGINT_IEP) {
		unsigned int d = UDAINT & 0xffff;
		if (d & 1) {
			unsigned int e = UDIEPINT(0);
			UDIEPINT(0) = e;
			if (e & UEPINT_XFC)
				i9100_usb_e0indone();
		}
		if (d & 2) {
			unsigned int e = UDIEPINT(1);
			UDIEPINT(1) = e;
			if (e & (UEPINT_XFC | UEPINT_AHBERR))
				i9100_usb_busy1 = 0;
		}
	}
	i9100_usb_drain();
}

void i9100_usbcdc_putc(char c)
{
	unsigned int nx;

	/* No MMIO until i9100_usbcdc_setmap() gave us a real kernel VA:
	 * the early identity map does not cover 0x12480000, and poking it
	 * in that window takes a translation data abort (seen: silent
	 * crash loop on QEMU before the first kprintf).  Chars still land
	 * in the ring and flush out once USB init runs. */
	if (!i9100_usb_map)
		goto ring_only;
	if (!i9100_usb_probed)
		i9100_usbcdc_init();
	if (i9100_usb_mode == USB_DEAD)
		return;
ring_only:
	nx = (i9100_usb_h + 1) & (UTXRING - 1);
	if (nx != i9100_usb_t) {
		i9100_usb_ring[i9100_usb_h] = c;
		i9100_usb_h = nx;
	}
	if (!i9100_usb_map || i9100_usb_mode == USB_DEAD)
		return;
	if (i9100_usb_in)
		return;
	i9100_usb_in = 1;
	i9100_usb_poll();
	i9100_usb_in = 0;
}

/* ml_io_map() became usable: swap the register base from the
 * identity-mapped PA to a proper kernel VA.  Any adopted session is
 * untouched - only the access path changes. */
void i9100_usbcdc_setmap(void)
{
	i9100_usb_map = ml_io_map(I9100_DWC2_PA, 0x4000);
	if (!i9100_usb_map) {
		i9100_usb_mode = USB_DEAD;
		return;
	}
	i9100_usbcdc_init();
}

/* Probe/adopt or hard-init.  Called lazily on the first putc and again
 * after ml_io_map() provided a real mapping (keeps an adopted
 * uniLoader session alive - the COM port stays open). */
void i9100_usbcdc_init(void)
{
	unsigned int ctl1;

	UPCGCCTL = 0;
	i9100_usb_spin(10);
	if ((UGSNPSID & 0xffff0000) != 0x4f540000) {
		i9100_usb_mode = USB_DEAD;
		i9100_usb_probed = 1;
		return;
	}
	ctl1 = UDIEPCTL(1);
	if ((UDCFG & (0x7f << 4)) && (ctl1 & UEPCTL_ACT)) {
		i9100_usb_mode = USB_ADOPT;
		i9100_usb_configured = 1;
		i9100_usb_hostopen = 1;
	} else {
		i9100_usb_hardinit();
	}
	i9100_usb_probed = 1;
}

#endif
