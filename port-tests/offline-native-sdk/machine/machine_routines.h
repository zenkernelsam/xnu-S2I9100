#ifdef __cplusplus
extern "C" {
#endif
int ml_get_interrupts_enabled(void);
int ml_at_interrupt_context(void);
int ml_set_interrupts_enabled(int);
#ifdef __cplusplus
}
#endif
