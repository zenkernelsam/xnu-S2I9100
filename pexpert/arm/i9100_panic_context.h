#ifndef I9100_PANIC_CONTEXT_H
#define I9100_PANIC_CONTEXT_H
/* PE cannot include the private scheduler declarations in misc_protos.h.
 * trap.c statically verifies this read-only mirror against its actual
 * abort_information_context_t before a kernel can be linked. */
struct I9100PanicContext {
    uint32_t r[13], sp, lr, pc, cpsr, fsr, far;
};
#endif
