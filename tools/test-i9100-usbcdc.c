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
static uint32_t regs[0x1000], rx[128];
static unsigned rx_pos;
static boolean_t interrupts = 1;
static boolean_t ml_set_interrupts_enabled(boolean_t state)
{ boolean_t old = interrupts; interrupts = state; return old; }
static vm_offset_t ml_io_map(vm_offset_t p, unsigned n)
{ (void)p; (void)n; return 1; }
#define UREG(off) regs[(off) / 4]
#define URXFIFO() rx[rx_pos++]
static unsigned pmu_offsets[4], pmu_values[4], pmu_count, barriers, halts;
#define UPMU_WRITE(off, value) do { pmu_offsets[pmu_count] = off; pmu_values[pmu_count++] = value; } while (0)
#define UCTL_BARRIER() (++barriers)
#define UCTL_HALT() (++halts)
static int PE_parse_boot_argn(const char *s, void *p, unsigned n)
{ (void)s; (void)p; (void)n; return 0; }
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

static void out(const char *s, unsigned n)
{
    memset(rx, 0, sizeof(rx)); memcpy(rx, s, n); rx_pos = 0;
    UGRXSTSP = (2 << 17) | (n << 4) | 1;
    i9100_usb_rxlvl();
    CHECK(rx_pos == (n + 3) / 4);
    UGRXSTSP = (3 << 17) | 1;
    i9100_usb_rxlvl();
    if (uctl_started) CHECK(UDOEPTSIZ(1) == ((1u << 19) | i9100_usb_bulk_mps));
}
static void command(const char *s) { out(s, strlen(s)); }
static void control_tests(void)
{
    uctl_cancel(); uctl_last = 0; i9100_usb_busy1 = 0;
    uctl_ready(); CHECK(!uctl_len);
    command("I9CTL1 00000001 reboot-download\n");
    CHECK(!uctl_len && !uctl_action && !uctl_last);
    i9100_usb_h = i9100_usb_t = 0;
    UDOEPCTL(1) = UEPCTL_ACT | UEPCTL_EPTYPE(2) | i9100_usb_bulk_mps;
    i9100_usbcdc_control_init();
    CHECK(uctl_started && (UDOEPCTL(1) & UEPCTL_EPENA));
    CHECK(UDOEPTSIZ(1) == ((1u << 19) | i9100_usb_bulk_mps));
    uctl_ready();
    CHECK(uctl_len < 64);
    CHECK(!memcmp(uctl_reply, "\nI9CTL1 READY control=0\nI9CTL1 BUILD 0000000000000000\n", uctl_len));
    uctl_cancel();
    i9100_usb_h = i9100_usb_t = 0;
    i9100_pmu_map = 1; uctl_enabled = 0;
    command("I9CTL1 00000001 reboot-download\n");
    CHECK(uctl_action == 0 && strstr(uctl_reply, "ERR disabled"));
    uctl_cancel(); uctl_enabled = 1;
    command("I9CTL1 00000002 pi"); CHECK(!uctl_len);
    command("ng\n"); CHECK(uctl_len && !uctl_action);
    /* Logs cannot overwrite or starve the response. */
    i9100_usb_h = UTXRING - 1;
    i9100_usb_drain(); CHECK(uctl_flight && i9100_usb_t == 0);
    command("I9CTL1 00000003 reboot-download\n"); CHECK(!uctl_action);
    uctl_complete(UEPINT_XFC); CHECK(!pmu_count);
    i9100_usb_busy1 = 0;
    command("I9CTL1 00000002 reboot-download\n");
    CHECK(!uctl_action && strstr(uctl_reply, "ERR sequence"));
    uctl_cancel();
    command("I9CTL1 00000004 write-memory\n"); CHECK(!uctl_action);
    uctl_cancel();
    char flood[512]; memset(flood, 'x', sizeof(flood));
    out(flood, sizeof(flood)); command("I9CTL1 00000005 reboot-download\n");
    CHECK(!uctl_len && !uctl_action);
    const char binary[] = "I9CTL1 00000006 reboot\0-download\n";
    out(binary, sizeof(binary) - 1); CHECK(!uctl_action && !uctl_len);
    command("I9CTL1 00000007 reboot-download\r\n"); CHECK(!uctl_action);
    command("I9CTL1 00000008 reboot-download\n");
    CHECK(uctl_action == 2 && !pmu_count);
    uctl_complete(UEPINT_XFC); CHECK(!pmu_count); /* queued is not sent */
    i9100_usb_drain(); CHECK(uctl_flight && !pmu_count);
    uctl_complete(UEPINT_AHBERR | UEPINT_XFC); CHECK(!pmu_count && !uctl_action);
    i9100_usb_busy1 = 0;
    command("I9CTL1 00000009 reboot-download\n"); i9100_usb_drain();
    UGINTSTS = UGINT_USBRST | UGINT_IEP; UDAINT = 2; UDIEPINT(1) = UEPINT_XFC;
    i9100_usb_poll(); CHECK(!pmu_count && !uctl_action);
    i9100_usb_configured = 1; i9100_usb_h = i9100_usb_t = 0;
    command("I9CTL1 0000000a reboot-download\n"); i9100_usb_drain();
    uctl_complete(UEPINT_XFC);
    CHECK(pmu_count == 2 && pmu_offsets[0] == 0x808 && pmu_values[0] == 0x12345678);
    CHECK(pmu_offsets[1] == 0x400 && pmu_values[1] == 1 && barriers == 3 && halts == 1);
    CHECK(!interrupts);
    uctl_complete(UEPINT_XFC); CHECK(pmu_count == 2); /* no duplicate reset */
    i9100_usb_busy1 = 0; pmu_count = 0;
    command("I9CTL1 0000000b reboot\n"); i9100_usb_drain(); uctl_complete(UEPINT_XFC);
    CHECK(pmu_count == 2 && pmu_values[0] == 0);
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
    control_tests();
    printf("kernel CDC: %u checks, %u failures (no hardware emulation)\n", checks, fails);
    return fails ? 1 : 0;
}
