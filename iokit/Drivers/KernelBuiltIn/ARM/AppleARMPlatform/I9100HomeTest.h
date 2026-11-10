#ifndef I9100_HOME_TEST_H
#define I9100_HOME_TEST_H
/* Explicit diagnostic only: one Menu press/release after the HID reader has
 * opened. Physical input wins; normal boots never arm this state machine. */
typedef struct { unsigned enabled, phase; uint64_t deadline; } I9100HomeTest;
static unsigned i9100_home_test_sample(I9100HomeTest *s, unsigned physical,
    unsigned opened, uint64_t now, uint64_t delay, uint64_t hold)
{
    if (!s->enabled || s->phase == 3) return physical;
    if (s->phase && (!opened || physical)) { s->phase = 3; return physical; }
    if (!s->phase) {
        if (!opened || physical) return physical;
        s->phase = 1; s->deadline = now+delay;
    }
    if (s->phase == 1 && now >= s->deadline) { s->phase = 2; s->deadline = now+hold; }
    if (s->phase == 2 && now >= s->deadline) { s->phase = 3; return physical; }
    return s->phase == 2 ? 1 : physical;
}
#endif
