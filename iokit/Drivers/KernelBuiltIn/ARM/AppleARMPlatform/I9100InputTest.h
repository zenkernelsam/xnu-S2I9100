#ifndef I9100_INPUT_TEST_H
#define I9100_INPUT_TEST_H
#include <stdint.h>
static inline int i9100_input_valid(uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,uint32_t ms)
{ return x0<480 && x1<480 && y0<800 && y1<800 && ms>=80 && ms<=1500; }
static inline uint32_t i9100_input_coordinate(uint32_t from,uint32_t to,uint64_t elapsed,uint64_t duration)
{
 if(!duration || elapsed>=duration)return to;
 uint32_t distance=to>=from?to-from:from-to;
 uint32_t change=(uint32_t)((uint64_t)distance*elapsed/duration);
 return to>=from?from+change:from-change;
}
#endif
