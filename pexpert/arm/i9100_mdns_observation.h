#ifndef I9100_MDNS_OBSERVATION_H
#define I9100_MDNS_OBSERVATION_H
/* Observation only; helper's retained sixteen-character proc name is distinct
 * from the daemon. Neither role changes any permission or syscall result. */
static unsigned i9100_mdns_observation_role(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "mDNSResponder")) return 1;
    if (!strncmp(name, "mDNSResponderHelper", 16)) return 2;
    return 0;
}
#endif
