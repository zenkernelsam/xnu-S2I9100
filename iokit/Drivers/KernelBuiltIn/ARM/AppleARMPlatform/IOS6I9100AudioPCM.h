/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IOS6_I9100_AUDIO_PCM_H
#define IOS6_I9100_AUDIO_PCM_H
#include "i9100_audio_worker.h"
#include "i9100_audio_user_session.h"
struct i9100_audio_user_config {
 uint32_t sample_rate,period_frames,measured_bclk,poll_us;
 int use_irq,verified_dispatch_hook,approved_low_output;
};
#ifdef __cplusplus
class IOService;
class IOS6I9100AudioPCM;
extern "C" {
int i9100_audio_user_device_create(int enabled,IOService *platform,
 struct i9100_audio_board_native *,struct i9100_irq_registry *,
 const struct i9100_audio_user_config *,IOS6I9100AudioPCM **);
int i9100_audio_user_device_destroy(IOS6I9100AudioPCM **);
}
#endif
/* No automatic registration/boot hook. A trusted platform owner explicitly
 * supplies a verified board and clock profile, retaining them until destroy
 * succeeds. The kernel-supplied output permission defaults to zero; the
 * caller cannot lift the -30dB digital/-36dB analog cap through this ABI.
 * The actual service/user client copies bounded signed16 PCM into the real
 * workloop. One client owns a session; close/death attempts hardware stop.
 * A failed stop retains the service, worker, board lease and diagnostics,
 * rejects new clients and needs explicit owner cleanup. No retry or forced
 * free. Destroy with live clients begins detach and returns BUSY; those
 * clients may still read status/stop/close but cannot restart the device.
 */
#endif
