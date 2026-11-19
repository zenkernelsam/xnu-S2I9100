/* Host-only PTE provider; production uses the XNU headers. */
#ifndef I9100_DMA_NATIVE_MODEL_PMAP
#define I9100_DMA_NATIVE_MODEL_PMAP
#include <stdint.h>
typedef uintptr_t vm_offset_t;
typedef uint32_t ppnum_t;
typedef void *pmap_t;
#ifdef __cplusplus
extern "C" {
#endif
extern pmap_t kernel_pmap;
#ifdef __cplusplus
}
#endif
#endif
