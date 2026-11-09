#ifndef I9100_TCC_DEVELOPMENT_H
#define I9100_TCC_DEVELOPMENT_H
/* Explicit unconfined development mode. This is not Seatbelt enforcement.
 * It accepts only tccd's missing Sandbox profile initialization while the
 * entire MAC policy table is empty; existing policies/errors stay intact. */
static int i9100_tcc_development_allowed(const char *caller, const char *policy,
    int operation, unsigned providers, int enabled, int original_error)
{
    return enabled == 1 && providers == 0 && original_error == ENOPOLICY &&
        caller && !strcmp(caller, "tccd") && policy && !strcmp(policy, "Sandbox") &&
        (operation == 0 || operation == 1);
}
#endif
