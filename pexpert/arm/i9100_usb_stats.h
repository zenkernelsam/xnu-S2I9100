#ifndef I9100_USB_STATS_H
#define I9100_USB_STATS_H
/* Append-only diagnostics; both the producer and the POWER caller use this. */
#define I9100_USB_STATS_WORDS 27U
/* ig= is the last failed initialization gate, or zero after success. */
#define I9100_USB_INIT_NO_MAP 1U
#define I9100_USB_INIT_NO_PROBE 2U
#define I9100_USB_INIT_DEAD 4U
#define I9100_USB_INIT_NO_TIMEBASE 8U
#define I9100_USB_INIT_RESET_FAILED 16U
#ifdef __cplusplus
extern "C" {
#endif
void i9100_usbcdc_stats(unsigned *out);
#ifdef __cplusplus
}
#endif
#endif
