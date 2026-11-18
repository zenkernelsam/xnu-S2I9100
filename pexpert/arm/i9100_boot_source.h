#ifndef I9100_BOOT_SOURCE_H
#define I9100_BOOT_SOURCE_H
#define I9100_BOOT_SOURCE_MAGIC 0x53503949U
#define I9100_BOOT_SOURCE_PROPERTY "i9100-boot-source"
struct I9100BootSource {
 unsigned magic,version,flags,inform2,inform3,sboot_mode,r1,r2;
};
#endif
