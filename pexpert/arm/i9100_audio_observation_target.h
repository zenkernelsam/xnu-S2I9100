#ifndef I9100_AUDIO_OBSERVATION_TARGET_H
#define I9100_AUDIO_OBSERVATION_TARGET_H
/* version,state(0/1 selecting/2 ready),pid,task,slide,runtime,calls,stage.
 * Select only one actual callback execution, shared by all observers. */
extern volatile uint32_t i9100_audio_observation_target[8];
int i9100_audio_select_task(unsigned,unsigned);
#endif
