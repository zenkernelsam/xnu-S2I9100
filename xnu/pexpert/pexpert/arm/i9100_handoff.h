/* I9100 iOS7 handoff ABI (Exynos 4210, Galaxy S2 GT-I9100).
 * Same 128-byte layout as Leo/J1 handoff for upper-layer reuse, but i9100
 * constants and validation. No runtime or hardware success is implied.
 * Every word is little-endian uint32 on ARMv7; exact size is 128 bytes. */
#ifndef IOS7LAB_I9100_HANDOFF_H
#define IOS7LAB_I9100_HANDOFF_H
#define IOS7LAB_I9100_MAGIC 0x4a314831U /* "J1H1" — wire magic, shared ABI */
#define IOS7LAB_I9100_VERSION 1U
#define IOS7LAB_I9100_HANDOFF_BYTES 128U
#define IOS7LAB_I9100_DT_PROPERTY "ios7lab-j1-handoff"
#define IOS7LAB_I9100_MACHINE 0x4210U /* Exynos 4210 */
#define IOS7LAB_I9100_RGB565 0x35363552U
#define IOS7LAB_I9100_FLAGS 3U
#define IOS7LAB_I9100_SCTLR_REJECT 0x52080085U
/* i9100 display: direct framebuffer RAM, no DMA engine (same direct-FB
 * design as J1 — uniLoader FIMD window or the bring-up scratch FB).
 * DISP_PHYSICAL is the FB RAM base itself; MAP covers FB + control. */
#define IOS7LAB_I9100_DISP_PHYSICAL 0x5E000000U
#define IOS7LAB_I9100_DISP_MAP_BYTES 0x00200000U
#define IOS7LAB_I9100_FB_WIDTH 480U
#define IOS7LAB_I9100_FB_HEIGHT 800U
#define IOS7LAB_I9100_FB_ROW 1920U
#define IOS7LAB_I9100_FB_BYTES 1536000U
#define IOS7LAB_I9100_EARLY_VM_RESERVE 0x00220000U
#define IOS7LAB_I9100_STAGE_BASE 0xe000U
/* i9100 memory window (uniLoader i9100_defconfig, factual): */
#define IOS7LAB_I9100_PHYS_BASE 0x40000000U
#define IOS7LAB_I9100_MEM_SIZE 0x30000000U
/* i9100 MMIO (factual, reference only): */
#define IOS7LAB_I9100_GIC_DIST 0x10490000U
#define IOS7LAB_I9100_GIC_CPU 0x10480000U
#define IOS7LAB_I9100_MCT_BASE 0x10050000U
#define IOS7LAB_I9100_UART_BASE 0x13820000U
#define IOS7LAB_I9100_SDMMC_BASE 0x12530000U /* external microSD host */
typedef struct ios7lab_i9100_handoff {
    unsigned int magic, version, bytes, flags;
    unsigned int machine, entry_sctlr, entry_cpsr, entry_atags;
    unsigned int disp_physical, dma_config, dma_size, dma_address;
    unsigned int dma_stride, dma_xy, width, height;
    unsigned int row_bytes, pixel_format, framebuffer_bytes, memory_base;
    unsigned int memory_bytes, kernel_base, kernel_extent, top_of_kernel_data;
    unsigned int loader_start, loader_end, initrd_start, initrd_end;
    unsigned int atags_end, checkpoint_count, reserved0, reserved1;
} ios7lab_i9100_handoff;
#endif
