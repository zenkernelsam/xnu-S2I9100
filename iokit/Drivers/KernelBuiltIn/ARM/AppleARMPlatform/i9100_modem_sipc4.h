/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_MODEM_SIPC4_H
#define I9100_MODEM_SIPC4_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum i9100_sipc_format {I9100_SIPC_FMT=1,I9100_SIPC_RAW=2,I9100_SIPC_RFS=3};
enum i9100_sipc_error {I9100_SIPC_ARGUMENT=-920,I9100_SIPC_DISABLED=-921,I9100_SIPC_FRAME=-922,
 I9100_SIPC_OVERFLOW=-923,I9100_SIPC_FRAGMENT=-924,I9100_SIPC_IDENTITY=-925};
struct i9100_sipc_frame {const uint8_t *bytes;uint32_t length;uint8_t format,channel,control,command;};
struct i9100_sipc4 {
 uint32_t initialized,enabled,fault,format,version,aligned,used,needed,header;
 uint32_t fragment_bytes,fragment_target,fragment_active;
 uint8_t fragment_id,frame[4100],fragments[16384];
};
int i9100_sipc4_init(struct i9100_sipc4 *,enum i9100_sipc_format,unsigned verified_version,int aligned);
int i9100_sipc4_enable(struct i9100_sipc4 *,int);
int i9100_sipc4_feed(struct i9100_sipc4 *,const uint8_t *,unsigned length,unsigned *consumed,struct i9100_sipc_frame *);
int i9100_sipc4_encode(enum i9100_sipc_format,unsigned channel,unsigned control,const uint8_t *payload,unsigned length,int aligned,uint8_t *out,unsigned capacity,unsigned *used);
struct i9100_xmm_pipe {uint8_t control_interface,data_interface,in_endpoint,out_endpoint;uint16_t max_packet;};
struct i9100_xmm_profile {uint16_t vid,pid;uint8_t configuration;struct i9100_xmm_pipe pipes[3];};
int i9100_xmm6260_profile(const uint8_t *device,unsigned device_bytes,const uint8_t *configuration,unsigned bytes,struct i9100_xmm_profile *);
/* EUR U1 is XMM6260/HSIC, not regional DPRAM. These are passive framing and
 * USB-descriptor primitives, with no modem power/reset/boot/RFS write API.
 * FMT/RAW/RFS are length-delimited (payload7f/7e is legal); optional4B padding.
 * One in-flight fragmented FMT message is supported; interleaved fragmented
 * IDs explicitly fail instead of corrupting messages. Parser is single-owner,
 * heap/static storage (20KiB), never an IRQ-stack object. Returned view lasts
 * until the next feed. USB boot/downloader058b:0041 and maintenance CDC are
 * rejected. HSIC/EHCI/PHY/CP boot and iOS telephony/RIL are separate stages. */
#ifdef __cplusplus
}
#endif
#endif
