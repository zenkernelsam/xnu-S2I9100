/* Offline protocol/ring regression, not a DWC2 or USB wire simulator. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uintptr_t vm_offset_t;
typedef int boolean_t;
#define FALSE 0
#define BOARD_CONFIG_EXYNOS4210_I9100 1
#define I9100_USB_SIM 1
static uint32_t regs[0x1000], rx[2];
static unsigned rx_pos;
static boolean_t interrupts = 1;
static boolean_t ml_set_interrupts_enabled(boolean_t state)
{ boolean_t old = interrupts; interrupts = state; return old; }
static vm_offset_t ml_io_map(vm_offset_t p, unsigned n)
{ (void)p; (void)n; return 1; }
#define UREG(off) regs[(off) / 4]
#define URXFIFO() rx[rx_pos++ & 1]
#include "../pexpert/arm/i9100_usbcdc.c"
static unsigned fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %u: %s\n", __LINE__, #c); } } while (0)

static void setup(unsigned rt, unsigned rq, unsigned value, unsigned length)
{
    rx[0] = rt | (rq << 8) | (value << 16);
    rx[1] = length << 16;
    rx_pos = 0;
    UGRXSTSP = (6 << 17) | (8 << 4);
    UGINTSTS = UGINT_RXFLVL;
    i9100_usbcdc_poll();
    UGINTSTS = 0;
    CHECK(rx_pos == 2);
    CHECK(UGRXSTSP == ((6 << 17) | (8 << 4)));
    CHECK((UDOEPCTL(0) & (UEPCTL_EPENA | UEPCTL_ACT)) == (UEPCTL_EPENA | UEPCTL_ACT));
}

int main(void)
{
    i9100_usb_map = 1;
    i9100_usb_probed = 1;
    i9100_usb_mode = USB_OURS;
    UDTXFSTS(0) = 256;
    UDTXFSTS(1) = 384;
    i9100_usb_devinit();
    CHECK(UGAHBCFG == 0);
    CHECK(UGRSTCTL == 0); /* bus reset must not flush/disable the live RX FIFO */
    CHECK(UGINTMSK == 0x800c3810);
    CHECK((UDAINTMSK & 0x30003) == 0x30003);
    CHECK(UDIEPCTL(0) == UEPCTL_ACT);
    CHECK(usb_str1[0] == sizeof(usb_str1));
    CHECK(usb_str2[0] == sizeof(usb_str2));
    CHECK(usb_str3[0] == sizeof(usb_str3));
    CHECK(usb_cfg_desc[43] >= 1 && usb_cfg_desc[43] <= 16);
    setup(0x80, 6, 0x303, 255);
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | sizeof(usb_str3)));
    setup(0x80, 6, 0x100, 18);
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | 18));
    CHECK(i9100_usb_e0len == 0);
    setup(0x80, 6, 0x200, 67);
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | 64));
    CHECK(i9100_usb_e0len == 3);
    i9100_usb_e0indone();
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | 3));
    CHECK(i9100_usb_e0len == 0);
    setup(0x80, 6, 0x600, 10);
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | 10));
    setup(0x80, 6, 0x700, 67);
    CHECK(i9100_usb_e0len == 3);
    setup(0, 5, 7, 0);
    CHECK(((UDCFG >> 4) & 127) == 7);
    setup(0, 9, 1, 0);
    CHECK(i9100_usb_configured);
    CHECK(!(UDIEPCTL(1) & UEPCTL_EPENA));
    CHECK((UDIEPCTL(1) & 0x7ff) == 512);
    UDSTS = 1 << 1;
    UGINTSTS = UGINT_ENUM;
    i9100_usbcdc_poll();
    CHECK(i9100_usb_bulk_mps == 64 && usb_cfg_desc[57] == 64 && usb_cfg_desc[58] == 0);
    UDSTS = 0;
    UGINTSTS = UGINT_ENUM;
    i9100_usbcdc_poll();
    CHECK(i9100_usb_bulk_mps == 512 && usb_cfg_desc[57] == 0 && usb_cfg_desc[58] == 2);
    /* No putc: the public periodic poll must service a late host request. */
    setup(0xa1, 0x21, 0, 7);
    CHECK(UDIEPTSIZ(0) == ((1 << 19) | 7));
    CHECK(interrupts == 1);
    for (unsigned n = 1; n <= 64; n++) {
        i9100_usb_h = n;
        i9100_usb_t = 0;
        i9100_usb_busy1 = 0;
        memset(i9100_usb_ring, 0x81, n);
        i9100_usb_drain();
        CHECK(i9100_usb_t == n);
        CHECK(UDIEPTSIZ(1) == ((1 << 19) | (1U << 29) | n));
        unsigned remaining = n % 4;
        if (remaining) CHECK((UFIFO(1) >> (remaining * 8)) == 0);
    }
    i9100_usb_busy1 = 0;
    i9100_usb_h = 7;
    i9100_usb_t = 0;
    UDTXFSTS(1) = 1;
    i9100_usb_drain();
    CHECK(i9100_usb_t == 0 && !i9100_usb_busy1);
    /* Adopting an active loader packet must preserve its TSIZ/FIFO. */
    UGSNPSID = 0x4f54280a;
    UDCFG = 7 << 4;
    UDIEPCTL(1) = UEPCTL_ACT | UEPCTL_EPENA | (1 << 22) | 64;
    UDIEPTSIZ(1) = (1 << 19) | 42;
    i9100_usbcdc_init();
    CHECK(i9100_usb_mode == USB_ADOPT && i9100_usb_busy1);
    CHECK(usb_cfg_desc[57] == 64 && usb_cfg_desc[58] == 0);
    UDTXFSTS(1) = 384;
    i9100_usb_drain();
    CHECK(UDIEPTSIZ(1) == ((1 << 19) | 42));
    UDAINT = 2;
    UDIEPINT(1) = UEPINT_XFC;
    UGINTSTS = UGINT_IEP;
    i9100_usbcdc_poll();
    CHECK(i9100_usb_t == 7);
    CHECK(interrupts == 1);
    printf("kernel CDC: %u checks, %u failures (no hardware emulation)\n", checks, fails);
    return fails ? 1 : 0;
}
