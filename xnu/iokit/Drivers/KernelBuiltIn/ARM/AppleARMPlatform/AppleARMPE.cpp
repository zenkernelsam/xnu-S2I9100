//
//  AppleARMPE.cpp
//  AppleARMPlatform
//
//  Created by rms on 5/21/13.
//  Copyright (c) 2013 rms. All rights reserved.
//

#include "AppleARMPE.h"
#include <kern/debug.h>
#include <pexpert/device_tree.h>

#define HIGH_SCORE  100000
#define super IOPlatformExpert

OSDefineMetaClassAndStructors(AppleARMPE, IODTPlatformExpert);

bool AppleARMPE::init(OSDictionary * propTable)
{
    if (!super::init(propTable)) {
        panic("IOPlatformExpert failed to initialize");
    }
    return true;
}

extern const IORegistryPlane *gIODTPlane;
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
extern void ios7leo_register_sd(IOService *platform);
extern void ios7leo_register_display(IOService *platform);
extern void ios7leo_prepare_root_file();
extern void ios7leo_register_hid_touch(IOService *platform);
extern "C" void ios7leo_prepare_log_reader(void);
#endif
#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE)
extern void ios7j1_register_sd(IOService *platform);
#endif
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
extern "C" void ios7i9100_register_sd(IOService *platform);
#endif
#if BOARD_CONFIG_ARMPBA8
extern void ios7lab_register_display(IOService *platform);
extern void ios7lab_register_hid_touch(IOService *platform);
extern void ios7lab_register_sd(IOService *platform);
#endif

void AppleARMPE::registerNVRAMController(IONVRAMController * caller)
{
    publishResource("IONVRAM");
}

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
extern "C" int i9100_smp_init(void);
extern "C" void i9100_fimd_selftest(void);
extern "C" void i9100_smp_report(void);
#endif

bool AppleARMPE::start(IOService * provider)
{
    DTEntry entry;
    char *dtype;
    unsigned int size;

    IOLog("AppleARMPE::start: Welcome to the NeXT generation.\n");

    if (!super::start(provider)) {
        panic("IOPlatformExpert failed to start");
    }

    removeProperty(kIOPlatformMapperPresentKey);
    assert(IOService::getPlatform() == this);

    registerService();

    /*
     * Let these time out to let everything else initialize right. 
     */
#if 0
    publishResource("IONVRAM");
    publishResource("IORTC");
#endif

    OSData *        prop;
    bool        ok = false;

    prop = (OSData *) getProvider()->getProperty( gIODTModelKey );
    ok = (0 != prop);

    if( ok )
        populate_model_name((char *) prop->getBytesNoCopy());
    else
        populate_model_name("Power Macintosh");

#if BOARD_CONFIG_ARMPBA8
    ios7lab_register_display(this);
    ios7lab_register_hid_touch(this);
    ios7lab_register_sd(this);
#endif
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
    ios7leo_register_display(this);
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
    ios7leo_prepare_root_file();
#endif
#if defined(BOARD_CONFIG_EXYNOS3475_J1XLTE)
    ios7j1_register_sd(this);
#elif defined(BOARD_CONFIG_QSD8250_LEO)
    ios7leo_prepare_log_reader(); /* optional owned logger before rawmedia publication */
    ios7leo_register_sd(this);
#elif defined(BOARD_CONFIG_EXYNOS4210_I9100)
    ios7i9100_register_sd(this);
#endif
    ios7leo_register_hid_touch(this);
#endif
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    {
        extern int PE_parse_boot_argn(const char *, void *, int);
        int smp_arg = 1;
        boolean_t found = PE_parse_boot_argn("i9100smp", &smp_arg, sizeof(smp_arg));
        if (found && smp_arg == 0)
            ;
        else {
            (void) i9100_smp_init();
            i9100_smp_report();
        }
    }
    {
        extern int PE_parse_boot_argn(const char *, void *, int);
        int fimd_arg = 0;
        if (PE_parse_boot_argn("i9100fimdtest", &fimd_arg, sizeof(fimd_arg)) && fimd_arg)
            i9100_fimd_selftest();
    }
#endif
    return true;
}

const char *AppleARMPE::deleteList(void)
{
    return "";
}

const char *AppleARMPE::excludeList(void)
{
    return ("'chosen', 'memory', 'options', 'aliases'");
}

IOService *AppleARMPE::probe(IOService * provider, SInt32 * score)
{
    return this;
}

bool AppleARMPE::getMachineName(char *name, int maxLength)
{
    OSData *        prop;
    bool        ok = false;

    maxLength--;
    prop = (OSData *) getProvider()->getProperty( gIODTModelKey );
    ok = (0 != prop);

    if( ok )
        strlcpy( name, (const char *) prop->getBytesNoCopy(), maxLength );
    else 
        strlcpy( name, "Power Macintosh", maxLength );
    
    return(true);
}
