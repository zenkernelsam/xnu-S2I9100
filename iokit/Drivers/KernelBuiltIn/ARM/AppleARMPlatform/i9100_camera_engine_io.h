/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_CAMERA_ENGINE_IO_H
#define I9100_CAMERA_ENGINE_IO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_camera_engine_error {
 I9100_CE_ARGUMENT=-880, I9100_CE_DISABLED=-881, I9100_CE_BUSY=-882,
 I9100_CE_IO=-883, I9100_CE_OWNER=-884, I9100_CE_TIMEOUT=-885,
 I9100_CE_FRAME=-886
};
struct i9100_camera_engine_io {
 void *cookie;
 uint32_t (*read32)(void *,unsigned);
 void (*write32)(void *,unsigned,uint32_t);
 int (*lock)(void *);
 void (*unlock)(void *);
 void (*barrier)(void *);
 uint64_t (*now_ns)(void *);
 int (*delay_us)(void *,uint32_t);
};
struct i9100_camera_register {
 unsigned offset;
 uint32_t mask,saved,last,attempted,dirty,uncertain;
};
int i9100_camera_reg_save(const struct i9100_camera_engine_io *,struct i9100_camera_register *);
int i9100_camera_reg_set(const struct i9100_camera_engine_io *,struct i9100_camera_register *,uint32_t);
int i9100_camera_reg_restore(const struct i9100_camera_engine_io *,struct i9100_camera_register *);
/* Thread-context outer serialization covers each sequence. No blocking lock
 * in an IRQ handler. Only saved/last/attempted owned fields may be changed;
 * a lost write keeps its cleanup record, and foreign fields stay untouched.
 * A read of all ones is an IO failure, never an idle/identity proof. */
#ifdef __cplusplus
}
#endif
#endif
