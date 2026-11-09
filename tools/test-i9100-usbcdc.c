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
static uint64_t fake_ticks;
#define UCTL_NOW() fake_ticks
#define UCTL_ACK_TICKS 12000000U
#define I9100_MCT_HZ 24000000U
static int fake_timebase_ready;
static unsigned fake_panic_seconds = 180, fake_panic_calls;
#define I9100_SIM_PANIC_SECONDS fake_panic_seconds
#define I9100_SIM_PANIC_TRIGGER() (++fake_panic_calls)
#define UCTL_TIMEBASE_READY() fake_timebase_ready
static unsigned wait_calls, reset_calls, fail_wait;
static int fake_wait(volatile unsigned int *, unsigned int, unsigned int);
#define UWAIT(r, m, v) fake_wait((r), (m), (v))
#define UPMU_WRITE(off, value) do { pmu_offsets[pmu_count] = off; pmu_values[pmu_count++] = value; } while (0)
#define UCTL_BARRIER() (++barriers)
#define UCTL_HALT() (++halts)
static int PE_parse_boot_argn(const char *s, void *p, unsigned n)
{ (void)s; (void)p; (void)n; return 0; }
/* These fixtures test the transport envelope, not private proc/IPC walks. */
static unsigned i9ctl_thread_dump(char *p, unsigned n, const char *want)
{ (void)want; const char s[] = "thread fixture\n"; if(n < sizeof(s))return 0; memcpy(p,s,sizeof(s)-1);return sizeof(s)-1; }
static unsigned i9ctl_ports_dump(char *p, unsigned n, const char *want)
{ (void)want; const char s[] = "port fixture\n"; if(n < sizeof(s))return 0; memcpy(p,s,sizeof(s)-1);return sizeof(s)-1; }
#include "../pexpert/arm/i9100_usbcdc.c"
static unsigned fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %u: %s\n", __LINE__, #c); } } while (0)

/* Inject only reset/wait outcomes. Ordinary registers remain plain memory;
 * this checks driver policy and IRQ ordering, not physical DWC2 behavior. */
static int fake_wait(volatile unsigned int *r, unsigned int mask, unsigned int value)
{
    wait_calls++;
    if(wait_calls == fail_wait)return -1;
    if(r == &UGRSTCTL && mask == UGRST_CSFT) {
        reset_calls++;
        memset(&regs[0x800/4],0,0x800);
        UGINTSTS=0;
    }
    *r=(*r & ~mask) | value;
    return 0;
}

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
    /* The hardware completion, then software W1C ACK, precede re-arm. */
    UDOEPCTL(1) &= ~UEPCTL_EPENA;
    UDOEPTSIZ(1) = 0;
    i9100_usb_e1outdone(UEPINT_XFC);
    if (uctl_started) CHECK(UDOEPTSIZ(1) == ((1u << 19) | i9100_usb_bulk_mps));
}
static void command(const char *s) { out(s, strlen(s)); }
struct root_fixture { unsigned misses, calls, states[3], state_count; int media; };
static void *root_wait(void *cookie, unsigned seconds)
{
    struct root_fixture *f=cookie;
    CHECK(seconds == 5);
    f->calls++;
    return f->calls > f->misses ? &f->media : NULL;
}
static void root_report(void *cookie, unsigned state)
{
    struct root_fixture *f=cookie;
    CHECK(f->state_count < 3);
    f->states[f->state_count++]=state;
    i9100_usbcdc_set_root_state(state);
    if(state == I9100_ROOT_MISSING) {
        CHECK(f->calls == 6);
        command("I9CTL1 00000001 root-status\n");
        const char *expected="\nI9CTL1 00000001 OK root=NO_ROOT\n";
        CHECK(uctl_len == strlen(expected) && !memcmp(uctl_reply,expected,uctl_len));
        CHECK(!uctl_action && !uctl_reset_action && !pmu_count);
        uctl_cancel();
    }
}
static void root_tests(void)
{
    const unsigned cases[]={0,2,5,6,29,30,80};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        struct root_fixture f={0}; f.misses=cases[i];
        struct i9100_root_wait_ops ops={&f,root_wait,root_report};
        uctl_cancel();uctl_last=0;uctl_reset_action=0;pmu_count=0;
        void *media=i9100_wait_for_root(&ops);
        CHECK(media == &f.media && f.calls == f.misses+1);
        CHECK(f.states[0] == I9100_ROOT_WAITING);
        CHECK(f.state_count == (f.misses >= 6 ? 3U : 2U));
        CHECK(f.states[f.state_count-1] == I9100_ROOT_MEDIA_FOUND);
        command("I9CTL1 00000002 root-status\n");
        const char *expected="\nI9CTL1 00000002 OK root=MEDIA_FOUND\n";
        CHECK(uctl_len == strlen(expected) && !memcmp(uctl_reply,expected,uctl_len));
        CHECK(!pmu_count);
    }
    i9100_usbcdc_set_root_state(99);
    CHECK(i9100_root_state == I9100_ROOT_MEDIA_FOUND);
}
static void control_tests(void)
{
    fake_timebase_ready=1;
    i9100_usb_mode=USB_OURS;
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
    CHECK(!pmu_count && uctl_reset_action == 2);
    uctl_cancel(); /* normal host close after receiving the ACK */
    fake_ticks += UCTL_ACK_TICKS - 1; uctl_reset_poll(); CHECK(!pmu_count);
    fake_ticks++; uctl_reset_poll();
    CHECK(pmu_count == 3 && pmu_offsets[0] == 0x808 && pmu_values[0] == 0x12345678);
    CHECK(pmu_offsets[1] == 0x80c && pmu_values[1] == 0x12345671);
    CHECK(pmu_offsets[2] == 0x400 && pmu_values[2] == 1 && barriers == 3 && halts == 1);
    CHECK(!interrupts);
    uctl_complete(UEPINT_XFC); uctl_reset_poll(); CHECK(pmu_count == 3);
    i9100_usb_busy1 = 0; pmu_count = 0;
    command("I9CTL1 0000000b reboot\n"); i9100_usb_drain(); uctl_complete(UEPINT_XFC);
    fake_ticks += UCTL_ACK_TICKS; uctl_reset_poll();
    CHECK(pmu_count == 3 && pmu_values[0] == 0x12345678 && pmu_values[1] == 0x12345670);
}

static void readmem_tests(void)
{
    static unsigned char fake_dram[0x40000];
    uctl_cancel(); uctl_last = 0; i9100_usb_busy1 = 0;
    i9100_usb_configured = 1; uctl_enabled = 1;
    UDTXFSTS(1) = 384; i9100_usb_h = i9100_usb_t = 0;
    for (unsigned i = 0; i < sizeof(fake_dram); i++)
        fake_dram[i] = (unsigned char)(i * 7u);
    uctl_sim_mem = fake_dram;
    command("I9CTL1 00000001 read-memory 40000010 80\n");
    CHECK(uctl_dlen == 0x80 && uctl_doff == 0);
    CHECK(strstr(uctl_reply, "OK DATA 40000010 00000080"));
    CHECK(!memcmp(uctl_data, fake_dram + 0x10, 0x80));
    /* While the header waits, a second request is rejected as busy. */
    command("I9CTL1 00000002 read-memory 40000000 10\n");
    CHECK(strstr(uctl_reply, "ERR busy") == NULL); /* reply slot busy: no new reply */
    i9100_usb_busy1 = 0; i9100_usb_drain();
    CHECK(uctl_flight && !uctl_len);          /* header packet in flight */
    uctl_complete(UEPINT_XFC); i9100_usb_busy1 = 0;
    /* Log ring must not interleave with payload chunks. */
    i9100_usb_h = 32; memset(i9100_usb_ring, 0x55, 32);
    i9100_usb_drain();
    CHECK(uctl_flight && uctl_doff == 0x80 && i9100_usb_t == 0);
    uctl_complete(UEPINT_XFC); i9100_usb_busy1 = 0;
    /* Stream finished: ring resumes. */
    i9100_usb_drain();
    CHECK(i9100_usb_t == 32 && uctl_doff == 0x80);
    /* Range/length gates. */
    i9100_usb_h = i9100_usb_t = 0;
    command("I9CTL1 00000003 read-memory 20000000 10\n");
    CHECK(strstr(uctl_reply, "ERR addr"));
    uctl_cancel();
    command("I9CTL1 00000004 read-memory 40000000 9000\n");
    CHECK(strstr(uctl_reply, "ERR len"));
    uctl_cancel();
    command("I9CTL1 00000005 read-memory 7ffffff0 40\n");
    CHECK(strstr(uctl_reply, "ERR addr"));
    uctl_cancel();
    command("I9CTL1 00000006 read-memory 40000000 0\n");
    CHECK(strstr(uctl_reply, "ERR len"));
    uctl_cancel();
    /* Busy mid-stream: header and one chunk sent, retry is rejected. */
    command("I9CTL1 00000007 read-memory 40000100 400\n");
    CHECK(uctl_dlen == 0x400);
    i9100_usb_busy1 = 0; i9100_usb_drain();
    uctl_complete(UEPINT_XFC); i9100_usb_busy1 = 0;
    i9100_usb_drain();                        /* chunk 1 in flight */
    CHECK(uctl_doff == 512);
    uctl_complete(UEPINT_XFC); i9100_usb_busy1 = 0;
    command("I9CTL1 00000008 read-memory 40000000 10\n");
    CHECK(strstr(uctl_reply, "ERR busy"));
    uctl_cancel();
}

static void usb_fixture(int ready)
{
    i9100_usb_panic_mode=i9100_usb_panic_test_enabled=0;
    i9100_usb_history_head=i9100_usb_history_used=0;
    memset(&i9100_panic_log,0,sizeof(i9100_panic_log));
    fake_panic_seconds=180;fake_panic_calls=0;
    memset(regs,0,sizeof(regs));
    memset(rx,0,sizeof(rx));rx_pos=0;
    interrupts=1;fake_timebase_ready=ready;fake_ticks=I9100_MCT_HZ;
    i9100_usb_map=i9100_phy_map=i9100_pmu_map=1;
    i9100_usb_mode=USB_OURS;i9100_usb_probed=1;i9100_usb_in=0;
    i9100_usb_configured=1;i9100_usb_hostopen=1;i9100_usb_busy1=0;
    i9100_usb_enum=0;i9100_usb_bulk_mps=512;
    i9100_usb_npoll=i9100_usb_nrxl=i9100_usb_nrxb=0;
    i9100_usb_ntx=i9100_usb_ncmd=i9100_usb_st_or=0;
    i9100_usb_reconn=i9100_usb_rearm=i9100_usb_last_rxb=0;
    i9100_usb_armed_at=i9100_usb_last_txok=i9100_usb_last_reconn=0;
    i9100_usb_disconnect_at=i9100_usb_last_rxb_at=i9100_usb_rearm_at=0;
    i9100_usb_init_attempts=i9100_usb_init_gate=0;
    i9100_usb_out_arms=i9100_usb_reset_errors=0;
    i9100_usb_h=i9100_usb_t=0;
    uctl_started=uctl_enabled=uctl_last=0;uctl_cancel();
    uctl_reset_action=0;uctl_reset_at=uctl_flight_at=0;
    wait_calls=reset_calls=fail_wait=0;pmu_count=barriers=halts=0;
    UGSNPSID=0x4f54280a;UGUSBCFG=0x540f;UGRSTCTL=UGRST_AHBIDLE;
    UDIEPCTL(1)=UEPCTL_ACT | UEPCTL_EPTYPE(2) | (1u<<22) | 512;
    UDOEPCTL(1)=UEPCTL_ACT | UEPCTL_EPTYPE(2) | 512;
    UDTXFSTS(0)=256;
}

static void init_recovery_tests(void)
{
    /* MCT first, USB maps/probe later: no lost one-shot initialization. */
    usb_fixture(1);i9100_usb_map=0;i9100_usb_probed=0;
    i9100_usbcdc_control_init();
    CHECK(!uctl_started && i9100_usb_init_attempts==1);
    CHECK(i9100_usb_init_gate==(I9100_USB_INIT_NO_MAP|I9100_USB_INIT_NO_PROBE));
    CHECK(!wait_calls && !(UDCTL & UDCTL_SFTDIS));
    i9100_usb_map=1;UDCFG=7u<<4;
    UDOEPCTL(1)=0x80088200U;UDOEPTSIZ(1)=0;
    i9100_usbcdc_poll();
    CHECK(i9100_usb_probed && uctl_started && !i9100_usb_init_gate);
    CHECK(i9100_usb_disconnect_at && i9100_usb_reconn==1);
    CHECK(!i9100_usb_configured && !uctl_len && !wait_calls);
    CHECK(UDOEPTSIZ(1)==0 && UDOEPCTL(1)==0x80088200U);
    CHECK(interrupts==1);

    /* USB first, MCT later: early console polling must remain log-only. */
    usb_fixture(0);i9100_usb_mode=USB_ADOPT;
    UDOEPCTL(1)=0x80088200U;
    i9100_usbcdc_poll();
    CHECK(!uctl_started && i9100_usb_init_gate==I9100_USB_INIT_NO_TIMEBASE);
    CHECK(!(UDCTL & UDCTL_SFTDIS) && !wait_calls);
    fake_timebase_ready=1;i9100_usbcdc_poll();
    CHECK(uctl_started && i9100_usb_reconn==1 && !uctl_len);
    uint64_t deadline=i9100_usb_disconnect_at;
    unsigned attempts=i9100_usb_init_attempts;
    /* Nothing services old completion bits during the 125ms detach. */
    UGINTSTS=UGINT_RXFLVL|UGINT_OEP;UDAINT=1u<<17;UDOEPINT(1)=UEPINT_XFC;
    fake_ticks=deadline-1;i9100_usbcdc_poll();
    CHECK(!reset_calls && !rx_pos && !i9100_usb_out_arms);
    CHECK(i9100_usb_init_attempts==attempts && (UDCTL & UDCTL_SFTDIS));
    fake_ticks=deadline;i9100_usbcdc_poll();
    CHECK(reset_calls==1 && wait_calls==2 && !i9100_usb_reset_errors);
    CHECK(!i9100_usb_disconnect_at && !(UDCTL & UDCTL_SFTDIS));
    CHECK(i9100_usb_mode==USB_OURS && !i9100_usb_configured && !i9100_usb_busy1);
    CHECK(!(UDCFG & (0x7fU<<4)) && UGAHBCFG==1);
    CHECK(UGUSBCFG==((1u<<30)|0x540f));
    CHECK(UGRXFSIZ==0x200 && UDPTXF(1)==((0x180u<<16)|0x300));
    CHECK(!uctl_len && !i9100_usb_out_arms && !pmu_count);
    /* New host setup is required; READY cannot precede SET_CONFIG. */
    setup(0,5,9,0);setup(0,9,1,0);
    CHECK(i9100_usb_configured && uctl_len && i9100_usb_out_arms==1);
    CHECK(UDOEPTSIZ(1)==((1u<<19)|512));
    CHECK(strstr(uctl_reply,"I9CTL1 READY control=0"));
    uctl_cancel();uctl_enabled=1;
    command("I9CTL1 00000001 ping\n");
    CHECK(strstr(uctl_reply,"OK control=1") && i9100_usb_ncmd==1);
    CHECK(i9100_usb_out_arms==2 && i9100_usb_reconn==1);
    /* A repeat call neither queues another greeting nor resets state. */
    unsigned queued=uctl_len;i9100_usbcdc_control_init();
    CHECK(uctl_len==queued && i9100_usb_init_attempts==attempts);

    /* Re-entry does not parse boot arguments or touch register state. */
    usb_fixture(1);i9100_usb_in=1;
    i9100_usbcdc_poll();i9100_usbcdc_control_init();
    CHECK(!uctl_started && !i9100_usb_init_attempts && !wait_calls && interrupts==1);
    i9100_usb_in=0;i9100_usbcdc_control_init();
    CHECK(uctl_started && i9100_usb_out_arms==1);

    /* Idle live budgets survive DTR, keepalive, and unrelated features. */
    uctl_cancel();unsigned budget=UDOEPTSIZ(1), arms=i9100_usb_out_arms;
    setup(0x21,0x22,1,0);
    CHECK(UDOEPTSIZ(1)==budget && i9100_usb_out_arms==arms && !i9100_usb_reconn);
    uctl_cancel();fake_ticks+=I9100_MCT_HZ*31ULL;uctl_reset_poll();
    CHECK(UDOEPTSIZ(1)==budget && i9100_usb_out_arms==arms && !i9100_usb_reconn);
    uctl_data[0]=42;uctl_doff=0;uctl_dlen=1;uctl_ready();
    CHECK(!uctl_len && uctl_dlen==1);uctl_cancel();

    /* RX OUT_DONE alone must not rewrite a still-enabled transfer. */
    UGRXSTSP=(3u<<17)|1;i9100_usb_rxlvl();
    CHECK(i9100_usb_out_arms==arms && UDOEPTSIZ(1)==budget);
    UDOEPTSIZ(1)=0;UDOEPCTL(1)&=~UEPCTL_EPENA;
    UGINTSTS=UGINT_OEP;UDAINT=1u<<17;UDOEPINT(1)=UEPINT_XFC;
    i9100_usbcdc_poll();UGINTSTS=0;
    CHECK(i9100_usb_out_arms==arms+1 && UDOEPTSIZ(1)==budget);
    CHECK(!i9100_usb_reconn);

    /* Exhaustion without completion requires a detached reset, no EPDIS. */
    UDOEPCTL(1)=0x80088200U;UDOEPTSIZ(1)=0;
    i9100_usb_e1outarm(0);
    CHECK(i9100_usb_reconn==1 && (UDCTL & UDCTL_SFTDIS));
    CHECK(UDOEPCTL(1)==0x80088200U && !wait_calls);
    CHECK(!i9100_usb_configured && !uctl_len);
    fake_ticks=i9100_usb_disconnect_at;i9100_usbcdc_poll();
    CHECK(reset_calls==1 && !i9100_usb_disconnect_at);
    i9100_usb_configured=1;UDOEPCTL(1)=0x80088200U;UDOEPTSIZ(1)=0;
    i9100_usb_e1outarm(0);
    CHECK(i9100_usb_reconn==1 && !i9100_usb_disconnect_at); /* bounded recovery */
    fake_ticks+=I9100_MCT_HZ*60ULL;i9100_usb_e1outarm(0);
    CHECK(i9100_usb_reconn==2 && i9100_usb_disconnect_at);

    /* A failed reset remains detached and stops retries at either wait. */
    for(unsigned stage=1;stage<=2;stage++) {
        usb_fixture(1);i9100_usb_mode=USB_ADOPT;i9100_usbcdc_control_init();
        fail_wait=stage;fake_ticks=i9100_usb_disconnect_at;i9100_usbcdc_poll();
        CHECK(i9100_usb_mode==USB_DEAD && i9100_usb_reset_errors==1);
        CHECK(i9100_usb_init_gate==I9100_USB_INIT_RESET_FAILED && (UDCTL & UDCTL_SFTDIS));
        unsigned waits=wait_calls;
        for(unsigned k=0;k<20;k++){fake_ticks+=I9100_MCT_HZ;i9100_usbcdc_poll();}
        CHECK(wait_calls==waits && i9100_usb_reconn==1 && !pmu_count);
    }

    /* Producer/caller share the exact appended diagnostics layout. */
    usb_fixture(1);i9100_usbcdc_control_init();
    unsigned stats[I9100_USB_STATS_WORDS+1];
    for(unsigned k=0;k<I9100_USB_STATS_WORDS+1;k++)stats[k]=0xdeadbeef;
    i9100_usbcdc_stats(stats);
    CHECK(stats[21]==1 && stats[22]==1 && !stats[23]);
    CHECK(stats[24]==1 && !stats[25] && stats[26]==1);
    for(unsigned k=0;k<I9100_USB_STATS_WORDS;k++)CHECK(stats[k]!=0xdeadbeef);
    CHECK(stats[I9100_USB_STATS_WORDS]==0xdeadbeef && interrupts==1);
    command("I9CTL1 00000001 threads\n"); /* READY owns reply: no new envelope */
    uctl_cancel();uctl_enabled=1;
    command("I9CTL1 00000002 threads\n");
    CHECK(uctl_dlen==15 && strstr(uctl_reply,"OK DATA 00000000 0000000f"));
    uctl_cancel();command("I9CTL1 00000003 ports\n");
    CHECK(uctl_dlen==13 && strstr(uctl_reply,"OK DATA 00000000 0000000d"));
}

static void panic_tests(void)
{
    usb_fixture(1);uctl_enabled=uctl_started=1;
    UDTXFSTS(1)=384;
    command("I9CTL1 00000001 panic-test\n");
    CHECK(strstr(uctl_reply,"ERR disabled") && !uctl_action);
    uctl_cancel();i9100_usb_panic_test_enabled=1;
    command("I9CTL1 00000002 panic-test\n");
    CHECK(strstr(uctl_reply,"OK panic-test") && uctl_action==3 && !fake_panic_calls);
    i9100_usb_drain();
    CHECK(uctl_flight && !fake_panic_calls);
    uctl_complete(UEPINT_XFC);
    CHECK(uctl_reset_action==3 && !fake_panic_calls);
    uctl_cancel();fake_ticks+=UCTL_ACK_TICKS-1;uctl_reset_poll();
    CHECK(!fake_panic_calls && !pmu_count);
    fake_ticks++;uctl_reset_poll();
    CHECK(fake_panic_calls==1 && !uctl_reset_action && !pmu_count);

    usb_fixture(1);uctl_enabled=uctl_started=1;
    i9100_usb_mode=USB_DEAD;
    for(unsigned n=0;n<I9100_PANIC_HISTORY_BYTES+17;n++)
        i9100_usbcdc_putc('a'+n%26);
    i9100_usb_mode=USB_OURS;
    i9100_usb_reconn=1;i9100_usb_last_reconn=fake_ticks;
    uctl_doff=1;uctl_dlen=128;uctl_used=12;i9100_usb_in=1;
    i9100_usbcdc_panic_begin(0xf103,0x80350100,0x80320000,0x80310001,
        0x80097000,0x80,0x13,180,0x1122334455667788ULL);
    CHECK(i9100_usb_panic_mode && !i9100_usb_in && !uctl_dlen && !uctl_used);
    CHECK(i9100_usb_disconnect_at && i9100_usb_reconn==2 && (UDCTL&UDCTL_SFTDIS));
    CHECK(sizeof(i9100_panic_log)-I9100_PANIC_TEXT_BYTES==64);
    CHECK(i9100_panic_log.magic==I9100_PANIC_MAGIC && i9100_panic_log.version==1);
    CHECK(i9100_panic_log.category==0xf103 && i9100_panic_log.pc==0x80320000);
    CHECK(i9100_panic_log.history_bytes==8192 && i9100_panic_log.used==8192);
    CHECK(i9100_panic_log.ticks_low==0x55667788 && i9100_panic_log.ticks_high==0x11223344);
    for(unsigned n=0;n<8192;n++) CHECK(i9100_panic_log.text[n]=='a'+(n+17)%26);
    i9100_usbcdc_putc('!');CHECK(i9100_panic_log.used==8193 && i9100_panic_log.text[8192]=='!');
    i9100_usbcdc_panic_begin(1,2,3,4,5,6,7,8,9);
    CHECK(i9100_panic_log.category==0xf103 && i9100_panic_log.used==8193);
    fake_ticks+=I9100_MCT_HZ/8;UGINTSTS=0;i9100_usbcdc_panic_poll();
    CHECK(!i9100_usb_disconnect_at && !(UDCTL&UDCTL_SFTDIS) && reset_calls==1);
    setup(0,9,1,0);uctl_cancel();
    command("I9CTL1 00000001 panic-status\n");
    CHECK(strstr(uctl_reply,"OK panic=1 seconds=000000b4"));
    uctl_cancel();fake_panic_seconds=168;
    command("I9CTL1 00000002 panic-log\n");
    CHECK(uctl_dlen==64+8193 && !uctl_doff);
    CHECK(!memcmp(uctl_data,&i9100_panic_log,uctl_dlen));
    CHECK(i9100_panic_log.elapsed_seconds==12);
    uctl_len=uctl_flight=0;command("I9CTL1 00000003 panic-log\n");
    CHECK(strstr(uctl_reply,"ERR busy") && uctl_dlen==64+8193);
    uctl_cancel();command("I9CTL1 00000004 threads SpringBoard\n");
    CHECK(strstr(uctl_reply,"ERR panic-readonly") && !uctl_dlen);
    uctl_cancel();command("I9CTL1 00000005 reboot\n");
    CHECK(strstr(uctl_reply,"ERR panic-readonly") && !uctl_action);
    uctl_cancel();command("I9CTL1 00000006 panic-test\n");
    CHECK(strstr(uctl_reply,"ERR disabled") && !uctl_action);
    uctl_cancel();uctl_enabled=0;command("I9CTL1 00000007 panic-log\n");
    CHECK(strstr(uctl_reply,"ERR disabled") && !uctl_dlen);
    uctl_cancel();uctl_enabled=1;i9100_panic_log.reset_seconds=0;fake_panic_seconds=0;
    command("I9CTL1 00000008 panic-log\n");
    CHECK(i9100_panic_log.elapsed_seconds==0);
    i9100_usbcdc_panic_disable();i9100_panic_log.used=I9100_PANIC_TEXT_BYTES-1;
    unsigned ring=i9100_usb_h;
    i9100_usbcdc_putc('X');i9100_usbcdc_putc('Y');
    CHECK(i9100_panic_log.used==I9100_PANIC_TEXT_BYTES && i9100_panic_log.dropped==1);
    CHECK(i9100_usb_h==ring && i9100_usb_mode==USB_DEAD);
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
    root_tests();
    readmem_tests();
    init_recovery_tests();
    panic_tests();
    printf("kernel CDC: %u checks, %u failures (no hardware emulation)\n", checks, fails);
    return fails ? 1 : 0;
}
