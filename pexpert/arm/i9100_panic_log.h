#ifndef I9100_PANIC_LOG_H
#define I9100_PANIC_LOG_H
#define I9100_PANIC_TEXT_BYTES 28672U
#define I9100_PANIC_HISTORY_BYTES 8192U
#define I9100_PANIC_MAGIC 0x39504c47U
/* Exactly 64-byte header followed by text. Metadata and text stay in wired
 * BSS until Download reboot; flash is never touched by a fatal handler. */
struct I9100PanicLog {
    unsigned magic, version, used, dropped, history_bytes, category, callsite;
    unsigned pc, lr, far, fsr, cpsr, reset_seconds, elapsed_seconds;
    unsigned ticks_low, ticks_high;
    char text[I9100_PANIC_TEXT_BYTES];
};
#endif
