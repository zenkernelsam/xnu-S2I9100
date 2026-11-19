/* Host-only Mach notification layout; not used by the ARM/XNU compiler. */
#ifndef I9100_MODEL_MACH_MESSAGE_H
#define I9100_MODEL_MACH_MESSAGE_H
#include <stdint.h>
typedef uint32_t mach_port_t;
typedef int kern_return_t;
typedef struct {uint32_t msgh_bits,msgh_size;mach_port_t msgh_remote_port,msgh_local_port;
 uint32_t msgh_reserved;int32_t msgh_id;} mach_msg_header_t;
enum {MACH_PORT_NULL=0,MACH_MSG_TYPE_COPY_SEND=19,MACH_MSG_SUCCESS=0,MACH_SEND_TIMED_OUT=0x10000004};
#define MACH_MSGH_BITS(remote,local) ((remote)|((local)<<8))
#ifdef __cplusplus
extern "C" {
#endif
kern_return_t mach_msg_send_from_kernel_proper(mach_msg_header_t *,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
