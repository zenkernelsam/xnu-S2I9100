#ifndef I9100_MDNS_DEVELOPMENT_H
#define I9100_MDNS_DEVELOPMENT_H
/* Explicit unconfined MDNS initialization only. This is not Seatbelt.
 * Queries/checks and helper processes keep their original outcomes. */
static int i9100_mdns_development_allowed(const char *caller, const char *policy,
    int operation, unsigned providers, int enabled, int original_error)
{
    return enabled == 1 && providers == 0 && original_error == ENOPOLICY &&
        caller && !strcmp(caller, "mDNSResponder") && policy && !strcmp(policy, "Sandbox") &&
        operation == 1;
}
#endif
