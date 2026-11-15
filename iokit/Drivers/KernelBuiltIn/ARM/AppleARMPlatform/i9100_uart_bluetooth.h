/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_UART_BLUETOOTH_H
#define I9100_UART_BLUETOOTH_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_uart_fraction {I9100_UART_RAW_FRACTION=1,I9100_UART_SLOT_PATTERN=2};
enum i9100_uart_error {I9100_UART_ARGUMENT=-960,I9100_UART_DISABLED=-961,I9100_UART_BUSY=-962,
 I9100_UART_IO=-963,I9100_UART_TIMEOUT=-964,I9100_UART_RX_ERROR=-965};
struct i9100_uart_io {
 void *cookie;uint32_t (*read32)(void *,unsigned);uint8_t (*read8)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);void (*write8)(void *,unsigned,uint8_t);
 int (*lock)(void *);void (*unlock)(void *);uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t);
};
struct i9100_uart_bluetooth {
 struct i9100_uart_io io;uint32_t saved[7],initialized,enabled,prepared,fault;
 uint32_t clock_hz,baud,actual_baud,fraction_mode,rx_errors,tx_bytes;
};
int i9100_uart_bt_init(struct i9100_uart_bluetooth *,const struct i9100_uart_io *);
int i9100_uart_bt_enable(struct i9100_uart_bluetooth *,int);
int i9100_uart_bt_configure(struct i9100_uart_bluetooth *,uint32_t verified_clock_hz,uint32_t baud,enum i9100_uart_fraction,int gpio_clock_fresh_peer_verified);
int i9100_uart_bt_write(struct i9100_uart_bluetooth *,const uint8_t *,unsigned bytes,unsigned *sent);
int i9100_uart_bt_read(struct i9100_uart_bluetooth *,uint8_t *,unsigned capacity,unsigned *received);
int i9100_uart_bt_stop(struct i9100_uart_bluetooth *);
/* UART0@13800000 only, 8N1/RTS-CTS, PIO, independently owned/masked IRQ.
 * Input clock and fractional-register variant are explicitly verified; this
 * module never infers them from baud or changes CMU/GPIO/BT reset pins.
 * A partial TX or RX line error quarantines the transport. Stop never claims
 * the remote H4 framing was resynchronized; fresh peer reset/generation is
 * still required by the H4 layer before reuse. No firmware download here. */
#ifdef __cplusplus
}
#endif
#endif
