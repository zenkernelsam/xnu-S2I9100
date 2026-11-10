#ifndef I9100_TCC_EXTENSION_H
#define I9100_TCC_EXTENSION_H
/* The iOS6 library itself emits "invalid" for an unconfined extension and
 * consumes it as handle0 without a syscall. This is a development no-op,
 * not a Seatbelt capability or a replacement for TCC consent decisions. */
static int i9100_tcc_extension_gate(const char *caller, const char *policy,
    int operation, unsigned providers, int enabled, int original)
{
    return enabled == 1 && providers == 0 && original == ENOPOLICY &&
        caller && !strcmp(caller, "tccd") && policy && !strcmp(policy, "Sandbox") && operation == 5;
}
static int i9100_tcc_extension_wire(const uint32_t *w)
{
    /* libsystem_sandbox393083b4 passes five wide fields even on ARM32:
     * class,type,file,flags,token-buffer. Only file extensions are covered. */
    return w && !w[1] && !w[3] && !w[5] && !w[7] && !w[9] && w[2] == 0 &&
        w[0] >= 0x1000 && w[0] < 0x40000000 &&
        w[4] >= 0x1000 && w[4] < 0x40000000 &&
        w[8] >= 0x1000 && w[8] <= 0x3ffffff8;
}
static int i9100_tcc_extension_class(const char *name, const char *path)
{
    if (!name || !path || path[0] != '/') return 0;
    return !strcmp(name, "com.apple.tcc.kTCCServiceAddressBook") ||
        !strcmp(name, "com.apple.tcc.kTCCServiceCalendar") ||
        !strcmp(name, "com.apple.tcc.kTCCServiceReminders") ||
        !strcmp(name, "com.apple.tcc.kTCCServicePhotos");
}
#endif
