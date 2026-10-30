#ifndef _ARM_IOS7_COMPAT_H_
#define _ARM_IOS7_COMPAT_H_

/* Selected software ABI adapters only. This never changes board identity. */
#define IOS7_COMPAT_PLATFORM (BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO || BOARD_CONFIG_EXYNOS3475_J1XLTE || BOARD_CONFIG_EXYNOS4210_I9100)

#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_EXYNOS4210_I9100
#include <pexpert/pexpert.h>
#endif

static inline int ios7_compat_vmabi_enabled(void)
{
#if BOARD_CONFIG_EXYNOS4210_I9100
    /* Default ON for the iOS 7.1.2 userland. Passing ios7lab_vmabi=0
     * switches the shared-region window, exec/execve quirks and the
     * rpc guards back to the stock (iOS<=6-era) behaviour so the same
     * kernel can A/B an iOS 6 rootfs. */
    int enabled;
    if (PE_parse_boot_argn("ios7lab_vmabi", &enabled, sizeof(enabled)))
        return enabled == 1;
    return 1;
#elif BOARD_CONFIG_QSD8250_LEO || BOARD_CONFIG_EXYNOS3475_J1XLTE
    return 1;
#elif BOARD_CONFIG_ARMPBA8
    int enabled = 0;
    return PE_parse_boot_argn("ios7lab_vmabi", &enabled, sizeof(enabled)) && enabled == 1;
#else
    return 0;
#endif
}
#endif
