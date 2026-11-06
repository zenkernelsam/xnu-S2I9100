/* i9100 root discovery policy. Shared with the offline callback tests. */
#ifndef I9100_ROOT_WAIT_H
#define I9100_ROOT_WAIT_H
enum {
    I9100_ROOT_BOOTING,
    I9100_ROOT_WAITING,
    I9100_ROOT_MISSING,
    I9100_ROOT_MEDIA_FOUND
};
#define I9100_ROOT_POLL_SECONDS 5U
#define I9100_ROOT_GRACE_POLLS 6U
struct i9100_root_wait_ops {
    void *cookie;
    void *(*wait_for_media)(void *, unsigned int seconds);
    void (*report_state)(void *, unsigned int state);
};
/* The wait callback sleeps with interrupts/scheduling usable. Never invent a
 * root device, panic or auto-reset because the requested media is absent. */
static inline void *i9100_wait_for_root(const struct i9100_root_wait_ops *ops)
{
    unsigned int misses = 0;
    ops->report_state(ops->cookie, I9100_ROOT_WAITING);
    for (;;) {
        void *media = ops->wait_for_media(ops->cookie, I9100_ROOT_POLL_SECONDS);
        if (media) {
            ops->report_state(ops->cookie, I9100_ROOT_MEDIA_FOUND);
            return media;
        }
        if (misses < I9100_ROOT_GRACE_POLLS &&
            ++misses == I9100_ROOT_GRACE_POLLS)
            ops->report_state(ops->cookie, I9100_ROOT_MISSING);
    }
}
#ifdef __cplusplus
extern "C" {
#endif
void i9100_usbcdc_set_root_state(unsigned int state);
#ifdef __cplusplus
}
#endif
#endif
