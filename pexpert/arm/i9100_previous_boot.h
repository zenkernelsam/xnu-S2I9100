#ifndef I9100_PREVIOUS_BOOT_H
#define I9100_PREVIOUS_BOOT_H
#define I9100_PREVIOUS_BOOT_MAGIC 0x42503949U
#define I9100_PREVIOUS_BOOT_PROPERTY "i9100-prev-boot"
struct I9100PreviousBoot {
    unsigned magic, version, bytes, valid, inform6, inform7;
    unsigned stage, reason, pc, lr, far, fsr, reserved[4];
};
extern struct I9100PreviousBoot i9100_previous_boot;
#endif
