#ifndef I9100_EMMC_CLOCK_FACADE_H
#define I9100_EMMC_CLOCK_FACADE_H
#include <stdint.h>
enum {kSecondScale=1000000000U,THREAD_UNINT=0,THREAD_TIMED_OUT=2};
extern "C" void clock_interval_to_deadline(uint32_t,uint32_t,uint64_t *);
#endif
