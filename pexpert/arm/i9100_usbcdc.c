/* i9100 Exynos4210 USB CDC debug console (kernel side).
 * Continuation of uniLoader's dwc2cdc4210 gadget: the DWC2 OTG device
 * at 0x12480000 is adopted for early logs. Once the MCT is ready, a
 * detached reset gives maintenance a fresh host/endpoint session.
 * Polled slave mode, serviced by putc and the existing MCT tick. No
 * dedicated USB IRQ or DMA; no logging/allocation in the poll path. */
#ifndef I9100_USB_SIM
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include "i9100_timer.h"
#endif
#include <string.h>
#include "i9100_root_wait.h"
#include "i9100_usb_stats.h"
#include "i9100_panic_log.h"

#if defined(BOARD_CONFIG_EXYNOS4210_I9100) && !defined(I9100_USB_SIM)
/* Thread/proc walk lives in kern_exit.c where MACH_KERNEL_PRIVATE struct
 * visibility already exists; keeps private headers out of pexpert units. */
extern unsigned i9ctl_thread_dump(char *buf, unsigned cap, const char *want);
extern unsigned i9ctl_ports_dump(char *buf, unsigned cap, const char *want);
extern unsigned i9100_hid_swipe(unsigned x0,unsigned y0,unsigned x1,unsigned y1,unsigned ms);
extern unsigned i9100_hid_home(unsigned ms);
#endif

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)

void i9100_usbcdc_putc(char c);
void i9100_usbcdc_setmap(void);
void i9100_usbcdc_init(void);
void i9100_usbcdc_poll(void);
static void i9100_usb_spin(unsigned int n);
void i9100_usbcdc_control_init(void);
static void i9100_usb_control_start(void);

#define I9100_DWC2_PA	0x12480000U
#define I9100_PHY_PA	0x125B0000U
#define I9100_PMU_PA	0x10020000U
#define I9100_PMU_USBPHY	0x704U

static vm_offset_t i9100_usb_map;
static vm_offset_t i9100_phy_map;
static vm_offset_t i9100_pmu_map;

#ifndef UREG
#define UREG(x) (*(volatile unsigned int *) \
		 (i9100_usb_map ? i9100_usb_map + (x) : I9100_DWC2_PA + (x)))
#endif
#define UGOTGCTL	UREG(0x000)
#define UGAHBCFG	UREG(0x008)
#define UGUSBCFG	UREG(0x00C)
#define UGRSTCTL	UREG(0x010)
#define UGINTSTS	UREG(0x014)
#define UGINTMSK	UREG(0x018)
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
#ifndef URXFIFO
#define URXFIFO()	UFIFO(0)
#endif
#define UPCGCCTL	UREG(0xE00)
#define UPHYREG(x)	(*(volatile unsigned int *) \
		 (i9100_phy_map ? i9100_phy_map + (x) : I9100_PHY_PA + (x)))

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
/* Liveness forensics for the "CDC dies silently a few minutes in" wedge.
 * Cheap counters bumped in interrupt-context code only; read out from
 * the POWER workloop print so the death signature stays on screen. */
static volatile unsigned i9100_usb_npoll, i9100_usb_nrxl, i9100_usb_nrxb,
	i9100_usb_ntx, i9100_usb_ncmd, i9100_usb_st_or, i9100_usb_reconn,
	i9100_usb_rearm, i9100_usb_last_rxb;
static uint64_t i9100_usb_armed_at, i9100_usb_last_txok,
	i9100_usb_last_reconn, i9100_usb_disconnect_at, i9100_usb_last_rxb_at,
	i9100_usb_rearm_at;
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
static char i9100_usb_history[I9100_PANIC_HISTORY_BYTES];
static unsigned i9100_usb_history_head, i9100_usb_history_used;
static struct I9100PanicLog i9100_panic_log;
static int i9100_usb_panic_mode, i9100_usb_panic_test_enabled;
#ifdef I9100_USB_SIM
#define UCTL_RAW_MASK_GET(mask) do { (mask) = ml_set_interrupts_enabled(FALSE); } while (0)
#define UCTL_RAW_MASK_SET(mask) (void)ml_set_interrupts_enabled(mask)
#else
#define UCTL_RAW_MASK_GET(mask) \
    __asm__ volatile("mrs %0,cpsr\n\tcpsid if" : "=r"(mask) :: "memory")
#define UCTL_RAW_MASK_SET(mask) \
    __asm__ volatile("msr cpsr_c,%0" :: "r"(mask) : "memory")
#endif
#ifndef I9100_USB_SIM
extern unsigned ios7i9100_panic_seconds_remaining(void);
extern void ios7i9100_recovery_test_panic(void) __attribute__((noreturn));
#else
#ifndef I9100_SIM_PANIC_SECONDS
#define I9100_SIM_PANIC_SECONDS 180U
#define I9100_SIM_PANIC_TRIGGER() ((void)0)
#endif
static unsigned ios7i9100_panic_seconds_remaining(void) { return I9100_SIM_PANIC_SECONDS; }
static void ios7i9100_recovery_test_panic(void) { I9100_SIM_PANIC_TRIGGER(); }
#endif

/* One request at a time. Separate response storage cannot be starved by logs.
 * Sequence numbers increase within a USB session; this is a local maintenance
 * protocol, not authentication. No allocator, printf or arbitrary MMIO verbs. */
static char uctl_line[256], uctl_reply[64];
/* read-memory staging: one snapshot buffer, drained in 64-byte EP1 packets
 * ahead of the log ring so frame text cannot interleave with payload. */
static unsigned char uctl_data[32768];
static unsigned int uctl_doff, uctl_dlen;
static char uctl_build[17] = "0000000000000000";
static unsigned int uctl_used, uctl_len, uctl_last;
static int uctl_drop, uctl_flight, uctl_action, uctl_enabled, uctl_started;
static unsigned int i9100_usb_init_attempts, i9100_usb_init_gate;
static unsigned int i9100_usb_out_arms, i9100_usb_reset_errors;
#ifndef UCTL_TIMEBASE_READY
#define UCTL_TIMEBASE_READY() I9100_timebase_initialized()
#endif
#ifndef UWAIT
#define UWAIT(r, m, v) i9100_usb_wait((r), (m), (v))
#endif
static int uctl_reset_action;
static uint64_t uctl_reset_at;
static unsigned int i9100_usb_bulk_mps = 512;
/* Lost EP1-IN completion would wedge uctl_flight forever: while a reply is
 * marked in-flight every incoming command byte is dropped, so the channel
 * looks dead until the next USB reset. Timestamp the flight so a poll
 * timeout can recover. */
static uint64_t uctl_flight_at;
/* Atomic word snapshots on the single-core bring-up target; IRQ-side queries
 * never acquire filesystem locks or allocate memory. */
static volatile unsigned int i9100_root_state = I9100_ROOT_BOOTING;
void i9100_usbcdc_set_root_state(unsigned int state)
{
	if (state <= I9100_ROOT_MEDIA_FOUND) i9100_root_state = state;
}
static const char *uctl_root_status(void)
{
	switch (i9100_root_state) {
	case I9100_ROOT_WAITING: return "OK root=WAIT_ROOT";
	case I9100_ROOT_MISSING: return "OK root=NO_ROOT";
	case I9100_ROOT_MEDIA_FOUND: return "OK root=MEDIA_FOUND";
	default: return "OK root=BOOTING";
	}
}
#ifndef UCTL_NOW
#define UCTL_NOW() (i9100_usb_panic_mode ? I9100_panic_ticks() : I9100_get_timebase())
#define UCTL_ACK_TICKS (I9100_MCT_HZ / 2U)
#endif

#ifndef UPMU_WRITE
#define UPMU_WRITE(off, value) \
	(*(volatile unsigned int *)(i9100_pmu_map + (off)) = (value))
#define UCTL_BARRIER() __asm__ __volatile__("dsb sy" ::: "memory")
#define UCTL_HALT() do { for (;;) __asm__ __volatile__("wfi"); } while (0)
#endif

static void uctl_cancel(void)
{
	uctl_used = uctl_len = 0;
	uctl_doff = uctl_dlen = 0;
	uctl_drop = uctl_flight = uctl_action = 0;
}

/* Parse up to 8 hex digits; advances *pp past them. Returns 0 on empty. */
static unsigned int uctl_hex(const char **pp)
{
	unsigned int v = 0, n = 0, c;
	while (n < 8) {
		c = (unsigned char)**pp;
		if (c >= '0' && c <= '9') c -= '0';
		else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
		else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
		else break;
		v = (v << 4) | c; n++; (*pp)++;
	}
	return n ? v : 0;
}

static void uctl_hex8(char *out, unsigned int v)
{
	unsigned int i, c;
	for (i = 0; i < 8; i++) {
		c = (v >> (28 - i * 4)) & 0xf;
		out[i] = c < 10 ? '0' + c : 'a' + c - 10;
	}
}

#ifdef I9100_USB_SIM
static const unsigned char *uctl_sim_mem; /* test supplies a DRAM stand-in */
#define UCTL_PREAD(pa, dst, n) \
	memcpy(dst, uctl_sim_mem + ((pa) - 0x40000000U), n)
#else
/* Direct map via the platform physmap: VA = PA - gPhysBase + gVirtBase. */
#define UCTL_PREAD(pa, dst, n) \
	memcpy(dst, (const void *)phystokv(pa), n)
#endif

static void uctl_ready(void)
{
	if (!uctl_started || uctl_reset_action || !i9100_usb_configured ||
	    i9100_usb_disconnect_at || uctl_doff < uctl_dlen) return;
	const char *s = uctl_enabled ? "\nI9CTL1 READY control=1\n" :
		"\nI9CTL1 READY control=0\n";
	if (!uctl_len && !uctl_flight) {
		memcpy(uctl_reply, s, strlen(s));
		uctl_len = strlen(s);
		memcpy(uctl_reply + uctl_len, "I9CTL1 BUILD ", 13);
		memcpy(uctl_reply + uctl_len + 13, uctl_build, 16);
		uctl_reply[uctl_len + 29] = '\n';
		uctl_len += 30;
	}
}

static void uctl_command(void)
{
	i9100_usb_ncmd++;
	unsigned int i, seq = 0, n;
	const char *answer, *verb;
	if (uctl_len || uctl_flight || uctl_used < 18 ||
	    memcmp(uctl_line, "I9CTL1 ", 7) || uctl_line[15] != ' ')
		return;
	for (i = 7; i < 15; i++) {
		unsigned int c = (unsigned char)uctl_line[i], digit;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else return;
		seq = (seq << 4) | digit;
	}
	uctl_line[uctl_used] = 0;
	verb = uctl_line + 16;
	uctl_action = 0;
	if (!seq || seq <= uctl_last) answer = "ERR sequence";
	else {
		uctl_last = seq;
        if (!strcmp(verb, "ping") || !strcmp(verb, "status"))
			answer = uctl_enabled ? "OK control=1" : "OK control=0";
        else if (!strcmp(verb, "panic-status")) {
            static char status[40];
            memcpy(status, "OK panic=", 9); status[9] = i9100_usb_panic_mode ? '1' : '0';
            memcpy(status+10, " seconds=", 9);
            unsigned seconds = i9100_usb_panic_mode ? ios7i9100_panic_seconds_remaining() : 0;
            uctl_hex8(status+19, seconds); status[27] = 0;
            answer = status;
        } else if (!strcmp(verb, "panic-log")) {
            if (!i9100_usb_panic_mode) answer = "ERR no-panic";
            else if (!uctl_enabled) answer = "ERR disabled";
            else if (uctl_doff < uctl_dlen) answer = "ERR busy";
            else {
                static char hdr[26];
                unsigned used = i9100_panic_log.used;
                if (used > I9100_PANIC_TEXT_BYTES) used = I9100_PANIC_TEXT_BYTES;
                unsigned remaining = ios7i9100_panic_seconds_remaining();
                i9100_panic_log.elapsed_seconds =
                    remaining <= i9100_panic_log.reset_seconds ?
                    i9100_panic_log.reset_seconds - remaining : 0;
                unsigned size = 64U+used;
                memcpy(uctl_data, &i9100_panic_log, size);
                uctl_doff = 0; uctl_dlen = size;
                memcpy(hdr, "OK DATA ", 8); uctl_hex8(hdr+8, 0);
                hdr[16] = ' '; uctl_hex8(hdr+17, size); hdr[25] = 0;
                answer = hdr;
            }
        } else if (!strcmp(verb, "panic-test")) {
            if (!uctl_enabled || !i9100_usb_panic_test_enabled || i9100_usb_panic_mode)
                answer = "ERR disabled";
            else { uctl_action = 3; answer = "OK panic-test"; }
        } else if (i9100_usb_panic_mode && strcmp(verb, "reboot-download") &&
            strcmp(verb, "root-status"))
            answer = "ERR panic-readonly";
		else if (!strcmp(verb, "root-status"))
			answer = uctl_root_status();
		else if (!strcmp(verb, "reboot") || !strcmp(verb, "reboot-download")) {
			if (!uctl_enabled || !i9100_pmu_map) answer = "ERR disabled";
			else {
				uctl_action = !strcmp(verb, "reboot-download") ? 2 : 1;
				answer = uctl_action == 2 ? "OK reboot-download" : "OK reboot";
			}
		} else if (!strncmp(verb, "hid-home ", 9)) {
            const char *p=verb+9,*start=p;unsigned ms=uctl_hex(&p);
            if(!uctl_enabled)answer="ERR disabled";
            else if(uctl_doff<uctl_dlen)answer="ERR busy";
            else if(p==start || p-start>4 || *p)answer="ERR input-args";
            else {
#ifdef I9100_USB_SIM
                unsigned result=0;
#else
                unsigned result=i9100_hid_home(ms);
#endif
                answer=result==1?"OK input-queued":result==2?"ERR input-args":
                    result==3?"ERR busy":result==4?"ERR input-not-ready":"ERR disabled";
            }
		} else if (!strncmp(verb, "hid-swipe ", 10)) {
            /* Only publish a bounded RAM mailbox here. The HID workloop
             * builds events later; no IOKit locks/queues from the USB IRQ. */
            unsigned a[5], valid=1;
            const char *p=verb+10;
            for(unsigned n=0;n<5;n++){
                const char *start=p;a[n]=uctl_hex(&p);
                if(p==start || p-start>4){valid=0;break;}
                if(n<4){if(*p!=' '){valid=0;break;}p++;}
                else if(*p)valid=0;
            }
            if(!uctl_enabled)answer="ERR disabled";
            else if(uctl_doff<uctl_dlen)answer="ERR busy";
            else if(!valid)answer="ERR input-args";
            else {
#ifdef I9100_USB_SIM
                unsigned result=0;
#else
                unsigned result=i9100_hid_swipe(a[0],a[1],a[2],a[3],a[4]);
#endif
                answer=result==1?"OK input-queued":result==2?"ERR input-args":
                    result==3?"ERR busy":result==4?"ERR input-not-ready":"ERR disabled";
            }
		} else if (!strncmp(verb, "read-memory ", 12)) {
			/* read-memory <pa> <len>: snapshot DRAM [0x40000000,0x80000000)
			 * into uctl_data, then stream raw bytes after the DATA header.
			 * PA range only: no MMIO side effects, direct map always valid. */
			const char *p = verb + 12;
			unsigned int addr = uctl_hex(&p), len;
			while (*p == ' ') p++;
			len = uctl_hex(&p);
			if (!uctl_enabled) answer = "ERR disabled";
			else if (uctl_doff < uctl_dlen) answer = "ERR busy";
			else if (!len || len > sizeof(uctl_data)) answer = "ERR len";
			else if (addr < 0x40000000U || addr + len > 0x80000000U ||
			         addr + len < addr) answer = "ERR addr";
			else {
				static char hdr[26];
				UCTL_PREAD(addr, uctl_data, len);
				uctl_doff = 0; uctl_dlen = len;
				memcpy(hdr, "OK DATA ", 8);
				uctl_hex8(hdr + 8, addr);
				hdr[16] = ' ';
				uctl_hex8(hdr + 17, len);
				hdr[25] = '\0';
				answer = hdr;
			}
		} else if (!strncmp(verb, "threads", 7)) {
			/* threads [comm-substr]: forensic proc/thread snapshot streamed
			 * as text via the DATA path. Polled context; torn snapshots ok. */
			if (!uctl_enabled) answer = "ERR disabled";
			else if (uctl_doff < uctl_dlen) answer = "ERR busy";
			else {
				const char *want = verb + 7;
				while (*want == ' ') want++;
				unsigned used = i9ctl_thread_dump((char *)uctl_data,
				    sizeof(uctl_data), want);
				static char hdr[26];
				uctl_doff = 0; uctl_dlen = used;
				memcpy(hdr, "OK DATA ", 8);
				uctl_hex8(hdr + 8, 0);
				hdr[16] = ' ';
				uctl_hex8(hdr + 17, used);
				hdr[25] = '\0';
				answer = hdr;
			}
		} else if (!strncmp(verb, "ports", 5)) {
			/* ports [comm-substr]: IPC space dump - resolves the port
			 * name a threads-line rnam is waiting on into object,
			 * rights and receiver task. */
			if (!uctl_enabled) answer = "ERR disabled";
			else if (uctl_doff < uctl_dlen) answer = "ERR busy";
			else {
				const char *want = verb + 5;
				while (*want == ' ') want++;
				unsigned used = i9ctl_ports_dump((char *)uctl_data,
				    sizeof(uctl_data), want);
				static char hdr[26];
				uctl_doff = 0; uctl_dlen = used;
				memcpy(hdr, "OK DATA ", 8);
				uctl_hex8(hdr + 8, 0);
				hdr[16] = ' ';
				uctl_hex8(hdr + 17, used);
				hdr[25] = '\0';
				answer = hdr;
			}
		} else answer = "ERR verb";
	}
	uctl_reply[0] = '\n';
	memcpy(uctl_reply + 1, uctl_line, 16);
	n = strlen(answer);
	memcpy(uctl_reply + 17, answer, n);
	uctl_reply[17 + n] = '\n';
	uctl_len = 18 + n;
}

static void i9100_usb_e1outarm(int completed);

static void uctl_byte(unsigned char c)
{
	if (!uctl_started || uctl_reset_action) return;
	if (c == '\n') {
		if (!uctl_drop) uctl_command();
		uctl_used = 0;
		uctl_drop = 0;
	} else if (c < 32 || c > 126 || uctl_used == sizeof(uctl_line) - 1 ||
	           uctl_len || uctl_flight) {
		uctl_drop = 1;
	} else if (!uctl_drop) uctl_line[uctl_used++] = c;
}

static void uctl_complete(unsigned int status)
{
	int action = uctl_action;
	if (status & (UEPINT_AHBERR | (1u << 1) | (1u << 3))) {
		uctl_cancel();
		return;
	}
	if (!(status & UEPINT_XFC) || !uctl_flight) return;
	uctl_flight = uctl_action = 0;
	if (!action || !uctl_enabled || !i9100_pmu_map) return;
	/* USB ACK is earlier than a synchronous Windows ReadFile completion.
	 * Commit once, then allow the 250 ms reader to collect it before reset.
	 * DTR close/USB teardown after the ACK must not cancel this committed action. */
	uctl_reset_action = action;
	uctl_reset_at = UCTL_NOW() + UCTL_ACK_TICKS;
}

static int i9100_usb_wait(volatile unsigned int *r, unsigned int m,
			  unsigned int v);
static int i9100_usb_reset_controller(void);

static void i9100_usb_begin_reconnect(void)
{
	if (i9100_usb_disconnect_at || uctl_reset_action || i9100_usb_reset_errors ||
	    !UCTL_TIMEBASE_READY()) return;
	/* One immediate boot recovery, then at most one detach per minute. */
    if (!i9100_usb_panic_mode && i9100_usb_reconn && UCTL_NOW() - i9100_usb_last_reconn <
	    (uint64_t)I9100_MCT_HZ * 60U) return;
	i9100_usb_configured = i9100_usb_hostopen = 0;
	uctl_cancel();
	UDCTL |= UDCTL_SFTDIS;
	i9100_usb_last_reconn = UCTL_NOW();
	i9100_usb_reconn++;
	i9100_usb_disconnect_at = UCTL_NOW() + (uint64_t)I9100_MCT_HZ / 8U;
}

static void uctl_reset_poll(void)
{
	int action = uctl_reset_action;
	/* Phase 2 of a host-dead reconnect: hold the soft disconnect ~125ms so
	 * the host actually observes the detach (a ~3ms pulse left a zombie
	 * session where the core dropped state but the host never noticed). */
	if (i9100_usb_disconnect_at) {
		if (UCTL_NOW() < i9100_usb_disconnect_at)
			return;
		/* CSRST while detached clears inherited endpoint/FIFO state. Never
		 * flush the live RX FIFO or rewrite a still-enabled exhausted OUT. */
		if (!i9100_usb_reset_controller()) {
			i9100_usb_disconnect_at = 0;
			i9100_usb_reset_errors++;
			i9100_usb_init_gate = I9100_USB_INIT_RESET_FAILED;
			i9100_usb_mode = USB_DEAD; /* stay detached, no retry loop */
			return;
		}
		UDCTL &= ~UDCTL_SFTDIS;
		i9100_usb_disconnect_at = 0;
	}
	if (uctl_flight && UCTL_NOW() - uctl_flight_at > UCTL_ACK_TICKS * 4U) {
		/* The armed EP1-IN packet whose completion vanished holds stale
		 * FIFO data. Flush EP1 TX FIFO and clear busy so the drain can
		 * re-arm and command input reopens. Staged data offsets already
		 * advanced; a truncated stream fails host-side and is retried. */
		UWAIT(&UGRSTCTL, UGRST_AHBIDLE, UGRST_AHBIDLE);
		UGRSTCTL = UGRST_TXFNUM(1) | UGRST_TXFFLSH;
		UWAIT(&UGRSTCTL, UGRST_TXFFLSH, 0);
		i9100_usb_busy1 = 0;
		uctl_flight = uctl_action = 0;
	}
	/* Orphan armed packet: busy1 set without a flight owner means a packet
	 * was armed for a host that never collects (selective suspend/dropped
	 * pipe). The uctl_flight watchdog above cannot see this state. Flush
	 * the stale FIFO contents after 2s so console TX un-wedges. */
	if (i9100_usb_busy1 && !uctl_flight && i9100_usb_armed_at &&
	    UCTL_NOW() - i9100_usb_armed_at > (uint64_t)I9100_MCT_HZ * 2U) {
		UWAIT(&UGRSTCTL, UGRST_AHBIDLE, UGRST_AHBIDLE);
		UGRSTCTL = UGRST_TXFNUM(1) | UGRST_TXFFLSH;
		UWAIT(&UGRSTCTL, UGRST_TXFFLSH, 0);
		i9100_usb_busy1 = 0;
	}
	/* Idle valid OUT budgets are preserved. Exhausted arms without a
	 * consumed completion are recovered through detached re-enumeration. */
	if (uctl_started && i9100_usb_configured &&
	    i9100_usb_hostopen && !i9100_usb_disconnect_at) {
		if (i9100_usb_nrxb != i9100_usb_last_rxb) {
			i9100_usb_last_rxb = i9100_usb_nrxb;
			i9100_usb_last_rxb_at = UCTL_NOW();
		} else if (i9100_usb_last_rxb_at &&
		    UCTL_NOW() - i9100_usb_last_rxb_at >
		        (uint64_t)I9100_MCT_HZ * 10U &&
		    UCTL_NOW() - i9100_usb_rearm_at >
		        (uint64_t)I9100_MCT_HZ * 30U) {
			i9100_usb_e1outarm(0);
			i9100_usb_rearm_at = UCTL_NOW();
			i9100_usb_rearm++;
		}
	}
	/* Host-dead reconnect: a control reply or staged DATA has waited 30s
	 * with zero EP1-IN completions - the host stopped servicing pipes.
	 * Pulse soft-disconnect (125ms, re-armed in phase 2 above); the host
	 * sees unplug+replug and re-enumerates fresh endpoints. Conversational
	 * data only: idle console chatter must not flap the link, and the 60s
	 * rate-limit bounds recovery attempts for a genuinely absent host. */
	{
		int pending = uctl_len || uctl_doff < uctl_dlen;
		if (pending && uctl_started && i9100_usb_configured &&
		    i9100_usb_hostopen && !i9100_usb_disconnect_at &&
		    UCTL_NOW() - i9100_usb_last_txok >
		        (uint64_t)I9100_MCT_HZ * 30U &&
		    UCTL_NOW() - i9100_usb_last_reconn >
		        (uint64_t)I9100_MCT_HZ * 60U) {
			i9100_usb_begin_reconnect();
		}
	}
    if (!action || UCTL_NOW() < uctl_reset_at) return;
	uctl_reset_action = 0;
    if (action == 3) {
        ios7i9100_recovery_test_panic();
        return;
    }
	/* S-Boot check_reboot reads INFORM3, verifies 0x12345670 in its high
	 * bits and consumes the low nibble: 1 requests download, 0 normal.
	 * INFORM2 alone only supplied the reboot marker. Never clear kernel RAM. */
	(void)ml_set_interrupts_enabled(FALSE);
	UCTL_BARRIER();
	UPMU_WRITE(0x808, 0x12345678);
	UPMU_WRITE(0x80c, action == 2 ? 0x12345671 : 0x12345670);
	UCTL_BARRIER();
	UPMU_WRITE(0x400, 1);
	UCTL_BARRIER();
	UCTL_HALT();
}

static const unsigned char usb_dev_desc[] = {
	18, 1, 0, 2, 2, 0, 0, 64, 0x25, 0x05, 0xa7, 0xa4, 0x01, 0,
	1, 2, 3, 1 };
static unsigned char usb_cfg_desc[] = {
	9, 2, 67, 0, 2, 1, 0, 0x80, 0xfa,
	9, 4, 0, 0, 1, 2, 2, 1, 0,
	5, 0x24, 0, 0x10, 0x01,
	5, 0x24, 1, 0, 1,
	4, 0x24, 2, 0x02,
	5, 0x24, 6, 0, 1,
	7, 5, 0x82, 3, 8, 0, 16,
	9, 4, 1, 0, 2, 10, 0, 0, 0,
	7, 5, 0x81, 2, 0, 2, 0,
	7, 5, 0x01, 2, 0, 2, 0 };
/* shared with the reset-poll keepalive above; initialised at devinit */
static const unsigned char usb_str0[] = { 4, 3, 9, 4 };
static const unsigned char usb_str1[] = {
	32, 3, 'i',0,'9',0,'1',0,'0',0,'0',0,' ',0,'u',0,'n',0,'i',0,
	'L',0,'o',0,'a',0,'d',0,'e',0,'r',0 };
static const unsigned char usb_str2[] = {
	24, 3, 'X',0,'N',0,'U',0,' ',0,'C',0,'D',0,'C',0,' ',0,'L',0,
	'O',0,'G',0 };
static const unsigned char usb_str3[] = {
	24, 3, 'S',0,'2',0,'I',0,'9',0,'1',0,'0',0,'0',0,'-',0,
	'O',0,'S',0,'6',0 };
static const unsigned char usb_qual_desc[] = {
	10, 6, 0, 2, 2, 0, 0, 64, 1, 0 };

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
    unsigned int t = i9100_usb_panic_mode ? 1000U : 100000U;
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
	UGNPTXFSIZ = (0x100 << 16) | 0x200;
	UDPTXF(1) = (0x180 << 16) | 0x300;
	/* Reset-time flushing previously broke this core's live RX path.
	 * Initial CSRST already clears it; USBRST needs no FIFO flush. */
}

static void i9100_usb_e0arm(void)
{
	UDOEPTSIZ(0) = (1 << 29) | (1 << 19) | 8;
	UDOEPCTL(0) = UEPCTL_ACT | UEPCTL_EPENA | UEPCTL_CNAK;
}

static void i9100_usb_e1outarm(int completed)
{
	unsigned int ctl;
	if (!uctl_started || uctl_reset_action || !i9100_usb_configured ||
	    i9100_usb_disconnect_at) return;
	ctl = UDOEPCTL(1);
	if ((ctl & UEPCTL_EPDIS) || ((ctl & UEPCTL_EPENA) && !completed &&
	    !(UDOEPTSIZ(1) & (0x3ffU << 19)))) {
		i9100_usb_begin_reconnect();
		return;
	}
	if ((ctl & UEPCTL_EPENA) && !completed) {
		/* Already has a live budget: only release NAK, do not abort it. */
		if (!(ctl & UEPCTL_STALL)) UDOEPCTL(1) = (ctl & ~UEPCTL_SNAK) | UEPCTL_CNAK;
		return;
	}
	UDOEPTSIZ(1) = (1u << 19) | i9100_usb_bulk_mps;
	UDOEPCTL(1) = (ctl & ~(UEPCTL_SNAK | UEPCTL_STALL)) |
		UEPCTL_EPENA | UEPCTL_CNAK;
	/* Every (re-)arm starts a fresh RX-silence window; without this the
	 * keepalive can never fire for a link that never saw one byte. */
	i9100_usb_last_rxb_at = UCTL_NOW();
	i9100_usb_out_arms++;
}

static void i9100_usb_e1outdone(unsigned int status)
{
	if (status & (UEPINT_AHBERR | (1u << 1)))
		i9100_usb_begin_reconnect();
	else if (status & UEPINT_XFC)
		i9100_usb_e1outarm(1);
}

static void i9100_usb_devinit(void)
{
	UDCFG = (1 << 18) | 0;
	UDIEPMSK = 0x1d;
	UDOEPMSK = 0x0d;
	UDAINTMSK = (1 << 17) | (1 << 16) | (1 << 1) | 1;
	i9100_usb_fifos();
	UDIEPINT(0) = UDIEPINT(1) = 0x3ff;
	UDOEPINT(0) = UDOEPINT(1) = 0x3ff;
	UDIEPCTL(0) = UEPCTL_ACT;
	UGINTMSK = 0x800c3810U;
	i9100_usb_e0st = UE0_WAIT;
	i9100_usb_e0len = 0;
	i9100_usb_busy1 = 0;
	i9100_usb_configured = 0;
	i9100_usb_hostopen = 0;
	uctl_cancel();
	uctl_last = 0;
	i9100_usb_last_txok = UCTL_NOW();
	i9100_usb_e0arm();
}

/* Reset only while detached. Preserve the working PHY/GUSBCFG settings;
 * switch to the known slave-mode device configuration without a PHY reset. */
static int i9100_usb_reset_controller(void)
{
	unsigned int usb_config = UGUSBCFG;
	if (!(UDCTL & UDCTL_SFTDIS)) return 0;
	if (UWAIT(&UGRSTCTL, UGRST_AHBIDLE, UGRST_AHBIDLE)) return 0;
	UGRSTCTL = UGRST_CSFT;
	if (UWAIT(&UGRSTCTL, UGRST_CSFT, 0)) {
		UDCTL |= UDCTL_SFTDIS;
		return 0;
	}
	UDCTL |= UDCTL_SFTDIS;
	UPCGCCTL = 0;
	UGAHBCFG = 1; /* global IRQ enabled; DMA disabled */
	UGUSBCFG = (usb_config & ~(1u << 29)) | (1u << 30);
	i9100_usb_mode = USB_OURS;
	i9100_usb_enum = 0;
	i9100_usb_newaddr = 0;
	i9100_usb_bulk_mps = 512;
	usb_cfg_desc[57] = usb_cfg_desc[64] = 0;
	usb_cfg_desc[58] = usb_cfg_desc[65] = 2;
	i9100_usb_devinit();
	return 1;
}

static void i9100_usb_pmu_set(unsigned int off, unsigned int bits)
{
	if (!i9100_pmu_map)
		return;
	*(volatile unsigned int *)(i9100_pmu_map + off) |= bits;
}

static void i9100_usb_hardinit(void)
{
	i9100_usb_pmu_set(I9100_PMU_USBPHY, 1);
	UPHYREG(0) = 0x1f80;
	UPHYREG(4) = 3;
	UPHYREG(8) = 1;
	i9100_usb_spin(20000);
	UPHYREG(8) = 0;
	i9100_usb_spin(20000);
	UPHYREG(0x24) = 0x001819bb;

	UWAIT(&UGRSTCTL, UGRST_AHBIDLE, UGRST_AHBIDLE);
	UGRSTCTL = UGRST_CSFT;
	UWAIT(&UGRSTCTL, UGRST_CSFT, 0);
	UGAHBCFG = 1;
	UGUSBCFG = (1 << 30) | 0x540f;
	i9100_usb_devinit();
	UDCTL |= UDCTL_SFTDIS;
	i9100_usb_spin(10000);
	UDCTL &= ~UDCTL_SFTDIS;
	i9100_usb_mode = USB_OURS;
}

static int i9100_usb_e0tx(unsigned int len)
{
	unsigned int words = (len + 3) >> 2, i;
	unsigned int w;

	if ((UDTXFSTS(0) & 0xffff) < words)
		return -1;
	UDIEPTSIZ(0) = (1 << 19) | len;
	UDIEPCTL(0) = UEPCTL_ACT | UEPCTL_EPENA | UEPCTL_CNAK;
	for (i = 0; i < words; i++) {
		unsigned int j;
		w = 0;
		for (j = 0; j < 4 && i * 4 + j < len; j++)
			w |= (unsigned int)i9100_usb_e0buf[i * 4 + j] << (j * 8);
		UFIFO(0) = w;
	}
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
	if (i9100_usb_disconnect_at) return;
	i9100_usb_e0st = UE0_SIN;
	UDIEPTSIZ(0) = 1 << 19;
	UDIEPCTL(0) = UEPCTL_ACT | UEPCTL_EPENA | UEPCTL_CNAK;
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
			case 1: i9100_usb_e0reply(usb_str1, sizeof(usb_str1), wl); return;
			case 2: i9100_usb_e0reply(usb_str2, sizeof(usb_str2), wl); return;
			default: i9100_usb_e0reply(usb_str3, sizeof(usb_str3), wl); return;
			}
		case 6:
			i9100_usb_e0reply(usb_qual_desc, sizeof(usb_qual_desc), wl);
			return;
		case 7: {
			static unsigned char other[sizeof(usb_cfg_desc)];
			memcpy(other, usb_cfg_desc, sizeof(other));
			other[1] = 7;
			unsigned int mps = i9100_usb_bulk_mps == 512 ? 64 : 512;
			other[57] = other[64] = mps & 255;
			other[58] = other[65] = mps >> 8;
			i9100_usb_e0reply(other, sizeof(other), wl);
			return;
		}
		}
		i9100_usb_e0stall();
		return;
	}
	if (rt == 0x00 && rq == 5) {
		UDCFG = (UDCFG & ~0x7f0U) | ((wv & 0x7f) << 4);
		i9100_usb_newaddr = 0;
		i9100_usb_e0sin();
		return;
	}
	if (rt == 0x00 && rq == 9) {
		uctl_cancel();
		if (wv != 1) {
			i9100_usb_configured = 0;
			UDOEPCTL(1) |= UEPCTL_SNAK;
			i9100_usb_e0sin();
			return;
		}
		UDIEPCTL(1) = UEPCTL_ACT | UEPCTL_EPTYPE(2) | (1 << 22) |
			UEPCTL_MPS(i9100_usb_bulk_mps) |
			UEPCTL_SD0;
		UDIEPCTL(2) = UEPCTL_ACT | UEPCTL_EPTYPE(3) | (2 << 22) |
			UEPCTL_MPS(8) | UEPCTL_SNAK;
		UDOEPCTL(1) = UEPCTL_ACT | UEPCTL_EPTYPE(2) |
			UEPCTL_MPS(i9100_usb_bulk_mps) |
			UEPCTL_SD0;
		i9100_usb_configured = 1;
		i9100_usb_e1outarm(0);
		uctl_ready();
		i9100_usb_e0sin();
		return;
	}
	if (rt == 0x21 && rq == 0x20) {
		uctl_ready();
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
		if (!(wv & 1)) uctl_cancel();
		/* Every COM open toggles DTR - cheap moment to guarantee the
		 * command endpoint has a fresh transfer budget. */
		i9100_usb_e1outarm(0);
		uctl_ready();
		i9100_usb_e0sin();
		return;
	}
	if (rq == 1 || rq == 3) {
		/* CLEAR_FEATURE/SET_FEATURE aimed at EP1-OUT: usbser issues a
		 * pipe reset (or a raw CLEAR_FEATURE(ENDPOINT_HALT)) when its
		 * write pipe jams. Re-arming the endpoint here gives host-side
		 * recovery a working revive that needs no cable replug and no
		 * admin rights. */
		if (rt == 0x02 && rq == 1 && wv == 0 &&
		    i9100_usb_setup[4] == 1 && i9100_usb_setup[5] == 0)
			i9100_usb_e1outarm(0);
		i9100_usb_e0sin();
	} else if (rt & 0x80)
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
	unsigned int i, ep = gs & 0xf;

	i9100_usb_nrxl++;

	if (ps == 6) {
		/* GRXSTSP pops STATUS. Payload always comes from DFIFO0. */
		unsigned int lo = URXFIFO(), hi = URXFIFO();
		memcpy(i9100_usb_setup, &lo, 4);
		memcpy(i9100_usb_setup + 4, &hi, 4);
		UDIEPINT(0) = 0x3ff; /* a new SETUP aborts the previous control transfer */
		i9100_usb_e0setup();
	} else if (ps == 2) {
		i9100_usb_nrxb += bc;
		for (i = 0; i < (bc + 3) / 4; i++) {
			unsigned int word = URXFIFO(), j;
			if (ep == 1 && i9100_usb_configured)
				for (j = 0; j < 4 && i * 4 + j < bc; j++)
					uctl_byte((word >> (j * 8)) & 255);
		}
		if (ep == 0 && i9100_usb_e0st == UE0_DOUT)
			i9100_usb_e0sin();
	} else if (ps == 3) {
		/* OUT_DONE in the RX queue is not the endpoint interrupt ACK.
		 * DOEPINT.XFRC below owns re-arming, after payload consumption. */
		if (ep == 0 && i9100_usb_e0st == UE0_SOUT)
			i9100_usb_e0st = UE0_WAIT;
	} else {
		for (i = 0; i < (bc + 3) / 4; i++)
			(void)URXFIFO();
	}
	if (ep == 0 && !i9100_usb_disconnect_at)
		i9100_usb_e0arm();
}

static void i9100_usb_drain(void)
{
	unsigned char pkt[512] = { 0 };
	unsigned int n = 0, i, w, space, pkts;

	if (i9100_usb_busy1 || !i9100_usb_configured ||
	    (!uctl_len && uctl_doff >= uctl_dlen &&
	     i9100_usb_h == i9100_usb_t))
		return;
	space = UDTXFSTS(1) & 0xffff;
	if (!space)
		return;
	n = uctl_len ? uctl_len : uctl_doff < uctl_dlen ?
	    uctl_dlen - uctl_doff : (i9100_usb_h - i9100_usb_t) & (UTXRING - 1);
	if (n > sizeof(pkt)) n = sizeof(pkt);
	if (space < (n + 3) / 4) return;
	if (uctl_len) {
		UDIEPINT(1) = 0x3ff; /* clear stale completions only for control replies */
		memcpy(pkt, uctl_reply, n);
		uctl_len = 0;
		uctl_flight = 1;
		uctl_flight_at = UCTL_NOW();
	} else if (uctl_doff < uctl_dlen) {
		memcpy(pkt, uctl_data + uctl_doff, n);
		uctl_doff += n;
		uctl_flight = 1;
		uctl_flight_at = UCTL_NOW();
	} else for (i = 0; i < n; i++) {
		pkt[i] = i9100_usb_ring[i9100_usb_t];
		i9100_usb_t = (i9100_usb_t + 1) & (UTXRING - 1);
	}
	pkts = (n + i9100_usb_bulk_mps - 1) / i9100_usb_bulk_mps;
	UDIEPTSIZ(1) = (pkts << 19) | (1u << 29) | n;
	UDIEPCTL(1) = (UDIEPCTL(1) & ~(UEPCTL_EPDIS | UEPCTL_SNAK |
		UEPCTL_STALL)) | UEPCTL_EPENA | UEPCTL_CNAK;
	for (i = 0; i < (n + 3) / 4; i++) {
		w = (unsigned int)pkt[i * 4] | ((unsigned int)pkt[i * 4 + 1] << 8) |
		    ((unsigned int)pkt[i * 4 + 2] << 16) |
		    ((unsigned int)pkt[i * 4 + 3] << 24);
		UFIFO(1) = w;
	}
	i9100_usb_ntx += n;
	i9100_usb_armed_at = UCTL_NOW();
	i9100_usb_busy1 = 1;
}

static void i9100_usb_poll(void)
{
	unsigned int st;

	if (!i9100_usb_probed)
		return;
	i9100_usb_npoll++;
	uctl_reset_poll();
	if (i9100_usb_disconnect_at || i9100_usb_mode == USB_DEAD) return;
	st = UGINTSTS;
	i9100_usb_st_or |= st;
	if (st & UGINT_USBRST) {
		UGINTSTS = UGINT_USBRST;
		i9100_usb_newaddr = 0;
		i9100_usb_mode = USB_OURS;
		i9100_usb_devinit();
		if (uctl_started) return; /* remaining bits belong to the previous USB session */
	}
	if (uctl_started && (st & ((1u << 29) | (1u << 11)))) {
		UGINTSTS = st & ((1u << 29) | (1u << 11));
		uctl_cancel();
		return;
	}
	if (st & UGINT_ENUM) {
		UGINTSTS = UGINT_ENUM;
		i9100_usb_enum = 1;
		i9100_usb_bulk_mps = ((UDSTS >> 1) & 3) == 0 ? 512 : 64;
		usb_cfg_desc[57] = usb_cfg_desc[64] = i9100_usb_bulk_mps & 255;
		usb_cfg_desc[58] = usb_cfg_desc[65] = i9100_usb_bulk_mps >> 8;
		UDIEPCTL(0) = UEPCTL_ACT;
		i9100_usb_e0arm();
	}
	if (st & UGINT_RXFLVL) {
		UGINTSTS = UGINT_RXFLVL;
		i9100_usb_rxlvl();
		if (i9100_usb_disconnect_at) return;
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
			uctl_complete(e);
			if ((e & (UEPINT_XFC | UEPINT_AHBERR)) ||
			    (uctl_started && (e & ((1u << 1) | (1u << 3))))) {
				i9100_usb_busy1 = 0;
				i9100_usb_last_txok = UCTL_NOW();
			}
		}
	}
	if (st & UGINT_OEP) {
		unsigned int d = (UDAINT >> 16) & 0xffff;
		if (d & 1) UDOEPINT(0) = UDOEPINT(0);
		if (d & 2) {
			unsigned int e = UDOEPINT(1);
			UDOEPINT(1) = e;
			i9100_usb_e1outdone(e);
			if (i9100_usb_disconnect_at) return;
		}
	}
	i9100_usb_drain();
}

/* Called exactly once before any fatal formatter. Preserve the recent normal
 * log first; discard any half-sent control envelope so a host can reconnect to
 * the same CDC identity and fetch a self-contained panic-log snapshot. */
void i9100_usbcdc_panic_begin(unsigned category, unsigned callsite,
    unsigned pc, unsigned lr, unsigned far, unsigned fsr, unsigned cpsr,
    unsigned seconds, uint64_t ticks)
{
    unsigned mask;
    UCTL_RAW_MASK_GET(mask);
    if (!i9100_usb_panic_mode) {
        memset(&i9100_panic_log, 0, sizeof(i9100_panic_log));
        i9100_panic_log.magic = I9100_PANIC_MAGIC;
        i9100_panic_log.version = 1;
        i9100_panic_log.category = category; i9100_panic_log.callsite = callsite;
        i9100_panic_log.pc = pc; i9100_panic_log.lr = lr;
        i9100_panic_log.far = far; i9100_panic_log.fsr = fsr;
        i9100_panic_log.cpsr = cpsr; i9100_panic_log.reset_seconds = seconds;
        i9100_panic_log.ticks_low = (unsigned)ticks;
        i9100_panic_log.ticks_high = (unsigned)(ticks >> 32);
        for (unsigned n = 0; n < i9100_usb_history_used; n++)
            i9100_panic_log.text[n] = i9100_usb_history[
                (i9100_usb_history_head+I9100_PANIC_HISTORY_BYTES-
                    i9100_usb_history_used+n) & (I9100_PANIC_HISTORY_BYTES-1U)];
        i9100_panic_log.history_bytes = i9100_usb_history_used;
        i9100_panic_log.used = i9100_usb_history_used;
        i9100_usb_panic_mode = 1;
        uctl_cancel();
        i9100_usb_in = 0;
        if (i9100_usb_map && i9100_usb_probed && i9100_usb_mode != USB_DEAD)
            i9100_usb_begin_reconnect();
    }
    UCTL_RAW_MASK_SET(mask);
}

/* CPU-local panic loop: raw mask only, no scheduler/preemption API. */
void i9100_usbcdc_panic_disable(void)
{
    i9100_usb_mode = USB_DEAD;
    i9100_usb_in = 0;
}
void i9100_usbcdc_panic_poll(void)
{
    if (!i9100_usb_panic_mode || !i9100_usb_map || !i9100_usb_probed ||
        i9100_usb_mode == USB_DEAD) return;
    if (!i9100_usb_in) {
        i9100_usb_in = 1;
        i9100_usb_poll();
        i9100_usb_in = 0;
    }
}

void i9100_usbcdc_poll(void)
{
    if (i9100_usb_panic_mode) { i9100_usbcdc_panic_poll(); return; }
	boolean_t enabled;
	enabled = ml_set_interrupts_enabled(FALSE);
	if (!i9100_usb_in) {
		i9100_usb_in = 1;
		/* Retry after either boot order, within the same non-reentrant
		 * critical section as the register/FIFO polling. */
		if (i9100_usb_map && !i9100_usb_probed) i9100_usbcdc_init();
		if (!uctl_started) i9100_usb_control_start();
		if (i9100_usb_map && i9100_usb_probed && i9100_usb_mode != USB_DEAD)
			i9100_usb_poll();
		i9100_usb_in = 0;
	}
	ml_set_interrupts_enabled(enabled);
}

void i9100_usbcdc_putc(char c)
{
	unsigned int nx;
	boolean_t enabled;

    unsigned history_mask;
    UCTL_RAW_MASK_GET(history_mask);
    if (i9100_usb_panic_mode) {
        if (i9100_panic_log.used < I9100_PANIC_TEXT_BYTES)
            i9100_panic_log.text[i9100_panic_log.used++] = c;
        else i9100_panic_log.dropped++;
    } else {
        i9100_usb_history[i9100_usb_history_head] = c;
        i9100_usb_history_head = (i9100_usb_history_head+1U) & (I9100_PANIC_HISTORY_BYTES-1U);
        if (i9100_usb_history_used < I9100_PANIC_HISTORY_BYTES) i9100_usb_history_used++;
    }
    UCTL_RAW_MASK_SET(history_mask);
    /* Fatal paths never lazily probe/map registers. */
    if (i9100_usb_panic_mode) {
        if (i9100_usb_mode != USB_DEAD) {
            nx = (i9100_usb_h+1U) & (UTXRING-1U);
            if (nx != i9100_usb_t) {
                i9100_usb_ring[i9100_usb_h] = c; i9100_usb_h = nx;
            }
            i9100_usbcdc_panic_poll();
        }
        return;
    }
    /* No MMIO until i9100_usbcdc_setmap() gave us a real kernel VA:
	 * the early identity map does not cover 0x12480000.  Chars still
	 * land in the ring and flush out once USB init runs. */
	if (!i9100_usb_map)
		goto ring_only;
	if (!i9100_usb_probed)
		i9100_usbcdc_init();
	if (i9100_usb_mode == USB_DEAD)
		return;
ring_only:
	enabled = ml_set_interrupts_enabled(FALSE);
	nx = (i9100_usb_h + 1) & (UTXRING - 1);
	if (nx != i9100_usb_t) {
		i9100_usb_ring[i9100_usb_h] = c;
		i9100_usb_h = nx;
	}
	i9100_usbcdc_poll();
	ml_set_interrupts_enabled(enabled);
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
	i9100_phy_map = ml_io_map(I9100_PHY_PA, 0x1000);
	i9100_pmu_map = ml_io_map(I9100_PMU_PA, 0x4000);
	i9100_usbcdc_init();
	{
		static const char msg[] = "\n[i9100-usbcdc] kernel CDC up, mode ";
		const char *p = msg;

		while (*p)
			i9100_usbcdc_putc(*p++);
		i9100_usbcdc_putc(i9100_usb_mode == USB_ADOPT ? 'A' :
				  i9100_usb_mode == USB_OURS ? 'H' : 'D');
		i9100_usbcdc_putc('\n');
	}
}

/* USB maps before the framebuffer console. Keep that proven early path
 * log-only; enable maintenance only after the MCT timebase is running. */
static void i9100_usb_control_start(void)
{
	unsigned int gate = 0;
	if (uctl_started) return;
	i9100_usb_init_attempts++;
	if (!i9100_usb_map) gate |= I9100_USB_INIT_NO_MAP;
	if (!i9100_usb_probed) gate |= I9100_USB_INIT_NO_PROBE;
	if (i9100_usb_mode == USB_DEAD) gate |= I9100_USB_INIT_DEAD;
	if (!UCTL_TIMEBASE_READY()) gate |= I9100_USB_INIT_NO_TIMEBASE;
	i9100_usb_init_gate = gate;
	if (gate) return;
	(void)PE_parse_boot_argn("i9100control", &uctl_enabled, sizeof(uctl_enabled));
	uctl_enabled = uctl_enabled == 1;
    (void)PE_parse_boot_argn("i9100panictest", &i9100_usb_panic_test_enabled,
        sizeof(i9100_usb_panic_test_enabled));
    i9100_usb_panic_test_enabled = i9100_usb_panic_test_enabled == 1;
	/* An underscore forces a string even when all 16 hash digits are numeric. */
	(void)PE_parse_boot_argn("_i9100build", uctl_build, sizeof(uctl_build));
	uctl_started = 1;
	UDOEPMSK |= 0x0d;
	/* The inherited address does not establish RX budget/PID/completion
	 * ownership. One detached reset forces a fresh SET_CONFIG instead. */
	if (i9100_usb_mode == USB_ADOPT)
		i9100_usb_begin_reconnect();
	else
		i9100_usb_e1outarm(0);
	uctl_ready();
}

void i9100_usbcdc_control_init(void)
{
	boolean_t enabled = ml_set_interrupts_enabled(FALSE);
	if (!i9100_usb_in) {
		i9100_usb_in = 1;
		i9100_usb_control_start();
		i9100_usb_in = 0;
	}
	ml_set_interrupts_enabled(enabled);
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
		/* Adopt an in-flight loader packet. Wait for its XFRC before
		 * writing TSIZ or FIFO; the loader's software busy flag is gone. */
		i9100_usb_busy1 = !!(ctl1 & UEPCTL_EPENA);
		i9100_usb_bulk_mps = ctl1 & 0x7ff;
		usb_cfg_desc[57] = usb_cfg_desc[64] = i9100_usb_bulk_mps & 255;
		usb_cfg_desc[58] = usb_cfg_desc[65] = i9100_usb_bulk_mps >> 8;
		UGINTMSK = 0x800c3810U;
		UDAINTMSK |= (1 << 17) | (1 << 16) | (1 << 1) | 1;
	} else {
		i9100_usb_hardinit();
	}
	i9100_usb_probed = 1;
}

/* On-screen post-mortem for CDC wedge: POWER tick prints these so the last
 * line before death shows whether polling ran (p), whether the dwc2 raised
 * any interrupt bits (st), whether host bytes arrived (rx) and whether our
 * TX armed (tx), plus the live stuck-flags. */
void i9100_usbcdc_stats(unsigned *o)
{
	boolean_t enabled = ml_set_interrupts_enabled(FALSE);
	unsigned g = i9100_usb_map ? UGINTSTS : 0;
	o[0] = i9100_usb_mode;
	o[1] = i9100_usb_in;
	o[2] = i9100_usb_npoll;
	o[3] = i9100_usb_st_or;
	o[4] = i9100_usb_nrxl;
	o[5] = i9100_usb_nrxb;
	o[6] = i9100_usb_ntx;
	o[7] = i9100_usb_busy1;
	o[8] = i9100_usb_configured;
	o[9] = i9100_usb_hostopen;
	o[10] = uctl_flight;
	o[11] = uctl_len;
	o[12] = i9100_usb_ncmd;
	o[13] = g;
	o[14] = i9100_usb_map ? UDAINT : 0;
	o[15] = i9100_usb_map ? UDIEPCTL(1) : 0;
	o[16] = i9100_usb_reconn;
	o[17] = UCTL_TIMEBASE_READY() ? (unsigned)((UCTL_NOW() -
	    i9100_usb_last_txok) / I9100_MCT_HZ) : 0;
	o[18] = i9100_usb_map ? UDOEPCTL(1) : 0;
	o[19] = i9100_usb_map ? UDOEPTSIZ(1) : 0;
	o[20] = i9100_usb_rearm;
	o[21] = uctl_started;
	o[22] = i9100_usb_init_attempts;
	o[23] = i9100_usb_init_gate;
	o[24] = i9100_usb_out_arms;
	o[25] = i9100_usb_reset_errors;
	o[26] = UCTL_TIMEBASE_READY();
	ml_set_interrupts_enabled(enabled);
}

#endif /* BOARD_CONFIG_EXYNOS4210_I9100 */
