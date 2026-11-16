/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_SENSORS_H
#define I9100_SENSORS_H
#include "i9100_peripheral_io.h"
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_sensor_kind {I9100_SENSOR_K3DH=1,I9100_SENSOR_K3G=2,I9100_SENSOR_AK8975=3};
enum i9100_sensor_error {I9100_SENSOR_ARGUMENT=-480,I9100_SENSOR_DISABLED=-481,
 I9100_SENSOR_BUSY=-482,I9100_SENSOR_IO=-483,I9100_SENSOR_ID=-484,
 I9100_SENSOR_NOT_READY=-485,I9100_SENSOR_OVERFLOW=-486,I9100_SENSOR_TIMEOUT=-487};
struct i9100_sensor_config {
 enum i9100_sensor_kind kind;
 int8_t axis[3][3]; /* signed permutation: board->iOS coordinates, explicitly reviewed */
 int32_t offset[3]; /* micro-g, micro-degrees/s or nano-tesla, after axis rotation */
};
struct i9100_sensor_sample {
 uint64_t timestamp_ns;
 uint32_t sequence,kind,status;
 int32_t xyz[3]; /* micro-g (K3DH), micro-degrees/s (K3G), nano-tesla (AK8975) */
};
struct i9100_sensor {
 struct i9100_peripheral_i2c io;
 struct i9100_sensor_config config;
 uint64_t started,last_sample;
 uint32_t initialized,active,fault,converting,sequence,overflows;
 uint8_t address,id,saved[5],asa[3];
};
int i9100_sensor_init(struct i9100_sensor *,const struct i9100_peripheral_i2c *,const struct i9100_sensor_config *);
int i9100_sensor_enable(struct i9100_sensor *,int enabled);
int i9100_sensor_begin(struct i9100_sensor *); /* single-shot AK8975, no implicit retrigger */
int i9100_sensor_read(struct i9100_sensor *,struct i9100_sensor_sample *);
/* Exact identity and data-ready checks; no address scan, made-up samples or
 * guessed orientation. K3DH: 100Hz,12-bit,2g,BDU. K3G:lowest ODR,250dps,BDU.
 * AK8975 includes factory ASA adjustment, reads ST2 to release the latch and
 * rejects overflow/error. Samples are committed only after complete I/O.
 * Stable ST control fields are read back before admission and after restore;
 * AK8975 powerdown waits100us and reads the mode before marking inactive.
 * An ACK with unchanged controls leaves an error/fault for explicit cleanup.
 * Polling diagnostic module, not yet an iOS CoreMotion/HID service. */
#ifdef __cplusplus
}
#endif
#endif
